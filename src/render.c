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

// CGB palette ramps + apply_wall_palette / apply_field_palette / load_palettes live in
// render_palettes.c (bank 22) to relieve the near-full render bank 2. The two player sprite-palette
// helpers below stay here NONBANKED: they use no bank-2 consts and hurt() is called from the VBL ISR.
void render_sprite_palette_player_default(void) NONBANKED { class_palettes_sprite_player_apply(); }
void render_sprite_palette_player_hurt(void) NONBANKED {
    // stack-local: data must not be in a banked ROM section (called from entity_sprites_vbl_tick in bank 3)
    palette_color_t pal[4] = { RGB(0,0,0), RGB(26,0,2), RGB(31,6,8), RGB(31,14,12) };
    set_sprite_palette(PAL_PLAYER, 1u, pal);
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
        // Gravestones are low priority: yield to a stair/ladder, torch, or item that shares the cell
        // (e.g. an item dropped onto an old corpse). The feature scan only runs where a corpse exists.
        if (coff != 255 && !map_tile_blocks_gravestone(mx, my)) {
            *attr_out = PAL_CORPSE; return (uint8_t)(TILESET_VRAM_OFFSET + coff);
        }
    }
    {
        uint8_t t = tile_at(mx, my);
        uint8_t snow = 0u, desert = 0u;
        if (floor_biome == BIOME_OVERWORLD || floor_biome == BIOME_TOWN) { // town: feature overlay only, terrain falls through
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
            else if (off == TILE_ITEM_4)       *attr_out = (floor_biome == BIOME_TOWN || floor_biome == BIOME_OVERWORLD) ? PAL_LADDER : PAL_ITEM_GOLD_BG; // slot 6 is hijacked for tree-green in grass biomes (apply_wall_palette) — ride the ladder fire ramp instead, same trick as PAL_XP_UI_BG
            else if (off == TILE_TEST)         *attr_out = desert ? PAL_OW_ACCENT : PAL_LADDER; // dungeon floor deco torch-tinted; hub roads (region desert) sand-tinted
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

static void draw_ring_tile(uint8_t vx, uint8_t vy, uint8_t mx, uint8_t my) { // enemies are sprites; BG is always terrain-only
    draw_cell_terrain_only(vx, vy, mx, my);
}

void draw_cell(uint8_t mx, uint8_t my) { // cheap update if cell is on-screen
    if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)) return;
    if (my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)) return;
    draw_ring_tile((uint8_t)(mx & 31u), RING_BKG_VY_WORLD(my), mx, my);
}

// Classify (CPU only, no VRAM) one column/row into the render_strip_* buffers. cam_ty/cam_tx are the
// camera tile origin the strip belongs to, passed explicitly so camera_scroll_to can PRE-classify the
// about-to-be-revealed strip at the target position before the glide (see camera.c). Blitting the
// filled buffer is a separate step (render_blit_strip_col/row) done in the VBlank-safe window.
void classify_col_strip(uint8_t mx, uint8_t cam_ty) {
    uint8_t perf_stamp = perf_stamp_now();
    if (floor_biome == BIOME_OVERWORLD) { // one banked entry classifies the whole strip (bank 22)
        overworld_classify_col_strip(mx, cam_ty);
    } else {
        uint8_t y, n = (uint8_t)(GRID_H + 1u);
        for (y = 0u; y < n; y++)
            render_strip_tiles[y] = classify_cell(mx, (uint8_t)(cam_ty + y), &render_strip_attrs[y]);
    }
    perf_record(PERF_CLASSIFY, perf_stamp_elapsed(&perf_stamp));
}

void classify_row_strip(uint8_t my, uint8_t cam_tx) {
    uint8_t perf_stamp = perf_stamp_now();
    if (floor_biome == BIOME_OVERWORLD) {
        overworld_classify_row_strip(my, cam_tx);
    } else {
        uint8_t x, n = (uint8_t)(GRID_W + 1u);
        for (x = 0u; x < n; x++)
            render_strip_tiles[x] = classify_cell((uint8_t)(cam_tx + x), my, &render_strip_attrs[x]);
    }
    perf_record(PERF_CLASSIFY, perf_stamp_elapsed(&perf_stamp));
}

void draw_col_strip(uint8_t mx) { // classify one world column at map x=mx, then one bulk blit
    classify_col_strip(mx, CAM_TY);
    render_blit_strip_col((uint8_t)(mx & 31u), (uint8_t)(CAM_TY & 31u), (uint8_t)(GRID_H + 1u));
}

void draw_row_strip(uint8_t my) { // classify one world row at map y=my, then one bulk blit
    classify_row_strip(my, CAM_TX);
    render_blit_strip_row((uint8_t)(my & 31u), (uint8_t)(CAM_TX & 31u), (uint8_t)(GRID_W + 1u));
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

