#pragma bank 2

#include "render.h"
#include "map.h"
#include "enemy.h" // corpse_sheet_at — draw_cell_terrain_only
#include "globals.h"
#include "ui.h"     // ui_draw_bottom_rows
#include "lcd.h"    // line-8 ISR owns SCX/SCY during play
#include "wall_palettes.h" // wall_palette_table, NUM_WALL_PALETTES
#include "biome.h"
#include "entity_sprites.h"
#include "class_palettes.h"
#include "perf.h"

BANKREF_EXTERN(entity_sprites_refresh_all)
BANKREF_EXTERN(entity_sprites_refresh_oam_only)

static const palette_color_t pal_default[]  = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 0: black field, corpses, blank floor; wall paper
static const palette_color_t pal_floor_deco[] = { RGB(0,0,0), RGB(5,5,5), RGB(11,11,11), RGB(17,17,17) }; // BKG PAL_FLOOR_BG: E3–E5 ground deco, dark grey on black
static const palette_color_t pal_green[]    = { RGB(0,0,0),  RGB(0,20,0),  RGB(0,26,0),   RGB(0,31,0)   }; // BKG+OCP1: serpent & adder only (snakes)
static const palette_color_t pal_ladder[]   = { RGB(0,0,0),  RGB(6,8,12),  RGB(31,16,2),  RGB(31,26,8) }; // BKG4 pit/ladder base with blue-grey shadow under warm highlights
static const palette_color_t pal_enemy_skeleton[] = { RGB(0,0,0), RGB(8,6,20),  RGB(16,10,26), RGB(22,16,31) }; // OCP4 violet / blue-purple bone
static const palette_color_t pal_enemy_rat[]      = { RGB(0,0,0), RGB(22,6,10), RGB(30,10,16), RGB(31,18,22) }; // OCP5 red–rose (BKG5 = life bar)
static const palette_color_t pal_enemy_goblin[]   = { RGB(0,0,0), RGB(18,4,18), RGB(26,8,24),  RGB(31,14,28) }; // OCP6 magenta–pink (BKG6 = HUD text)
static const palette_color_t pal_enemy_bat[]      = { RGB(0,0,0), RGB(0,14,18), RGB(4,22,24),  RGB(10,28,28) }; // OCP7 aqua–turquoise (BKG7 = XP gold)
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,31,31) }; // slot 5: hearts/bar — bright = white
static const palette_color_t pal_ui[]       = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 6: HUD text
static const palette_color_t pal_xp_ui[]    = { RGB(0,0,0),  RGB(23,9,0), RGB(30,17,0), RGB(31,27,1) }; // slot 7: saturated gold ramp — low B keeps hue; steps stay dark/mid/bright

void render_sprite_palette_player_default(void) NONBANKED { class_palettes_sprite_player_apply(); }
void render_sprite_palette_player_hurt(void) NONBANKED {
    // stack-local: data must not be in a banked ROM section (called from entity_sprites_vbl_tick in bank 3)
    palette_color_t pal[4] = { RGB(0,0,0), RGB(26,0,2), RGB(31,6,8), RGB(31,14,12) };
    set_sprite_palette(PAL_PLAYER, 1u, pal);
}

static uint8_t wall_palette_hw_iw = 255u, wall_palette_hw_ip = 255u; // 255 = out of band; invalidated in load_palettes
static uint8_t wall_palette_hw_biome = 255u; // tracks floor_biome so the field color follows hub<->dungeon transitions

void apply_wall_palette(void) { // PAL_WALL_BG bulk walls + PAL_PILLAR_BG column tiles (CGB BGP slots)
    uint8_t iw = wall_palette_index, ip = pillar_palette_index;
    palette_color_t wall_pal[4], pil_pal[4];
    // index-0 ("paper" behind wall/pillar art) matches the open field: dark green on the hub, else black
    palette_color_t bg0 = (floor_biome == BIOME_OVERWORLD) ? (palette_color_t)RGB(1, 6, 2) : pal_default[0];
    if (iw >= NUM_WALL_PALETTES) iw = 0;
    if (ip >= NUM_WALL_PALETTES) ip = 0;
    if (iw == wall_palette_hw_iw && ip == wall_palette_hw_ip && floor_biome == wall_palette_hw_biome) return; // skip CRAM when draw_screen repeats same ramp
    wall_palette_hw_iw = iw;
    wall_palette_hw_ip = ip;
    wall_palette_hw_biome = floor_biome;
    if (floor_biome == BIOME_OVERWORLD) {
        // PAL_WALL_BG = green pine ramp for interior c10 trees (idx0 bg / idx1 foliage / idx2 trunk);
        // idx0 matches the open-field color so trees sit seamlessly on the field.
        palette_color_t tree_pal[4] = {
            RGB(13, 26, 6), RGB(6, 18, 4), RGB(10, 7, 2), RGB(12, 26, 6), // idx0 == pal_overworld_field[0]
        };
        // PAL_PILLAR_BG = blue water ramp for the border F10 tile (idx3 bulk / idx2 wave specks).
        palette_color_t water_pal[4] = {
            RGB(2, 4, 10), RGB(4, 8, 16), RGB(9, 16, 26), RGB(2, 7, 16),
        };
        set_bkg_palette(PAL_WALL_BG,   1u, tree_pal);
        set_bkg_palette(PAL_PILLAR_BG, 1u, water_pal);
        return;
    }
    wall_pal[0] = bg0; // field color — seamless with blank / pit-adjacent open cells
    wall_pal[1] = wall_palette_table[iw][1];
    wall_pal[2] = wall_palette_table[iw][2];
    wall_pal[3] = wall_palette_table[iw][3];
    pil_pal[0] = bg0;
    pil_pal[1] = wall_palette_table[ip][1];
    pil_pal[2] = wall_palette_table[ip][2];
    pil_pal[3] = wall_palette_table[ip][3];
    set_bkg_palette(PAL_WALL_BG, 1, wall_pal);
    set_bkg_palette(PAL_PILLAR_BG, 1, pil_pal);
    // PAL_WALL_BG sprite slot is reserved for gameplay fire particle tint; keep wall colors BG-only.
}

void apply_field_palette(void) { // slot 0 (blank field) + floor-deco, per biome — restores after a menu blanks slot 0
    if (floor_biome == BIOME_OVERWORLD) {
        // keep identical to biome_overworld.c pal_overworld_field / pal_overworld_floor_deco
        palette_color_t f[4]  = { RGB(13, 26, 6), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31) };
        palette_color_t fd[4] = { RGB(13, 26, 6), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17) };
        set_bkg_palette(0, 1, f);
        set_bkg_palette(PAL_FLOOR_BG, 1, fd);
    } else {
        set_bkg_palette(0, 1, pal_default);
        set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco);
    }
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
            uint8_t pal;
            if (off == 255u) {
                setchar(' ');
                pal = 0u;
            } else {
                uint8_t vram = (uint8_t)(TILESET_VRAM_OFFSET + off);
                set_bkg_tiles(sx, sy, 1, 1, &vram);
                // Palette is deterministic from offset — no second brazier/item scan needed
                if      (off == TILE_STAIRS_UP_1)  pal = 0u;
                else if ((off & 15u) == 2u)        pal = PAL_LADDER;  // TILE_LIGHT_1..4 (col C rows 1-4: 2,18,34,50)
                else if (off == TILE_ITEM_4)        pal = PAL_XP_UI;
                else                               pal = PAL_FLOOR_BG;
            }
            set_bkg_attribute_xy(sx, sy, pal);
        } else if (t == TILE_WALL && floor_biome == BIOME_OVERWORLD) {
            // Hub walls split by position: outer band = blue water (F10/PAL_PILLAR_BG),
            // interior = green tree (c10/PAL_WALL_BG). Neighbor-count pillar logic is bypassed
            // so the blue pillar slot never leaks onto interior trees.
            uint8_t border = (mx < OVERWORLD_BORDER_BAND || my < OVERWORLD_BORDER_BAND
                           || mx >= (uint8_t)(active_map_w - OVERWORLD_BORDER_BAND)
                           || my >= (uint8_t)(active_map_h - OVERWORLD_BORDER_BAND));
            uint8_t vram = border ? TILE_OVERWORLD_WATER_VRAM : TILE_OVERWORLD_WALL_VRAM;
            set_bkg_tiles(sx, sy, 1, 1, &vram);
            set_bkg_attribute_xy(sx, sy, border ? PAL_PILLAR_BG : PAL_WALL_BG);
        } else if (t == TILE_WALL) {
            uint8_t n = wall_ortho_wall_count_xy(mx, my); // computed from floor_bits at draw-time to save WRAM
            uint8_t off = wall_tileset_index;
            if (n == 0u || n == 2u || n == 3u) {
                if (floor_biome == BIOME_CAVERN) {
                    uint8_t mix = (uint8_t)((uint8_t)(mx * 13u) ^ (uint8_t)(my * 29u) ^ run_seed);
                    off = (mix & 1u) ? TILE_COLUMN_7 : TILE_COLUMN_6;
                } else {
                    off = floor_column_off;
                }
            }
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
    wall_palette_hw_iw = wall_palette_hw_ip = 255u; // load stomps wall BGP to table[0] — next apply must push true indices
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

void draw_gameplay_overlays(uint8_t px, uint8_t py) { // used when look/belt/panel change but BKG ring matches last full draw
    ui_draw_bottom_rows();
    entity_sprites_refresh_all(px, py);
}

void draw_screen(uint8_t px, uint8_t py) { // full repaint: dungeon ring, then text panel + bottom HUD
    uint8_t perf_stamp = perf_stamp_now();
    uint8_t x, y;

    apply_wall_palette(); // keep slot PAL_WALL_BG in sync after floor load or A-button cycle

    for (y = 0; y < GRID_H + 1u; y++) {
        for (x = 0; x < GRID_W + 1u; x++) {
            uint8_t mx = (uint8_t)(CAM_TX + x);
            uint8_t my = (uint8_t)(CAM_TY + y);
            draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my);
        }
    }

    draw_gameplay_overlays(px, py);
    // SCX/SCY: VBL + LYC (lcd.c) — do not write here during gameplay
    perf_record(PERF_DRAW_SCREEN, perf_stamp_elapsed(&perf_stamp));
}

void draw_gameplay_overlays_profiled(uint8_t px, uint8_t py) { // overlay-only path metric; excludes full-screen redraws
    uint8_t perf_stamp = perf_stamp_now();
    draw_gameplay_overlays(px, py);
    perf_record(PERF_DRAW_OVERLAY, perf_stamp_elapsed(&perf_stamp));
}

void draw_gameplay_overlays_profiled_far(uint8_t px, uint8_t py) BANKED { // cross-bank entry for combat.c (bank 19)
    draw_gameplay_overlays_profiled(px, py);
}

void draw_enemy_cells(uint8_t px, uint8_t py) { // idle glyph flip: OAM only — BG + WIN unchanged vs last draw_screen
    entity_sprites_refresh_oam_only(px, py);
}

void draw_corpse_cells(void) { // redraw BG tiles for all corpses and dropped items — call after any non-melee kill
    uint8_t i;
    for (i = 0u; i < num_corpses; i++)
        draw_cell(corpse_x[i], corpse_y[i]);
    for (i = 0u; i < MAX_GROUND_ITEMS; i++)
        if (ground_item_kind[i] != ITEM_KIND_NONE)
            draw_cell(ground_item_x[i], ground_item_y[i]);
}

void draw_corpse_cells_far(void) BANKED { // cross-bank entry for combat.c (bank 19)
    draw_corpse_cells();
}

void draw_boss_reveal_cells_far(void) BANKED { // called from combat.c on Gorgon kill to reveal stairs + ladder
    uint8_t px, py;
    draw_cell(player_spawn_x, player_spawn_y);
    if (map_pit_position(&px, &py)) draw_cell(px, py);
}