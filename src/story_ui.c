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
#define STORY_LEAD_LINES  20u // virtual blank doc lines before prose — bottom row stays empty until crawl reaches here
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

static const char *const story_continents[] = {
    "Aethervale",
    "Duskspire",
    "Ironmarch",
    "Thornwild",
    "Ashfen",
    "Stormcoast",
    "Bleakmoor",
    "Goldleaf",
    "Rimehold",
    "Emberdeep",
};
#define STORY_CONTINENT_N (sizeof story_continents / sizeof story_continents[0])

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
        for (x = 0u; x < 20u; x++) {
            gotoxy(x, sy);
            setchar(' ');
        }
        if (doc_line >= (int16_t)STORY_LEAD_LINES
                && doc_line < (int16_t)STORY_LEAD_LINES + (int16_t)story_nlines) {
            uint8_t li = (uint8_t)((uint16_t)doc_line - (uint16_t)STORY_LEAD_LINES);
            const char *ln = story_bigbuf + story_line_off[li];
            for (i = 0u; i < STORY_TEXT_COLS && ln[i] && ln[i] != '\n'; i++) {
                gotoxy((uint8_t)(STORY_TEXT_COL0 + i), sy);
                setchar(ln[i]);
            }
        }
        VBK_REG = VBK_ATTRIBUTES;
        for (x = 0u; x < 20u; x++) set_bkg_attribute_xy(x, sy, 0u);
        VBK_REG = VBK_TILES;
    }
}

void story_ui_run_before_first_floor(void) BANKED {
    uint16_t fc = 0u;
    int16_t scroll_i = 0; // virtual doc lines advanced upward each tick — bottom anchor (row 17) shows line scroll_i
    int16_t end_scroll;
    uint8_t prev_j = 0u, sk = 0u;
    uint8_t cidx = (uint8_t)((run_seed ^ (uint16_t)player_class * 131u) % (uint16_t)STORY_CONTINENT_N);
    const char *cont = story_continents[cidx];
    const char *pcl = class_label(player_class);

    BANK_DBG("story");
    memset(explored_bits, 0, (size_t)ST_SCRATCH_END); // fog array not live until gen — frees ~511 B WRAM vs dedicated story globals
    story_build_bigbuf(cont, pcl);
    story_wrap_newlines();
    story_build_line_table();
    end_scroll = (int16_t)STORY_LEAD_LINES + (int16_t)story_nlines + (int16_t)(STORY_CRAWL_ROWS + 6); // prose clears past top after crawl

    lcd_gameplay_active = 0u;
    wait_vbl_done();
    SCY_REG = 0u;
    SCX_REG = 0u;
    lcd_clear_display();
    set_bkg_palette(0u, 1u, story_bkg_pal);
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
            scroll_i++;
            story_draw_visible(scroll_i);
            if (scroll_i >= end_scroll) break;
        }
        fc++;
        wait_vbl_done();
    }

    story_fire_hide();
    HIDE_WIN;
    SCY_REG = 0u;
    load_palettes();
    font_color(3u, 0u);
    g_prev_j = 0u;
    memset(explored_bits, 0, BITSET_BYTES); // before level_generate lighting_reset; drop prose overlay from fog bitset
    BANK_DBG("story_x");
}
