#pragma bank 14

#include "story_ui.h"
#include "debug_bank.h"
#include "globals.h"
#include "lcd.h"
#include "render.h"
#include "map.h"
#include "defs.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <string.h>

BANKREF(story_ui_run_before_first_floor)

// ── Scratch buffer: reuse explored_bits[] before lighting_reset ───────────
// Saves ~511 B WRAM vs dedicated globals. Zeroed on entry, zeroed again on exit.
#define ST_OFF_LINES   (G_STORY_BIGBUF_CAP)
#define ST_OFF_NLINES  (ST_OFF_LINES  + G_STORY_MAX_LINES * 2u)
#define ST_OFF_TTL     (ST_OFF_NLINES + 1u)
#define ST_OFF_X       (ST_OFF_TTL    + G_STORY_FIRE_COUNT)
#define ST_OFF_Y       (ST_OFF_X      + G_STORY_FIRE_COUNT)
#define ST_SCRATCH_END (ST_OFF_Y      + G_STORY_FIRE_COUNT)
#if ST_SCRATCH_END > BITSET_BYTES
#error story scratch exceeds explored_bits
#endif
#define story_bigbuf   ((char *)(explored_bits))
#define story_line_off ((uint16_t *)(explored_bits + ST_OFF_LINES))
#define story_nlines   (explored_bits[ST_OFF_NLINES])
#define fire_ttl       (explored_bits + ST_OFF_TTL)
#define fire_ox        (explored_bits + ST_OFF_X)
#define fire_oy        (explored_bits + ST_OFF_Y)

// ── Layout ────────────────────────────────────────────────────────────────
#define CRAWL_ROWS    18u   // BKG rows used for scrolling text (0 = top, 17 = bottom)
#define TEXT_COLS     18u   // characters per wrapped line
#define TEXT_COL0      1u   // left margin (centres 18 cols in 20-tile map)
#define LEAD_LINES     3u   // blank doc-lines before prose begins
#define VBL_PER_TILE  24u   // VBL frames to advance one tile row (sets scroll speed)
#define VBL_PER_PX     3u   // VBL frames per pixel = VBL_PER_TILE / 8
#define FIRE_OAM_BASE 10u   // first OAM sprite slot for fire particles

// ── Palettes ──────────────────────────────────────────────────────────────
static const palette_color_t pal_bkg[] = {
    RGB(10,  2,  2), RGB(16,  5,  4), RGB(22, 10,  8), RGB(31, 31, 31),
};
static const palette_color_t pal_gold[] = {
    RGB(10,  2,  2), RGB(23,  9,  0), RGB(30, 17,  0), RGB(31, 27,  1),
};
static const palette_color_t pal_fire[] = {
    RGB( 0,  0,  0), RGB( 6,  8, 12), RGB(31, 16,  2), RGB(31, 26,  8),
};

// ── Highlights ────────────────────────────────────────────────────────────
#define HILITE_MAX 6u
static uint16_t hi_lo[HILITE_MAX];
static uint16_t hi_hi[HILITE_MAX];
static uint8_t  hi_n;

static const char *my_strstr(const char *hay, const char *needle) {
    uint16_t hl = (uint16_t)strlen(hay);
    uint16_t nl = (uint16_t)strlen(needle);
    uint16_t i, k;
    if (!nl || nl > hl) return 0;
    for (i = 0u; i <= hl - nl; i++) {
        for (k = 0u; k < nl && hay[i+k] == needle[k]; k++) {}
        if (k == nl) return hay + i;
    }
    return 0;
}

static void hi_add(const char *word) {
    const char *p = my_strstr(story_bigbuf, word);
    if (p && hi_n < HILITE_MAX) {
        hi_lo[hi_n] = (uint16_t)(p - story_bigbuf);
        hi_hi[hi_n] = (uint16_t)(hi_lo[hi_n] + (uint16_t)strlen(word));
        hi_n++;
    }
}

static uint8_t is_hi(uint16_t off) {
    uint8_t j;
    for (j = 0u; j < hi_n; j++)
        if (off >= hi_lo[j] && off < hi_hi[j]) return 1u;
    return 0u;
}

// ── Continent name tables ─────────────────────────────────────────────────
static const char *const cpref[] = {
    "Aethr","Dusk","Iron","Thorn","Ash","Storm","Bleak","Gold","Rime","Ember",
    "Mist","Frost","Gloom","Raven","Swift","Steel","Stone","Night","Dawn","Moon",
    "Void","Cloud","Bane","Crow","Hawk","Wolf","Star","Pale","Dark","Bramb",
    "Whisp","Crims","Quiet","Holow","Wintr","Summr","Drift","Shado","Wyrm","Fell",
    "Grim","Salt","Sand","North","South","East","West","Grey","High","Far",
};
static const char *const csuf[] = {
    "vale","spire","march","wild","fen","coast","moor","leaf","hold","deep",
    "ford","wold","glen","dale","ridge","brook","crag","peak","haven","mire",
    "pass","rune","gate","ward","isle","bay","rock","dune","tide","shade",
    "marsh","basin","strat","fjord","gulch","chasm","vault","crypt","nook","dell",
    "thorn","bloom","reach","chann","harbr","summt",
};
static const char *const csuf2[] = { "os","us","od","id","","es","un","","" };
#define CPREF_N (sizeof(cpref)  / sizeof(cpref[0]))
#define CSUF_N  (sizeof(csuf)   / sizeof(csuf[0]))
#define CSUF2_N (sizeof(csuf2)  / sizeof(csuf2[0]))

// ── Text generation ───────────────────────────────────────────────────────
static void build_text(const char *cont, const char *cls) {
    strcpy(story_bigbuf,
        "In another world the witch-demon MARA emerged from human spite.\n\n"
        "Her town's castle became a sunken fortress of hate.\n\n"
        "Within her Crimson Keep her powers grew...\n\n"
        "Now, creeping tendrils of her rage have torn through worldly barriers into your realm.\n\n"
        "Mara's evil has arrived- Here in the continent of ");
    strcat(story_bigbuf, cont);
    strcat(story_bigbuf, "\n\nYou, as the last known ");
    strcat(story_bigbuf, cls);
    strcat(story_bigbuf, " of repute in the region, sense a quest...");
}

static void insert_nl(uint16_t pos) {
    uint16_t L = (uint16_t)strlen(story_bigbuf);
    if (L + 1u >= G_STORY_BIGBUF_CAP) return;
    memmove(story_bigbuf + pos + 1u, story_bigbuf + pos, (size_t)(L - pos + 1u));
    story_bigbuf[pos] = '\n';
}

static void word_wrap(void) {
    uint16_t ls = 0u;
    for (;;) {
        if (!story_bigbuf[ls]) return;
        {
            uint16_t i = ls; uint8_t n = 0u;
            while (story_bigbuf[i] && story_bigbuf[i] != '\n' && n < TEXT_COLS) { i++; n++; }
            if (!story_bigbuf[i]) return;
            if (story_bigbuf[i] == '\n') { ls = (uint16_t)(i + 1u); continue; }
            {
                uint16_t br = (uint16_t)(ls + TEXT_COLS);
                while (br > ls && story_bigbuf[br - 1u] != ' ') br--;
                if (br > ls) { story_bigbuf[br - 1u] = '\n'; ls = br; }
                else {
                    uint16_t ins = (uint16_t)(ls + TEXT_COLS);
                    if ((uint16_t)strlen(story_bigbuf) + 1u >= G_STORY_BIGBUF_CAP)
                        { story_bigbuf[ins] = '\n'; ls = (uint16_t)(ins + 1u); }
                    else
                        { insert_nl(ins); ls = (uint16_t)(ins + 1u); }
                }
            }
        }
    }
}

static void build_lines(void) {
    uint16_t i = 0u; uint8_t line = 0u;
    story_line_off[0] = 0u;
    for (; story_bigbuf[i]; i++) {
        if (story_bigbuf[i] == '\n') {
            if (line + 1u >= G_STORY_MAX_LINES) break;
            story_line_off[++line] = (uint16_t)(i + 1u);
        }
    }
    story_nlines = (uint8_t)(line + 1u);
}

// ── Ring-buffer row write ─────────────────────────────────────────────────
// Uses set_bkg_tiles (bulk) — the only GBDK BKG function that correctly addresses
// all 32 hardware rows. gotoxy / setchar / set_bkg_tile_xy all clamp y at 18.
// IBM font at first_tile=0: tile index for char c = (uint8_t)(c - ' ').
static void write_row(uint8_t bkg_row, int16_t doc_line) {
    uint8_t tiles[20];
    uint8_t attrs[20];
    uint8_t i;
    const char *ln = 0;

    for (i = 0u; i < 20u; i++) { tiles[i] = 0u; attrs[i] = 0u; }

    if (doc_line >= (int16_t)LEAD_LINES
            && doc_line < (int16_t)LEAD_LINES + (int16_t)story_nlines) {
        uint8_t li = (uint8_t)((uint16_t)doc_line - (uint16_t)LEAD_LINES);
        ln = story_bigbuf + story_line_off[li];
        for (i = 0u; i < TEXT_COLS && ln[i] && ln[i] != '\n'; i++) {
            uint8_t col = (uint8_t)(TEXT_COL0 + i);
            tiles[col] = (uint8_t)(ln[i] - ' ');
            if (is_hi((uint16_t)((uint16_t)(ln - story_bigbuf) + i)))
                attrs[col] = (uint8_t)PAL_XP_UI;
        }
    }

    set_bkg_tiles(0u, bkg_row, 20u, 1u, tiles);
    VBK_REG = VBK_ATTRIBUTES;
    set_bkg_tiles(0u, bkg_row, 20u, 1u, attrs);
    VBK_REG = VBK_TILES;
}

// ── Fire particles ────────────────────────────────────────────────────────
static void fire_init(void) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) fire_ttl[i] = 0u;
}

static void fire_spawn(uint16_t fc) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        if (fire_ttl[i]) continue;
        fire_ox[i] = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X +
            8u + (uint8_t)((uint16_t)(DIV_REG + fc + (uint16_t)i * 17u) % 136u));
        fire_oy[i] = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + 132u + (uint8_t)(DIV_REG & 7u));
        fire_ttl[i] = (uint8_t)(28u + (uint8_t)(DIV_REG & 15u));
        return;
    }
}

static void fire_tick(uint16_t fc) {
    uint8_t ft = (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE);
    uint8_t fp = (uint8_t)(PAL_WALL_BG & 7u);
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        if (!fire_ttl[i]) continue;
        {
            uint8_t  y  = fire_oy[i];
            int16_t nx  = (int16_t)fire_ox[i];
            if (((fc + (uint16_t)i) & 3u) == 1u) nx += 2;
            else if (((fc + (uint16_t)i) & 3u) == 3u) nx -= 2;
            if (nx <   8) nx =   8;
            if (nx > 152) nx = 152;
            y = (uint8_t)((uint16_t)y - 1u);
            if (!((fc + i) & 1u)) y = (uint8_t)((uint16_t)y - 1u);
            fire_ttl[i]--;
            fire_oy[i] = y;
            fire_ox[i] = (uint8_t)nx;
            if (!fire_ttl[i] || y < 24u) {
                fire_ttl[i] = 0u;
                move_sprite((uint8_t)(FIRE_OAM_BASE + i), 0u, 0u);
            } else {
                set_sprite_tile((uint8_t)(FIRE_OAM_BASE + i), ft);
                set_sprite_prop((uint8_t)(FIRE_OAM_BASE + i), fp);
                move_sprite((uint8_t)(FIRE_OAM_BASE + i), (uint8_t)nx, y);
            }
        }
    }
}

static void fire_clear(void) {
    uint8_t i;
    for (i = 0u; i < G_STORY_FIRE_COUNT; i++) {
        fire_ttl[i] = 0u;
        move_sprite((uint8_t)(FIRE_OAM_BASE + i), 0u, 0u);
    }
}

// ── Entry point ───────────────────────────────────────────────────────────
void story_ui_run_before_first_floor(void) BANKED {
    char     cont[22];
    uint16_t mix = (uint16_t)(run_seed ^ ((uint16_t)player_class * 131u));
    uint8_t  pi  = (uint8_t)(mix % (uint16_t)CPREF_N);
    uint8_t  si  = (uint8_t)(((uint16_t)(mix >> 5) ^ (uint16_t)pi  * 13u
                               ^ (uint16_t)player_class * 3u) % (uint16_t)CSUF_N);
    uint8_t  s2  = (uint8_t)(((uint16_t)(mix >> 9) ^ (uint16_t)si  *  5u
                               ^ (uint16_t)pi * 7u ^ (uint16_t)player_class) % (uint16_t)CSUF2_N);
    const char *cls;
    int16_t  scroll_i, scroll_cap;
    uint8_t  pix_y, pix_sub, ring_next, do_ring;
    int16_t  doc_next;
    uint16_t fc;
    uint8_t  prev_j;

    switch (player_class) {
        case 1u: cls = "SCOUNDREL"; break;
        case 2u: cls = "WITCH";     break;
        case 3u: cls = "ZERKER";    break;
        default: cls = "KNIGHT";    break;
    }
    strcpy(cont, cpref[pi]);
    strcat(cont, csuf[si]);
    strcat(cont, csuf2[s2]);

    BANK_DBG("story");
    memset(explored_bits, 0, (size_t)ST_SCRATCH_END);
    build_text(cont, cls);
    word_wrap();
    build_lines();
    hi_n = 0u;
    hi_add("MARA"); hi_add("Crimson Keep"); hi_add("Mara's");
    hi_add("your realm"); hi_add(cont); hi_add(cls);

    scroll_cap = (int16_t)LEAD_LINES + (int16_t)story_nlines - 1;

    // ── LCD setup ─────────────────────────────────────────────────────────
    lcd_gameplay_active = 0u;
    lcd_suspend();
    wait_vbl_done();
    SCY_REG = 0u;
    SCX_REG = 0u;
    lcd_clear_display();
    set_bkg_palette(0u,         1u, pal_bkg);
    set_bkg_palette(PAL_XP_UI,  1u, pal_gold);
    set_sprite_palette(PAL_WALL_BG, 1u, pal_fire);
    font_color(3u, 0u);
    fire_init();
    SHOW_SPRITES;

    // ── Pre-fill ring buffer ───────────────────────────────────────────────
    // Rows 0..CRAWL_ROWS (19 rows) with doc_lines -(CRAWL_ROWS-1)..1 (all blank).
    // After loop: ring_next=19, doc_next=2.
    {
        uint8_t r;
        for (r = 0u; r <= CRAWL_ROWS; r++)
            write_row(r, (int16_t)r - (int16_t)(CRAWL_ROWS - 1u));
    }

    scroll_i  = 0;
    pix_y     = 0u;
    pix_sub   = 0u;
    ring_next = (uint8_t)(CRAWL_ROWS + 1u);
    doc_next  = 2;
    fc        = 0u;
    prev_j    = 0u;

    // ── Main loop ─────────────────────────────────────────────────────────
    for (;;) {
        uint8_t j = joypad();
        uint8_t e = (uint8_t)(j & (uint8_t)~prev_j);
        prev_j = j;
        if (e & (J_START | J_A)) break;

        do_ring = 0u;
        if (scroll_i < scroll_cap) {
            if (++pix_sub >= VBL_PER_PX) {
                pix_sub = 0u;
                if ((++pix_y & 7u) == 0u) {
                    scroll_i++;
                    do_ring = 1u;
                }
            }
        }

        wait_vbl_done();
        SCY_REG = pix_y;

        if (do_ring) {
            // Ring row (ring_next & 31) is always at screen pixel 144 — exactly one
            // pixel below the 144-px viewport when SCY_REG = pix_y. Safe to write.
            write_row(ring_next & 31u, doc_next);
            ring_next++;
            doc_next++;
        }

        fire_spawn(fc);
        fire_spawn((uint16_t)(fc + 7u));
        fire_spawn((uint16_t)(fc + 13u));
        fire_spawn((uint16_t)(fc + 19u));
        fire_tick(fc);
        fc++;
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    fire_clear();
    SCY_REG = 0u;
    wait_vbl_done();
    lcd_clear_display();
    load_palettes();
    font_color(3u, 0u);
    lcd_resume();
    g_prev_j = 0u;
    memset(explored_bits, 0, BITSET_BYTES);
    BANK_DBG("story_x");
}
