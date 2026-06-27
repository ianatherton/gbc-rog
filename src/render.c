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
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,31,31) }; // slot 5: hearts/bar + all white HUD/UI text — bright = white
static const palette_color_t pal_xp_ui[]    = { RGB(0,0,0),  RGB(23,9,0), RGB(30,17,0), RGB(31,27,1) }; // OCP7 sprite gold ramp — low B keeps hue; steps stay dark/mid/bright

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
        // PAL_OW_FOLIAGE (slot 6, freed from UI) = green pine ramp for interior c10 trees
        // (idx0 bg / idx1 foliage / idx2 trunk); idx0 matches the open-field color so trees
        // sit seamlessly on the field. Moving trees here frees PAL_WALL_BG (3) for future hub deco.
        palette_color_t tree_pal[4] = {
            RGB(12, 23, 5), RGB(6, 18, 4), RGB(10, 7, 2), RGB(12, 26, 6), // idx0 == pal_overworld_field[0]
        };
        // PAL_PILLAR_BG = water ramp for open sea (F10: idx3 bulk / idx2 wave specks) and the coast
        // tiles. idx0 is the green field color: open water never uses idx0, but the coast tiles use it
        // for their land bulk, so the shore reads as green land (idx0) with a blue water edge (idx2/3).
        palette_color_t water_pal[4] = {
            RGB(12, 23, 5), RGB(4, 8, 16), RGB(9, 16, 26), RGB(2, 7, 16),
        };
        // PAL_WALL_BG (freed in the hub) = icy snow ramp for the NW snowfield region: idx0 snow base
        // (open snow), idx1 blue shadow grain, idx2 mid, idx3 white. Frosted trees reuse it too.
        palette_color_t snow_pal[4] = {
            RGB(24, 27, 31), RGB(15, 19, 27), RGB(20, 24, 30), RGB(31, 31, 31),
        };
        set_bkg_palette(PAL_OW_FOLIAGE, 1u, tree_pal);
        set_bkg_palette(PAL_PILLAR_BG,  1u, water_pal);
        set_bkg_palette(PAL_WALL_BG,    1u, snow_pal);
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
        palette_color_t f[4]  = { RGB(12, 23, 5), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31) };
        palette_color_t fd[4] = { RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17) };
        set_bkg_palette(0, 1, f);
        set_bkg_palette(PAL_FLOOR_BG, 1, fd);
    } else {
        set_bkg_palette(0, 1, pal_default);
        set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco);
    }
    // A menu may have stomped PAL_WALL_BG/PAL_PILLAR_BG (e.g. inventory restores the metal ramp into
    // slot 3). Invalidate the cache so the next apply_wall_palette re-pushes the floor's true ramps —
    // dungeon walls/pillars, or hub water/snow/foliage.
    wall_palette_hw_iw = wall_palette_hw_ip = wall_palette_hw_biome = 255u;
}

// Hub cells are classified by one banked call, overworld_cell_render (bank 22, biome_overworld.c):
// it folds the water/tree split, coast lookup, and desert/snow region test into a single trampoline
// (was 3–4 per cell). Its arithmetic stays out of the pinned, nearly-full bank-2 render code, and it
// is only reached on the hub, so dungeons pay no trampoline cost.

// Per-strip scratch (≥ GRID_W+1 ≥ GRID_H+1): classify_cell fills these, then one bulk set_bkg_tiles +
// one set_bkg_attributes blits the whole strip — far fewer GBDK/VBK calls than a write per cell. The
// blit helpers (render_blit_strip_col/row) live in bank 22 to relieve the near-full render bank 2; these
// buffers are global so that bank reaches them.
uint8_t render_strip_tiles[GRID_W + 1u];
uint8_t render_strip_attrs[GRID_W + 1u];

// Resolve a world cell to its BG tile index (0 = blank/space, font tile 0) + CGB attribute, WITHOUT
// touching VRAM. Branch-for-branch identical to the old per-cell draw — the caller blits the result.
static uint8_t classify_cell(uint8_t mx, uint8_t my, uint8_t *attr_out) {
    if (!lighting_is_revealed(mx, my)) { *attr_out = 0u; return 0u; } // fog: blank field, pal 0
    {
        uint8_t coff = corpse_sheet_at(mx, my);
        if (coff != 255) { *attr_out = PAL_CORPSE; return (uint8_t)(TILESET_VRAM_OFFSET + coff); }
    }
    {
        uint8_t t = tile_at(mx, my);
        uint8_t snow = 0u, desert = 0u;
        if (floor_biome == BIOME_OVERWORLD) {
            // One banked call resolves the hub cell: water/tree/coast/prefab → a finished VRAM tile;
            // interior land returns 0 + a region code that drives the floor-deco palette below. OW wall
            // cells always resolve here, so the dungeon wall branch never sees them.
            uint8_t ow_pal, region;
            uint8_t ow_vram = overworld_cell_render(mx, my, t, &ow_pal, &region);
            if (ow_vram) { *attr_out = ow_pal; return ow_vram; }
            snow   = (region == OW_REGION_SNOW);
            desert = (region == OW_REGION_DESERT);
        }
        uint8_t ground_pal = snow ? PAL_WALL_BG : (desert ? PAL_OW_ACCENT : PAL_FLOOR_BG); // hub ground by region
        if (t == TILE_FLOOR || (t == TILE_PIT && tile_vram_index(t) == 0u)) { // hidden boss/miniboss pit renders as plain floor — no ASCII fallback tell
            uint8_t off = floor_tile_sheet_offset(mx, my);
            if (off == 255u) { *attr_out = snow ? PAL_WALL_BG : (desert ? PAL_OW_ACCENT : 0u); return 0u; } // blank snow/sand vs green field
            // Palette is deterministic from offset — no second brazier/item scan needed
            if      (off == TILE_STAIRS_UP_1)  *attr_out = 0u;
            else if ((off & 15u) == 2u)        *attr_out = PAL_LADDER;       // TILE_LIGHT_1..4 (col C rows 1-4: 2,18,34,50)
            else if (off == TILE_ITEM_4)       *attr_out = PAL_ITEM_GOLD_BG; // true orange-gold (slot 6), not the fire ramp
            else if (off == TILE_TEST)         *attr_out = PAL_LADDER;       // single non-overworld floor deco tile, torch-tinted
            else                               *attr_out = ground_pal;       // snow / sand / grey ground deco by region
            return (uint8_t)(TILESET_VRAM_OFFSET + off);
        } else if (t == TILE_WALL) {
            uint8_t n = wall_ortho_wall_count_xy(mx, my); // computed from floor_bits at draw-time to save WRAM
            uint8_t off = wall_tileset_index;
            uint8_t pillar = (n == 0u || n == 2u || n == 3u);
            if (pillar) {
                if (floor_biome == BIOME_CAVERN) {
                    uint8_t mix = (uint8_t)((uint8_t)(mx * 13u) ^ (uint8_t)(my * 29u) ^ run_seed);
                    off = (mix & 1u) ? TILE_COLUMN_7 : TILE_COLUMN_6;
                } else {
                    off = floor_column_off;
                }
            }
            *attr_out = pillar ? PAL_PILLAR_BG : PAL_WALL_BG;
            return (uint8_t)(TILESET_VRAM_OFFSET + off);
        } else {
            uint8_t vram = tile_vram_index(t);
            *attr_out = tile_palette(t);
            return vram ? vram : (uint8_t)((uint8_t)tile_char(t) - 32u); // ASCII fallback → font tile (space = 0)
        }
    }
}

static void draw_cell_terrain_only(uint8_t sx, uint8_t sy, uint8_t mx, uint8_t my) { // single cell: classify + blit
    uint8_t attr, t = classify_cell(mx, my, &attr);
    set_bkg_tiles(sx, sy, 1u, 1u, &t);
    set_bkg_attributes(sx, sy, 1u, 1u, &attr); // restores VBK to VBK_TILES
    VBK_REG = 0;
}

BANKREF(load_palettes)
void load_palettes(void) BANKED { // slots 0–7 except walls: wall table entry 0 until apply_wall_palette runs
    set_bkg_palette(0, 1, pal_default);
    set_bkg_palette(PAL_PILLAR_BG, 1, wall_palette_table[0]); // slot 1 = pillars in gameplay (was unused BKG green)
    set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco); // ground deco tile only; blank floor uses slot 0
    set_bkg_palette(PAL_WALL_BG, 1, wall_palette_table[0]); // matches wall_palette_index default 0
    set_bkg_palette(PAL_LADDER, 1, pal_ladder); // static shared ladder+brazier fire tone — also BKG gold (PAL_XP_UI_BG)
    set_bkg_palette(PAL_LIFE_UI, 1, pal_life_ui); // hearts + all white HUD/UI text (index 3 = white)
    set_bkg_palette(PAL_ITEM_GOLD_BG, 1, pal_xp_ui); // slot 6: true orange-gold for dungeon ground items
    // (the hub overwrites slot 6 with foliage via apply_wall_palette; slot 7 stays free for biome use)
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

void draw_col_strip(uint8_t mx) { // refresh one world column at map x=mx — classify into buffers, one bulk blit
    uint8_t y, n = (uint8_t)(GRID_H + 1u);
    for (y = 0u; y < n; y++) {
        uint8_t my = (uint8_t)(CAM_TY + y);
        render_strip_tiles[y] = classify_cell(mx, my, &render_strip_attrs[y]);
    }
    render_blit_strip_col((uint8_t)(mx & 31u), (uint8_t)(CAM_TY & 31u), n);
}

void draw_row_strip(uint8_t my) { // refresh one world row at map y=my
    uint8_t x, n = (uint8_t)(GRID_W + 1u);
    for (x = 0u; x < n; x++) {
        uint8_t mx = (uint8_t)(CAM_TX + x);
        render_strip_tiles[x] = classify_cell(mx, my, &render_strip_attrs[x]);
    }
    render_blit_strip_row((uint8_t)(my & 31u), (uint8_t)(CAM_TX & 31u), n);
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
    uint8_t x;

    apply_wall_palette(); // keep slot PAL_WALL_BG in sync after floor load or A-button cycle

    for (x = 0; x < GRID_W + 1u; x++) // column-major: each draw_col_strip classifies a column then bulk-blits it
        draw_col_strip((uint8_t)(CAM_TX + x));

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

