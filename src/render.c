#include "render.h"
#include "map.h"
#include "enemy.h"

/* ── CGB palette data ────────────────────────────────────────────────────── */
static const palette_color_t pal_default[]  = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) };
static const palette_color_t pal_green[]    = { RGB(0,0,0),  RGB(0,20,0),  RGB(0,26,0),   RGB(0,31,0)   };
static const palette_color_t pal_player[]   = { RGB(0,0,0),  RGB(20,10,0), RGB(28,16,0),  RGB(31,24,0)  };
static const palette_color_t pal_wall[]     = { RGB(0,0,0),  RGB(6,6,6),   RGB(10,10,10), RGB(16,16,16) };
static const palette_color_t pal_pit[]      = { RGB(0,0,0),  RGB(0,0,4),   RGB(0,0,8),    RGB(0,0,12)   };
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,4,4)   };
static const palette_color_t pal_ui[]       = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) };
static const palette_color_t pal_corpse[]   = { RGB(0,0,0),  RGB(0,4,0),   RGB(0,6,0),    RGB(0,10,0)   };

void load_palettes(void) {
    set_bkg_palette(0, 1, pal_default);
    set_bkg_palette(1, 1, pal_green);
    set_bkg_palette(2, 1, pal_player);
    set_bkg_palette(3, 1, pal_wall);
    set_bkg_palette(4, 1, pal_pit);
    set_bkg_palette(5, 1, pal_life_ui);
    set_bkg_palette(6, 1, pal_ui);
    set_bkg_palette(7, 1, pal_corpse);
}

/* ── Internal: draw cell (mx, my) at tilemap position (sx, sy) ───────────── */
static void draw_cell_at(uint8_t sx, uint8_t sy, uint8_t mx, uint8_t my,
                         uint8_t px, uint8_t py) {
    if (mx == px && my == py) {
        gotoxy(sx, sy);
        setchar('@');
        set_bkg_attribute_xy(sx, sy, 2);
        VBK_REG = 0;
        return;
    }
    {
        uint8_t idx = enemy_at(mx, my);
        if (idx != ENEMY_DEAD) {
            const EnemyDef *def = &enemy_defs[enemy_type[idx]];
            gotoxy(sx, sy);
            setchar(enemy_anim_toggle ? def->glyph_alt : def->glyph);
            set_bkg_attribute_xy(sx, sy, def->palette);
            VBK_REG = 0;
            return;
        }
    }
    if (corpse_at(mx, my)) {
        gotoxy(sx, sy);
        setchar('x');
        set_bkg_attribute_xy(sx, sy, PAL_CORPSE);
        VBK_REG = 0;
        return;
    }
    {
        uint8_t t    = tile_at(mx, my);
        uint8_t vram = tile_vram_index(t);
        gotoxy(sx, sy);
        if (vram) { set_bkg_tiles(sx, sy, 1, 1, &vram); }
        else      { setchar(tile_char(t)); }
        set_bkg_attribute_xy(sx, sy, tile_palette(t));
        VBK_REG = 0;
    }
}

/* Ring buffer: VRAM slot for map tile (mx, my) is (mx & 31, my & 31) ────── */
static void draw_ring_tile(uint8_t vx, uint8_t vy, uint8_t mx, uint8_t my,
                           uint8_t px, uint8_t py) {
    draw_cell_at(vx, vy, mx, my, px, py);
}

/* ── Single-cell redraw (terrain/corpse/player at that cell) ─────────────── */
void draw_cell(uint8_t mx, uint8_t my, uint8_t px, uint8_t py) {
    if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) return;
    if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) return;
    draw_ring_tile((uint8_t)(mx & 31u), (uint8_t)(my & 31u), mx, my, px, py);
}

/* ── One map column into ring (edge fill during scroll) ─────────────────── */
void draw_col_strip(uint8_t mx, uint8_t px, uint8_t py) {
    uint8_t y, vx = (uint8_t)(mx & 31u);
    /*
     * FIX: was (y < GRID_H + 1u), which wrote one extra row and stomped on
     * the first bottom-UI ring slot (CAM_TY + GRID_H) & 31 with dungeon
     * terrain, erasing the hotbar on every horizontal scroll step.
     */
    for (y = 0; y < GRID_H; y++) {
        uint8_t my = (uint8_t)(CAM_TY + y);
        uint8_t vy = (uint8_t)(my & 31u);
        draw_ring_tile(vx, vy, mx, my, px, py);
    }
}

/* ── One map row into ring (edge fill during scroll) ─────────────────────── */
void draw_row_strip(uint8_t my, uint8_t px, uint8_t py) {
    uint8_t x, vy = (uint8_t)(my & 31u);
    for (x = 0; x < GRID_W + 1u; x++) {
        uint8_t mx = (uint8_t)(CAM_TX + x);
        uint8_t vx = (uint8_t)(mx & 31u);
        draw_ring_tile(vx, vy, mx, my, px, py);
    }
}

/*
 * ── Ring-buffer UI helpers ────────────────────────────────────────────────
 *
 * The GBC background is a 32×32 tile ring.  The dungeon viewport occupies
 * ring rows  (CAM_TY)&31  through  (CAM_TY + GRID_H - 1)&31.
 *
 * We park the three UI bands in the ring slots that border the dungeon:
 *
 *   hud_vy  = (CAM_TY - 1) & 31  ← one slot ABOVE the dungeon
 *   bot1_vy = (CAM_TY + GRID_H)     & 31
 *   bot2_vy = (CAM_TY + GRID_H + 1) & 31
 *
 * Because SCY_REG is then set to (camera_py - 8) instead of camera_py,
 * the hardware scrolls the background up by one extra tile-row, which
 * brings hud_vy into screen row 0 and the dungeon into rows 1-15.
 *
 * hud_vx = CAM_TX & 31  keeps the HUD columns aligned with the dungeon
 * even after horizontal scrolling.
 */
#define UI_HUD_VX   ((uint8_t)(CAM_TX & 31u))
#define UI_HUD_VY   ((uint8_t)((CAM_TY + 31u) & 31u))
#define UI_BOT1_VY  ((uint8_t)((CAM_TY + GRID_H)      & 31u))
#define UI_BOT2_VY  ((uint8_t)((CAM_TY + GRID_H + 1u) & 31u))

/* Column helper: ring-wraps hud_vx + offset. */
#define HUD_COL(n)  ((uint8_t)((UI_HUD_VX + (uint8_t)(n)) & 31u))

/* ── Full-screen redraw (caller must wait_vbl_done first if LCD on) ─────── */
void draw_screen(uint8_t px, uint8_t py) {
    uint8_t x, y;

    /* Snapshot ring positions once (macros read globals, keep them stable). */
    uint8_t hvx  = UI_HUD_VX;
    uint8_t hvy  = UI_HUD_VY;

    /* ── Top HUD ──────────────────────────────────────────────────────── */
    /*
     * FIX: was gotoxy(0, UI_ROW_TOP=0) — wrote to tilemap row 0, a fixed
     * position that scrolled off-screen as soon as SCY_REG was set to
     * camera_py.  Now we write to the ring row that the hardware will
     * display at screen row 0 after the corrected SCY_REG below.
     */
    gotoxy(hvx, hvy);
    printf("FLR:%02d", floor_num);
    for (x = 0; x < 6; x++) {
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), hvy, PAL_UI);
        VBK_REG = 0;
    }

    gotoxy((uint8_t)((hvx + 6u) & 31u), hvy);
    setchar(' ');
    set_bkg_attribute_xy((uint8_t)((hvx + 6u) & 31u), hvy, PAL_UI);
    VBK_REG = 0;

    gotoxy((uint8_t)((hvx + 7u) & 31u), hvy);
    setchar('L');
    set_bkg_attribute_xy((uint8_t)((hvx + 7u) & 31u), hvy, PAL_LIFE_UI);
    VBK_REG = 0;

    gotoxy((uint8_t)((hvx + 8u) & 31u), hvy);
    setchar('[');
    set_bkg_attribute_xy((uint8_t)((hvx + 8u) & 31u), hvy, PAL_UI);
    VBK_REG = 0;

    {
        uint8_t k, pct = (uint8_t)((uint16_t)player_hp * 100u / PLAYER_HP_MAX);
        for (k = 0; k < LIFE_BAR_LEN; k++) {
            char c; uint8_t pal;
            if      (pct >= (uint8_t)(20u*(k+1u))) { c='='; pal=PAL_LIFE_UI; }
            else if (pct >= (uint8_t)(20u*k+10u))  { c='-'; pal=PAL_LIFE_UI; }
            else                                    { c='_'; pal=PAL_UI; }
            gotoxy((uint8_t)((hvx + 9u + k) & 31u), hvy);
            setchar(c);
            set_bkg_attribute_xy((uint8_t)((hvx + 9u + k) & 31u), hvy, pal);
            VBK_REG = 0;
        }
    }

    gotoxy((uint8_t)((hvx + 9u + LIFE_BAR_LEN) & 31u), hvy);
    setchar(']');
    set_bkg_attribute_xy((uint8_t)((hvx + 9u + LIFE_BAR_LEN) & 31u), hvy, PAL_UI);
    VBK_REG = 0;

    gotoxy((uint8_t)((hvx + 10u + LIFE_BAR_LEN) & 31u), hvy);
    printf("%3d%%", (uint16_t)player_hp * 100u / PLAYER_HP_MAX);
    for (x = 10u + LIFE_BAR_LEN; x < GRID_W; x++) {
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), hvy, PAL_UI);
        VBK_REG = 0;
    }

    /* ── Dungeon viewport into ring buffer ────────────────────────────── */
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            uint8_t mx = (uint8_t)(CAM_TX + x);
            uint8_t my = (uint8_t)(CAM_TY + y);
            draw_ring_tile((uint8_t)(mx & 31u), (uint8_t)(my & 31u), mx, my, px, py);
        }
    }
    /*
     * FIX: removed the redundant explicit '@' draw that used to follow the
     * loop.  draw_cell_at() already handles the player case first thing, so
     * the loop above already painted '@' in the correct ring slot.  The
     * second unconditional write caused a visible double-@ one frame after
     * any scroll step (the loop drew the player at the new ring slot while
     * the leftover write redrawed it at the old screen position too).
     */

    /* ── Bottom UI + scroll registers ────────────────────────────────── */
    draw_bottom_ui();

    /*
     * FIX: was SCY_REG = camera_py.
     * With SCY_REG = camera_py the hardware shows tilemap row CAM_TY at
     * screen row 0, which is the first *dungeon* row — the HUD row written
     * at (CAM_TY-1)&31 scrolls off the top.  Subtracting 8 pixels shifts
     * the viewport up by one tile so the HUD row appears at screen row 0
     * and the dungeon occupies rows 1-15 as intended.
     * The subtraction wraps safely in uint8_t (e.g. 0 - 8 = 248 = 0xF8,
     * which tells the GBC to show tilemap pixel row 248 = ring row 31 at
     * the top, exactly where we placed the HUD when CAM_TY == 0).
     */
    SCX_REG = (uint8_t)(camera_px & 0xFFu);
    SCY_REG = (uint8_t)((camera_py - 8u) & 0xFFu);
}

/* ── Bottom UI rows ──────────────────────────────────────────────────────── */
void draw_bottom_ui(void) {
    uint8_t x;
    /*
     * FIX: was gotoxy(0, UI_ROW_BOTTOM_1/2) — same fixed-row problem as
     * the top HUD.  Now computed from the current camera position so the
     * two UI rows always track just below the dungeon in the ring buffer.
     */
    uint8_t hvx  = UI_HUD_VX;
    uint8_t b1vy = UI_BOT1_VY;
    uint8_t b2vy = UI_BOT2_VY;

    gotoxy(hvx, b1vy);
    printf("ATK:-- DEF:-- SPD:--");
    for (x = 0; x < GRID_W; x++) {
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), b1vy, PAL_UI);
        VBK_REG = 0;
    }

    gotoxy(hvx, b2vy);
    printf("[  ][  ][  ][  ][  ]");
    for (x = 0; x < GRID_W; x++) {
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), b2vy, PAL_UI);
        VBK_REG = 0;
    }
}

/* ── Enemy animation redraw ──────────────────────────────────────────────── */
void draw_enemy_cells(uint8_t px, uint8_t py) {
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (enemy_x[i] == ENEMY_DEAD) continue;
        {
            uint8_t mx = enemy_x[i], my = enemy_y[i];
            if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) continue;
            if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) continue;
            if (mx == px && my == py) continue;
            {
                uint8_t vx = (uint8_t)(mx & 31u), vy = (uint8_t)(my & 31u);
                const EnemyDef *def = &enemy_defs[enemy_type[i]];
                gotoxy(vx, vy);
                setchar(enemy_anim_toggle ? def->glyph_alt : def->glyph);
                set_bkg_attribute_xy(vx, vy, def->palette);
                VBK_REG = 0;
            }
        }
    }
    /* Redraw player last — ensures '@' always wins over any enemy glyph
       that shares a ring slot (shouldn't happen, but belt-and-suspenders). */
    if (px >= CAM_TX && px < (uint8_t)(CAM_TX + GRID_W)
            && py >= CAM_TY && py < (uint8_t)(CAM_TY + GRID_H)) {
        uint8_t vx = (uint8_t)(px & 31u), vy = (uint8_t)(py & 31u);
        gotoxy(vx, vy);
        setchar('@');
        set_bkg_attribute_xy(vx, vy, 2);
        VBK_REG = 0;
    }
}

/* ── Screen shake ────────────────────────────────────────────────────────── */
void screen_shake(void) {
    uint8_t f;
    const int8_t off[] = { 2, -2, -1, 1, -2, 2, 1, -1 };
    /*
     * FIX 1: x shake was being applied as an absolute register value
     * (SCX_REG = off[f]) instead of relative to camera_px, which snapped
     * the view to near tile-column 0 during the shake.
     *
     * FIX 2: SCY_REG restore was camera_py instead of camera_py - 8,
     * causing the HUD to disappear for one frame after every shake.
     */
    uint8_t base_scx = (uint8_t)(camera_px & 0xFFu);
    uint8_t base_scy = (uint8_t)((camera_py - 8u) & 0xFFu);
    for (f = 0; f < 8; f++) {
        SCX_REG = (uint8_t)(base_scx + (uint8_t)off[f]);
        SCY_REG = (uint8_t)(base_scy + (uint8_t)off[(f + 2u) & 7u]);
        wait_vbl_done();
    }
    SCX_REG = base_scx;
    SCY_REG = base_scy;
}