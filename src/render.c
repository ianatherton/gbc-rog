#include "render.h" // this module's public draw API
#include "map.h"    // tile_at, wall_palette_index, CAM_TX/TY via camera globals in defs
#include "enemy.h"  // enemy_at, defs, anim toggle
#include "ui.h"     // ui_draw_top_hud, ui_draw_bottom_rows
#include "lcd.h"    // line-8 ISR owns SCX/SCY during play; shake offsets
#include "wall_palettes.h" // wall_palette_table, NUM_WALL_PALETTES

static const palette_color_t pal_default[]  = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 0: floor text default
static const palette_color_t pal_green[]    = { RGB(0,0,0),  RGB(0,20,0),  RGB(0,26,0),   RGB(0,31,0)   }; // slot 1: some enemies
static const palette_color_t pal_player[]   = { RGB(0,0,0),  RGB(20,10,0), RGB(28,16,0),  RGB(31,24,0)  }; // slot PAL_PLAYER: class tile
static const palette_color_t pal_ladder[]   = { RGB(0,0,0),  RGB(12,7,3),  RGB(22,14,6),  RGB(30,22,10) }; // PAL_LADDER: wood / amber (not pit-blue)
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,4,4)   }; // slot 5: bar fill
static const palette_color_t pal_ui[]       = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 6: HUD text
static const palette_color_t pal_xp_ui[]    = { RGB(0,0,0),  RGB(18,14,0), RGB(26,22,4),  RGB(31,28,10) }; // slot 7: XP HUD (gold on black)

void apply_wall_palette(void) { // copy chosen ROM ramp into hardware BG slot PAL_WALL_BG
    uint8_t i = wall_palette_index;
    if (i >= NUM_WALL_PALETTES) i = 0;
    set_bkg_palette(PAL_WALL_BG, 1, wall_palette_table[i]);
}

static void draw_player_at(uint8_t sx, uint8_t sy) { // tileset hero + CGB palette
    uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + PLAYER_TILE_OFFSET);
    gotoxy(sx, sy);
    set_bkg_tiles(sx, sy, 1, 1, &vram);
    set_bkg_attribute_xy(sx, sy, PAL_PLAYER);
    VBK_REG = 0;
}

static void draw_enemy_at(uint8_t sx, uint8_t sy, const EnemyDef *def) { // tileset mob + palette from def
    uint8_t off  = enemy_anim_toggle ? def->tile_alt : def->tile;
    uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + off);
    gotoxy(sx, sy);
    set_bkg_tiles(sx, sy, 1, 1, &vram);
    set_bkg_attribute_xy(sx, sy, def->palette);
    VBK_REG = 0;
}

void load_palettes(void) { // slots 0–7 except walls: wall table entry 0 until apply_wall_palette runs
    set_bkg_palette(0, 1, pal_default);
    set_bkg_palette(1, 1, pal_green);
    set_bkg_palette(2, 1, pal_player);
    set_bkg_palette(PAL_WALL_BG, 1, wall_palette_table[0]); // matches wall_palette_index default 0
    set_bkg_palette(PAL_LADDER, 1, pal_ladder);
    set_bkg_palette(5, 1, pal_life_ui);
    set_bkg_palette(6, 1, pal_ui);
    set_bkg_palette(PAL_XP_UI, 1, pal_xp_ui);
}

static void draw_cell_at(uint8_t sx, uint8_t sy, uint8_t mx, uint8_t my,
                         uint8_t px, uint8_t py) { // write one logical map cell into ring coords (sx,sy)
    if (mx == px && my == py) { // player wins draw order
        draw_player_at(sx, sy);
        return;
    }
    {
        uint8_t idx = enemy_at(mx, my);
        if (idx != ENEMY_DEAD) {
            draw_enemy_at(sx, sy, &enemy_defs[enemy_type[idx]]);
            return;
        }
    }
    {
        uint8_t coff = corpse_sheet_at(mx, my);
        if (coff != 255) { // random L1–L5 floor deco (not font 'x')
            uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + coff);
            gotoxy(sx, sy);
            set_bkg_tiles(sx, sy, 1, 1, &vram);
            set_bkg_attribute_xy(sx, sy, PAL_CORPSE); // slot 0 — default ramp, matches floor text
            VBK_REG = 0;
            return;
        }
    }
    {
        uint8_t t    = tile_at(mx, my);     // wall / floor / pit
        uint8_t vram = tile_vram_index(t);  // 0 → font path
        gotoxy(sx, sy);
        if (vram) { set_bkg_tiles(sx, sy, 1, 1, &vram); } // push 8x8 tile index from RAM
        else      { setchar(tile_char(t)); }               // use built-in font tile
        set_bkg_attribute_xy(sx, sy, tile_palette(t));
        VBK_REG = 0;
    }
}

static void draw_ring_tile(uint8_t vx, uint8_t vy, uint8_t mx, uint8_t my,
                           uint8_t px, uint8_t py) { // vx = mx&31; vy = RING_BKG_VY_WORLD(my) — row 0 reserved for HUD
    draw_cell_at(vx, vy, mx, my, px, py);
}

void draw_cell(uint8_t mx, uint8_t my, uint8_t px, uint8_t py) { // cheap update if cell is on-screen
    if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) return; // off left/right of viewport
    if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) return; // off top/bottom
    draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my, px, py);
}

void draw_col_strip(uint8_t mx, uint8_t px, uint8_t py) { // refresh one world column at map x=mx
    uint8_t y, vx = (uint8_t)(mx & 31u); // ring X coordinate (constant for this strip)
    for (y = 0; y < GRID_H; y++) { // only dungeon rows — GRID_H not +1 so UI row 16 is not overwritten
        uint8_t my = (uint8_t)(CAM_TY + y); // world Y for this screen row
        uint8_t vy = RING_BKG_VY_WORLD(my);
        draw_ring_tile(vx, vy, mx, my, px, py);
    }
}

void draw_row_strip(uint8_t my, uint8_t px, uint8_t py) { // refresh one world row at map y=my
    uint8_t x, vy = RING_BKG_VY_WORLD(my);
    for (x = 0; x < GRID_W + 1u; x++) { // +1 covers sub-tile SCX scroll exposing partial column
        uint8_t mx = (uint8_t)(CAM_TX + x);
        uint8_t vx = (uint8_t)(mx & 31u);
        draw_ring_tile(vx, vy, mx, my, px, py);
    }
}

void draw_ui_rows(void) { // call after scroll: UI must be rewritten into new ring slots
    ui_draw_top_hud();
    ui_draw_bottom_rows();
}

void draw_screen(uint8_t px, uint8_t py) { // full repaint: dungeon, then HUD (row 0), then window UI
    uint8_t x, y;

    apply_wall_palette(); // keep slot PAL_WALL_BG in sync after floor load or A-button cycle

    for (y = 0; y < GRID_H; y++) { // dungeon first — vy = RING_BKG_VY_WORLD(my) keeps tilemap row 0 for HUD only
        for (x = 0; x < GRID_W; x++) {
            uint8_t mx = (uint8_t)(CAM_TX + x); // world map coordinate
            uint8_t my = (uint8_t)(CAM_TY + y);
            draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my, px, py); // player tile inside draw_cell_at
        }
    }

    ui_draw_top_hud(); // after dungeon so row 0 cols 0..GRID_W-1 stay FLR/bar (same VRAM row scrolls in viewport below line 8)
    ui_draw_bottom_rows();
    // SCX/SCY: VBL + LYC=8 ISR (lcd.c) — do not write here during gameplay
}

void draw_enemy_cells(uint8_t px, uint8_t py) { // fast path when only anim toggle changed
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (enemy_x[i] == ENEMY_DEAD) continue;
        {
            uint8_t mx = enemy_x[i], my = enemy_y[i];
            if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) continue; // cull off-screen
            if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) continue;
            if (mx == px && my == py) continue; // player cell handled last
            {
                uint8_t vx = (uint8_t)(mx & 31u), vy = RING_BKG_VY_WORLD(my);
                draw_enemy_at(vx, vy, &enemy_defs[enemy_type[i]]);
            }
        }
    }
    if (px >= CAM_TX && px < (uint8_t)(CAM_TX + GRID_W)
            && py >= CAM_TY && py < (uint8_t)(CAM_TY + GRID_H)) { // redraw hero on top if co-located in ring
        draw_player_at((uint8_t)(px & 31u), RING_BKG_VY_WORLD(py));
    }
    ui_draw_top_hud(); // enemies/player can paint vy==0 (e.g. world row 31) — repaint HUD band on top
}

void screen_shake(void) { // jitter applied only in line-8 ISR so HUD stays fixed
    uint8_t f;
    const int8_t off[] = { 2, -2, -1, 1, -2, 2, 1, -1 }; // small pixel offsets alternating
    for (f = 0; f < 8; f++) {
        lcd_shake_x = off[f];
        lcd_shake_y = off[(f + 2u) & 7u];
        wait_vbl_done();
    }
    lcd_shake_x = 0;
    lcd_shake_y = 0;
}