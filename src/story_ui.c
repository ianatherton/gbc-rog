#pragma bank 14

#include "story_ui.h"
#include "debug_bank.h"
#include "defs.h"
#include "globals.h"
#include "lcd.h"
#include "map.h"
#include "render.h"
#include <gb/cgb.h>
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <string.h>

BANKREF(story_ui_run_before_first_floor)

/* Reuse explored_bits[] before lighting_reset — saves ~511 B WRAM so stack stays clear of class_emblem_draw ~320 B frame */
#define ST_OFF_LINES   (G_STORY_BIGBUF_CAP)
#define ST_OFF_NLINES  (ST_OFF_LINES + G_STORY_MAX_LINES * 2u) // 400 + 56 — keep preprocessor-safe (no casts) for #if below
#define ST_OFF_TTL     (ST_OFF_NLINES + 1u)
#define ST_OFF_X       (ST_OFF_TTL + G_STORY_FIRE_COUNT)
#define ST_OFF_Y       (ST_OFF_X + G_STORY_FIRE_COUNT)
#define ST_SCRATCH_END (ST_OFF_Y + G_STORY_FIRE_COUNT)

#if ST_SCRATCH_END > BITSET_BYTES
#error story scratch exceeds explored_bits
#endif

#define story_bigbuf    (explored_bits + 0u)
#define story_line_off  ((uint16_t *)(explored_bits + ST_OFF_LINES))
#define story_nlines    (explored_bits[ST_OFF_NLINES])
#define story_fire_ttl  (explored_bits + ST_OFF_TTL)
#define story_fire_x    (explored_bits + ST_OFF_X)
#define story_fire_y    (explored_bits + ST_OFF_Y)

#define STORY_TEXT_COLS   18u
#define STORY_CRAWL_ROWS  18u // visible BKG rows used for crawl (0 = top, 17 = bottom)
#define STORY_HOLD_TAIL_ROWS 10u // stop crawl with this many wrapped lines on bottom rows (17 = last line)
#if STORY_HOLD_TAIL_ROWS > STORY_CRAWL_ROWS
#error STORY_HOLD_TAIL_ROWS must fit in crawl window
#endif
#define STORY_LEAD_LINES  3u // virtual blank lines before prose (was 20 ≈8s @24 VBlanks/step; 6 ≈2.4s)
#define STORY_FIRE_FIRST  10u
#define STORY_SCROLL_EVERY 24u // VBlanks per crawl step — 4× slower than 3 (was ~20 ms/line @60Hz → ~200 ms/line)
#define STORY_TEXT_COL0  1u  // center 18 cols in 20-tile map

static const palette_color_t story_bkg_pal[] = { // pen 3 = bright red on black paper 0
    RGB(0, 0, 0),
    RGB(8, 0, 2),
    RGB(18, 2, 4),
    RGB(31, 8, 10),
};
static const palette_color_t story_fire_pal[] = { // OCP3 — match ladder/brazier fire ramp
    RGB(0, 0, 0),
    RGB(6, 8, 12),
    RGB(31, 16, 2),
    RGB(31, 26, 8),
};
static const palette_color_t story_gold_pal[] = { // BGP PAL_XP_UI — match render.c pal_xp_ui for story hilites
    RGB(0, 0, 0),
    RGB(23, 9, 0),
    RGB(30, 17, 0),
    RGB(31, 27, 1),
};

#define STORY_HILITE_SLOTS 6u
static uint16_t story_h_lo[STORY_HILITE_SLOTS];
static uint16_t story_h_hi[STORY_HILITE_SLOTS]; // exclusive byte offset in story_bigbuf
static uint8_t story_h_n;

static const char *story_strstr(const char *hay, const char *needle) { // GBDK string.h has no strstr
    uint16_t hlen = (uint16_t)strlen(hay);
    uint16_t nlen = (uint16_t)strlen(needle);
    uint16_t i, k;
    if (nlen == 0u || nlen > hlen) return NULL;
    for (i = 0u; i <= hlen - nlen; i++) {
        for (k = 0u; k < nlen && hay[i + k] == needle[k]; k++) {}
        if (k == nlen) return hay + i;
    }
    return NULL;
}

static void story_hilites_build(const char *cont, const char *pcl) {
    uint8_t j = 0u;
    const char *p;
    p = story_strstr(story_bigbuf, "MARA");
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + 4u);
        j++;
    }
    p = story_strstr(story_bigbuf, "Crimson Keep");
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + 12u); // "Crimson Keep"
        j++;
    }
    p = story_strstr(story_bigbuf, "Mara's");
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + 6u); // M a r a ' s
        j++;
    }
    p = story_strstr(story_bigbuf, "your realm");
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + 10u);
        j++;
    }
    p = story_strstr(story_bigbuf, cont);
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + (uint16_t)strlen(cont));
        j++;
    }
    p = story_strstr(story_bigbuf, pcl);
    if (p != NULL && j < STORY_HILITE_SLOTS) {
        story_h_lo[j] = (uint16_t)(p - story_bigbuf);
        story_h_hi[j] = (uint16_t)(story_h_lo[j] + (uint16_t)strlen(pcl));
        j++;
    }
    story_h_n = j;
}

static uint8_t story_hilite_gold(uint16_t off) {
    uint8_t j;
    for (j = 0u; j < story_h_n; j++) {
        if (off >= story_h_lo[j] && off < story_h_hi[j]) return 1u;
    }
    return 0u;
}

/* Each token <= 5 letters; terminal suf2 unchanged */
static const char *const story_continent_pref[] = {
    "Aethr", "Dusk", "Iron", "Thorn", "Ash", "Storm", "Bleak", "Gold", "Rime", "Ember",
    "Mist", "Frost", "Gloom", "Raven", "Swift", "Steel", "Stone", "Night", "Dawn", "Moon",
    "Void", "Cloud", "Bane", "Crow", "Hawk", "Wolf", "Star", "Pale", "Dark", "Bramb",
    "Whisp", "Crims", "Quiet", "Holow", "Wintr", "Summr", "Drift", "Shado", "Wyrm", "Fell",
    "Grim", "Salt", "Sand", "North", "South", "East", "West", "Grey", "High", "Far",
};
static const char *const story_continent_suf[] = {
    "vale", "spire", "march", "wild", "fen", "coast", "moor", "leaf", "hold", "deep",
    "ford", "wold", "glen", "dale", "ridge", "brook", "crag", "peak", "haven", "mire",
    "pass", "rune", "gate", "ward", "isle", "bay", "rock", "dune", "tide", "shade",
    "marsh", "basin", "strat", "fjord", "gulch", "chasm", "vault", "crypt", "nook", "dell",
    "thorn", "bloom", "reach", "chann", "harbr", "summt",
};
static const char *const story_continent_suf2[] = {
    "os", "us", "od", "id", "", "es", "un", "", "",
};
#define STORY_CONTINENT_PREFIX_N (sizeof(story_continent_pref) / sizeof((story_continent_pref)[0]))
#define STORY_CONTINENT_SUFFIX_N (sizeof(story_continent_suf) / sizeof((story_continent_suf)[0]))
#define STORY_CONTINENT_SUFFIX2_N (sizeof(story_continent_suf2) / sizeof((story_continent_suf2)[0]))

static void story_build_bigbuf(const char *cont, const char *pcl) {
    strcpy(story_bigbuf,
        "In another world the witch-demon MARA emerged from human spite.\n\n"
        "Her town's castle became a sunken fortress of hate.\n\n"
        "Within her Crimson Keep her powers grew...\n\n"
        "Now, creeping tendrils of her rage have torn through worldly barriers into your realm.\n\n"
        "Mara's evil has arrived- Here in the continent of ");
    strcat(story_bigbuf, cont);
    strcat(story_bigbuf, "\n\nYou, as the last known ");
    strcat(story_bigbuf, pcl);
    strcat(story_bigbuf, " of repute in the region, sense a quest...");
}

static const char *class_label(uint8_t c) {
    if (c == 1u) return "SCOUNDREL";
    if (c == 2u) return "WITCH";
    if (c == 3u) return "ZERKER";
    return "KNIGHT";
}

// Insert '\n' at pos; shifts tail right by 1 (needs spare capacity in story_bigbuf)
static void story_insert_nl(uint16_t pos) {
    uint16_t L = (uint16_t)strlen(story_bigbuf);
    if (L + 1u >= G_STORY_BIGBUF_CAP) return;
    memmove(story_bigbuf + pos + 1u, story_bigbuf + pos, (size_t)(L - pos + 1u));
    story_bigbuf[pos] = '\n';
}

// Word-wrap in place: explicit \n preserved; long runs split at last space in column window or hard-split with insert
static void story_wrap_newlines(void) {
    uint16_t ls = 0u;
    for (;;) {
        if (!story_bigbuf[ls]) return;
        {
            uint16_t i = ls;
            uint8_t n = 0u;
            while (story_bigbuf[i] && story_bigbuf[i] != '\n' && n < STORY_TEXT_COLS) {
                i++;
                n++;
            }
            if (!story_bigbuf[i]) return;
            if (story_bigbuf[i] == '\n') {
                ls = (uint16_t)(i + 1u);
                continue;
            }
            {
                uint16_t br = (uint16_t)(ls + (uint16_t)STORY_TEXT_COLS); // first char past width
                while (br > ls && story_bigbuf[(uint16_t)(br - 1u)] != ' ') br--;
                if (br > ls) {
                    story_bigbuf[(uint16_t)(br - 1u)] = '\n';
                    ls = br;
                } else {
                    uint16_t ins = (uint16_t)(ls + (uint16_t)STORY_TEXT_COLS);
                    uint16_t Ln = (uint16_t)strlen(story_bigbuf);
                    if (Ln + 1u >= G_STORY_BIGBUF_CAP) {
                        story_bigbuf[ins] = '\n'; // no tail room — clobber 19th char so wrap always advances
                        ls = (uint16_t)(ins + 1u);
                    } else {
                        story_insert_nl(ins);
                        ls = (uint16_t)(ins + 1u);
                    }
                }
            }
        }
    }
}

static void story_build_line_table(void) {
    uint16_t i = 0u;
    uint8_t line = 0u;
    story_line_off[0] = 0u;
    for (; story_bigbuf[i]; i++) {
        if (story_bigbuf[i] == '\n') {
            if (line + 1u >= G_STORY_MAX_LINES) break;
            line++;
            story_line_off[line] = (uint16_t)(i + 1u);
        }
    }
    story_nlines = (uint8_t)(line + 1u);
}

static void story_fire_init(void) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) story_fire_ttl[i] = 0u;
}

static void story_fire_try_spawn(uint16_t fc) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        if (story_fire_ttl[i] != 0u) continue;
        {
            uint8_t x = (uint8_t)(8u + (uint8_t)((uint16_t)(DIV_REG + fc + (uint16_t)i * 17u) % 136u));
            story_fire_x[i] = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + x);
            story_fire_y[i] = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + 132u + (uint8_t)(DIV_REG & 7u));
            story_fire_ttl[i] = (uint8_t)(28u + (uint8_t)(DIV_REG & 15u));
            return;
        }
    }
}

static void story_fire_tick(uint16_t fc) {
    uint8_t i;
    uint8_t ft = (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE);
    uint8_t fp = (uint8_t)(PAL_WALL_BG & 7u);
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        if (story_fire_ttl[i] == 0u) continue;
        {
            uint8_t y = story_fire_y[i];
            int16_t nx = (int16_t)story_fire_x[i];
            if (((fc + (uint16_t)i) & 3u) == 1u) nx += 2;
            else if (((fc + (uint16_t)i) & 3u) == 3u) nx -= 2;
            if (nx < 8) nx = 8;
            if (nx > 152) nx = 152;
            y = (uint8_t)((uint16_t)y - 1u);
            if (((fc + i) & 1u) == 0u) y = (uint8_t)((uint16_t)y - 1u);
            story_fire_ttl[i]--;
            story_fire_y[i] = y;
            story_fire_x[i] = (uint8_t)nx;
            if (story_fire_ttl[i] == 0u || y < 24u) {
                story_fire_ttl[i] = 0u;
                move_sprite((uint8_t)(STORY_FIRE_FIRST + i), 0u, 0u);
            } else {
                set_sprite_tile((uint8_t)(STORY_FIRE_FIRST + i), ft);
                set_sprite_prop((uint8_t)(STORY_FIRE_FIRST + i), fp);
                move_sprite((uint8_t)(STORY_FIRE_FIRST + i), (uint8_t)nx, y);
            }
        }
    }
}

static void story_fire_hide(void) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        story_fire_ttl[i] = 0u;
        move_sprite((uint8_t)(STORY_FIRE_FIRST + i), 0u, 0u);
    }
}

static void story_draw_visible(int16_t scroll_i) { // scroll_i = doc lines risen past bottom anchor (0 = crawl start)
    uint8_t sy, x, i;
    for (sy = 0u; sy < STORY_CRAWL_ROWS; sy++) {
        int16_t doc_line = (int16_t)sy + scroll_i - (int16_t)(STORY_CRAWL_ROWS - 1u); // bottom row sy=17 shows virtual line scroll_i
        const char *ln = NULL;
        for (x = 0u; x < 20u; x++) {
            gotoxy(x, sy);
            setchar(' ');
        }
        if (doc_line >= (int16_t)STORY_LEAD_LINES
                && doc_line < (int16_t)STORY_LEAD_LINES + (int16_t)story_nlines) {
            uint8_t li = (uint8_t)((uint16_t)doc_line - (uint16_t)STORY_LEAD_LINES);
            ln = story_bigbuf + story_line_off[li];
            for (i = 0u; i < STORY_TEXT_COLS && ln[i] && ln[i] != '\n'; i++) {
                gotoxy((uint8_t)(STORY_TEXT_COL0 + i), sy);
                setchar(ln[i]);
            }
        }
        VBK_REG = VBK_ATTRIBUTES;
        for (x = 0u; x < 20u; x++) {
            uint8_t a = 0u;
            if (ln != NULL && x >= STORY_TEXT_COL0 && x < (uint8_t)(STORY_TEXT_COL0 + STORY_TEXT_COLS)) {
                uint8_t ti = (uint8_t)(x - STORY_TEXT_COL0);
                if (ln[ti] && ln[ti] != '\n'
                        && story_hilite_gold((uint16_t)((uint16_t)(ln - story_bigbuf) + ti)))
                    a = (uint8_t)PAL_XP_UI;
            }
            set_bkg_attribute_xy(x, sy, a);
        }
        VBK_REG = VBK_TILES;
    }
}

void story_ui_run_before_first_floor(void) BANKED {
    uint16_t fc = 0u;
    int16_t scroll_i = 0; // virtual doc lines advanced upward each tick — bottom anchor (row 17) shows line scroll_i
    int16_t scroll_cap; // last doc line on bottom row → rows (CRAWL-HOLD_TAIL)..17 show last HOLD_TAIL wrapped lines
    uint8_t prev_j = 0u, sk = 0u;
    char cont_buf[22]; // pref + suf + suf2 + NUL (e.g. Aether+coast+un)
    uint16_t mix = (uint16_t)(run_seed ^ ((uint16_t)player_class * 131u));
    uint8_t pi = (uint8_t)(mix % (uint16_t)STORY_CONTINENT_PREFIX_N);
    uint8_t si = (uint8_t)(((uint16_t)(mix >> 5) ^ (uint16_t)pi * 13u ^ (uint16_t)player_class * 3u) % (uint16_t)STORY_CONTINENT_SUFFIX_N);
    uint8_t s2 = (uint8_t)(((uint16_t)(mix >> 9) ^ (uint16_t)si * 5u ^ (uint16_t)pi * 7u ^ (uint16_t)player_class) % (uint16_t)STORY_CONTINENT_SUFFIX2_N);
    const char *cont = cont_buf;
    const char *pcl = class_label(player_class);
    strcpy(cont_buf, story_continent_pref[pi]);
    strcat(cont_buf, story_continent_suf[si]);
    strcat(cont_buf, story_continent_suf2[s2]);

    BANK_DBG("story");
    memset(explored_bits, 0, (size_t)ST_SCRATCH_END); // fog array not live until gen — frees ~511 B WRAM vs dedicated story globals
    story_build_bigbuf(cont, pcl);
    story_wrap_newlines();
    story_build_line_table();
    story_hilites_build(cont, pcl);
    scroll_cap = (int16_t)STORY_LEAD_LINES + (int16_t)story_nlines - 1; // bottom row = last prose doc line; rows (CRAWL-HOLD_TAIL)..17 = last HOLD_TAIL lines when nlines>=TAIL; hold until A/START

    lcd_gameplay_active = 0u;
    wait_vbl_done();
    SCY_REG = 0u;
    SCX_REG = 0u;
    lcd_clear_display();
    set_bkg_palette(0u, 1u, story_bkg_pal);
    set_bkg_palette((uint8_t)PAL_XP_UI, 1u, story_gold_pal);
    set_sprite_palette(PAL_WALL_BG, 1u, story_fire_pal);
    font_color(3u, 0u);
    story_fire_init();
    SHOW_SPRITES;
    story_draw_visible(scroll_i);

    for (;;) {
        uint8_t j = joypad();
        uint8_t e = (uint8_t)(j & (uint8_t)~prev_j);
        prev_j = j;
        if (e & (J_START | J_A)) break;
        story_fire_try_spawn(fc);
        story_fire_try_spawn((uint16_t)(fc + 7u));
        story_fire_tick(fc);
        sk++;
        if (sk >= STORY_SCROLL_EVERY) {
            sk = 0u;
            if (scroll_i < scroll_cap) scroll_i++;
            story_draw_visible(scroll_i);
        }
        fc++;
        wait_vbl_done();
    }

    story_fire_hide();
    HIDE_WIN;
    SCY_REG = 0u;
    wait_vbl_done();
    lcd_clear_display(); // wipe story tiles+attrs before loading ISR paints — avoids ghost text one frame
    load_palettes();
    font_color(3u, 0u);
    g_prev_j = 0u;
    memset(explored_bits, 0, BITSET_BYTES); // before level_generate lighting_reset; drop prose overlay from fog bitset
    BANK_DBG("story_x");
}
