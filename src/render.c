#pragma bank 2

#include "render.h"
#include "map.h"
#include "enemy.h"
#include "globals.h"
#include "ui.h"     // ui_draw_bottom_rows
#include "lcd.h"    // line-8 ISR owns SCX/SCY during play
#include "wall_palettes.h" // wall_palette_table, NUM_WALL_PALETTES
#include "entity_sprites.h"
#include "class_palettes.h"

static const palette_color_t pal_default[]  = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 0: black field, corpses, blank floor; wall paper
static const palette_color_t pal_floor_deco[] = { RGB(0,0,0), RGB(5,5,5), RGB(11,11,11), RGB(17,17,17) }; // BKG PAL_FLOOR_BG: E3–E5 ground deco, dark grey on black
static const palette_color_t pal_green[]    = { RGB(0,0,0),  RGB(0,20,0),  RGB(0,26,0),   RGB(0,31,0)   }; // BKG+OCP1: serpent & adder only (snakes)
static const palette_color_t pal_player_hurt_flash[] = { RGB(0,0,0), RGB(26,0,2), RGB(31,6,8), RGB(31,14,12) }; // OCP PAL_PLAYER — brief damage tint (class ramp restored after)
static const palette_color_t pal_ladder[]   = { RGB(0,0,0),  RGB(6,8,12),  RGB(31,16,2),  RGB(31,26,8) }; // BKG4 pit/ladder base with blue-grey shadow under warm highlights
static const palette_color_t pal_enemy_skeleton[] = { RGB(0,0,0), RGB(8,6,20),  RGB(16,10,26), RGB(22,16,31) }; // OCP4 violet / blue-purple bone
static const palette_color_t pal_enemy_rat[]      = { RGB(0,0,0), RGB(22,6,10), RGB(30,10,16), RGB(31,18,22) }; // OCP5 red–rose (BKG5 = life bar)
static const palette_color_t pal_enemy_goblin[]   = { RGB(0,0,0), RGB(18,4,18), RGB(26,8,24),  RGB(31,14,28) }; // OCP6 magenta–pink (BKG6 = HUD text)
static const palette_color_t pal_enemy_bat[]      = { RGB(0,0,0), RGB(0,14,18), RGB(4,22,24),  RGB(10,28,28) }; // OCP7 aqua–turquoise (BKG7 = XP gold)
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,4,4)   }; // slot 5: bar fill
static const palette_color_t pal_ui[]       = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 6: HUD text
static const palette_color_t pal_xp_ui[]    = { RGB(0,0,0),  RGB(18,14,0), RGB(26,22,4),  RGB(31,28,10) }; // slot 7: XP HUD (gold on black)

void render_sprite_palette_player_default(void) NONBANKED { class_palettes_sprite_player_apply(); }
void render_sprite_palette_player_hurt(void) NONBANKED { set_sprite_palette(PAL_PLAYER, 1, pal_player_hurt_flash); }

void apply_wall_palette(void) { // PAL_WALL_BG bulk walls + PAL_PILLAR_BG column tiles (CGB BGP slots)
    uint8_t iw = wall_palette_index, ip = pillar_palette_index;
    palette_color_t wall_pal[4], pil_pal[4];
    if (iw >= NUM_WALL_PALETTES) iw = 0;
    if (ip >= NUM_WALL_PALETTES) ip = 0;
    wall_pal[0] = pal_default[0]; // black field — seamless with blank / pit-adjacent open cells
    wall_pal[1] = wall_palette_table[iw][1];
    wall_pal[2] = wall_palette_table[iw][2];
    wall_pal[3] = wall_palette_table[iw][3];
    pil_pal[0] = pal_default[0];
    pil_pal[1] = wall_palette_table[ip][1];
    pil_pal[2] = wall_palette_table[ip][2];
    pil_pal[3] = wall_palette_table[ip][3];
    set_bkg_palette(PAL_WALL_BG, 1, wall_pal);
    set_bkg_palette(PAL_PILLAR_BG, 1, pil_pal);
    // PAL_WALL_BG sprite slot is reserved for gameplay fire particle tint; keep wall colors BG-only.
}

static void draw_cell_terrain_only(uint8_t sx, uint8_t sy, uint8_t mx, uint8_t my) { // floor/wall/pit/corpse; no actors on BG
    if (!lighting_is_revealed(mx, my)) {
        gotoxy(sx, sy);
        setchar(' ');
        set_bkg_attribute_xy(sx, sy, 0u);
        VBK_REG = 0;
        return;
    }
    {
        uint8_t coff = corpse_sheet_at(mx, my);
        if (coff != 255) {
            uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + coff);
            gotoxy(sx, sy);
            set_bkg_tiles(sx, sy, 1, 1, &vram);
            set_bkg_attribute_xy(sx, sy, PAL_CORPSE);
            VBK_REG = 0;
            return;
        }
    }
    {
        uint8_t t = tile_at(mx, my);
        gotoxy(sx, sy);
        if (t == TILE_FLOOR) {
            uint8_t off = floor_tile_sheet_offset(mx, my);
            if (off == 255) {
                setchar(' ');
            } else {
                uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + off);
                set_bkg_tiles(sx, sy, 1, 1, &vram);
            }
            set_bkg_attribute_xy(sx, sy, floor_tile_palette_xy(mx, my));
        } else if (t == TILE_WALL) {
            uint8_t n = wall_ortho_wall_count_xy(mx, my); // computed from floor_bits at draw-time to save WRAM
            uint8_t off = (n == 0u || n == 2u || n == 3u) ? floor_column_off : wall_tileset_index;
            uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + off);
            uint8_t attr = (n == 0u || n == 2u || n == 3u) ? PAL_PILLAR_BG : PAL_WALL_BG;
            set_bkg_tiles(sx, sy, 1, 1, &vram);
            set_bkg_attribute_xy(sx, sy, attr);
        } else {
            uint8_t vram = tile_vram_index(t);
            if (vram) { set_bkg_tiles(sx, sy, 1, 1, &vram); }
            else      { setchar(tile_char(t)); }
            set_bkg_attribute_xy(sx, sy, tile_palette(t));
        }
        VBK_REG = 0;
    }
}

BANKREF(load_palettes)
void load_palettes(void) BANKED { // slots 0–7 except walls: wall table entry 0 until apply_wall_palette runs
    set_bkg_palette(0, 1, pal_default);
    set_bkg_palette(PAL_PILLAR_BG, 1, wall_palette_table[0]); // slot 1 = pillars in gameplay (was unused BKG green)
    set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco); // ground deco tile only; blank floor uses slot 0
    set_bkg_palette(PAL_WALL_BG, 1, wall_palette_table[0]); // matches wall_palette_index default 0
    set_bkg_palette(PAL_LADDER, 1, pal_ladder); // static shared ladder+brazier fire tone
    set_bkg_palette(5, 1, pal_life_ui);
    set_bkg_palette(6, 1, pal_ui);
    set_bkg_palette(PAL_XP_UI, 1, pal_xp_ui);
    set_sprite_palette(0, 1, pal_default);
    set_sprite_palette(1, 1, pal_green);
    class_palettes_sprite_player_apply();
    set_sprite_palette(PAL_WALL_BG, 1, pal_ladder); // gameplay fire particle ramp uses shared ladder fire tone
    set_sprite_palette(PAL_LADDER, 1, pal_enemy_skeleton);
    set_sprite_palette(PAL_ENEMY_RAT, 1, pal_enemy_rat);
    set_sprite_palette(PAL_ENEMY_GOBLIN, 1, pal_enemy_goblin);
    set_sprite_palette(PAL_XP_UI, 1, pal_xp_ui); // OBJ7 — belt M5 arrow + bats share XP gold ramp (PAL_ENEMY_BAT / PAL_XP_UI both 7)
}

static void draw_ring_tile(uint8_t vx, uint8_t vy, uint8_t mx, uint8_t my) { // enemies are sprites; BG is always terrain-only
    draw_cell_terrain_only(vx, vy, mx, my);
}

void draw_cell(uint8_t mx, uint8_t my) { // cheap update if cell is on-screen
    if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) return;
    if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) return;
    draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my);
}

void draw_col_strip(uint8_t mx) { // refresh one world column at map x=mx
    uint8_t y, vx = (uint8_t)(mx & 31u);
    for (y = 0; y < GRID_H + 1u; y++) {
        uint8_t my = (uint8_t)(CAM_TY + y);
        draw_ring_tile(vx, RING_BKG_VY_WORLD(my), mx, my);
    }
}

void draw_row_strip(uint8_t my) { // refresh one world row at map y=my
    uint8_t x, vy = RING_BKG_VY_WORLD(my);
    for (x = 0; x < GRID_W + 1u; x++) {
        uint8_t mx = (uint8_t)(CAM_TX + x);
        draw_ring_tile((uint8_t)(mx & 31u), vy, mx, my);
    }
}

void draw_ui_rows(void) { // 3-line panel + bottom HUD row after BKG ring updates
    ui_draw_bottom_rows();
}

void draw_screen(uint8_t px, uint8_t py) { // full repaint: dungeon ring, then text panel + bottom HUD
    uint8_t x, y;

    apply_wall_palette(); // keep slot PAL_WALL_BG in sync after floor load or A-button cycle

    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            uint8_t mx = (uint8_t)(CAM_TX + x);
            uint8_t my = (uint8_t)(CAM_TY + y);
            draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my);
        }
    }

    ui_draw_bottom_rows();
    entity_sprites_refresh_all(px, py);
    // SCX/SCY: VBL + LYC (lcd.c) — do not write here during gameplay
}

void draw_enemy_cells(uint8_t px, uint8_t py) { // fast path when only anim toggle changed
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (!enemy_alive[i]) continue;
        {
            uint8_t mx = enemy_x[i], my = enemy_y[i];
            if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) continue; // cull off-screen
            if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) continue;
            {
                uint8_t vx = (uint8_t)(mx & 31u), vy = RING_BKG_VY_WORLD(my);
                draw_cell_terrain_only(vx, vy, mx, my); // keep BG correct under sprite
            }
        }
    }
    if (px >= CAM_TX && px < (uint8_t)(CAM_TX + GRID_W)
            && py >= CAM_TY && py < (uint8_t)(CAM_TY + GRID_H)) {
        draw_cell_terrain_only((uint8_t)(px & 31u), RING_BKG_VY_WORLD(py), px, py);
    }
    ui_draw_bottom_rows();
    entity_sprites_refresh_all(px, py);
}