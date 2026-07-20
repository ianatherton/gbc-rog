#pragma bank 29

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h"
#include "map.h"
#include "dungeon.h"
#include "lcd.h" // lcd_note_bkg0 — panic flash restores the live slot-0 ramp
#include <gb/cgb.h>
#include <gbdk/platform.h>

// Town interior (floors TOWN_FLOOR_BASE + 0..2): a large safe zone entered from the hub town's
// door, sized 59..96 square by its building count (5..20). The map border is a pine ring with a
// brick town wall just inside it; a sand road cross runs through the centre and out through gaps
// at N/S/E/W — each mouth is a LEAVE TOWN exit (town_exit_x/y), and the south mouth doubles as the
// spawn (drawn as the stairs-up glyph via the player_spawn path). Buildings are brick rects spread
// between the roads, each with a signpost by its door (SIGN_KIND_BUILDING), villagers inside the
// first few, and a roof that hides the interior until the player steps in: roof bits live in the
// fog buffer (SVBK2 0xD000, townroof_* in map.h — towns never read fog), town_state->inside_idx picks the
// one building drawn open. Fully lit — no fog, no braziers, no enemies, no items. Layout is
// deterministic from (run_seed, town_id).

BANKREF(biome_town_copy_defs)
void biome_town_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for FLOORKIND_TOWN anyway
}

// Same grass field / floor deco as the hub (biome_overworld.c), plus the sand accent for road
// cells. Set here at floor-gen time exactly like the hub's biome row does — apply_field_palette /
// apply_wall_palette only re-push after a menu stomps CRAM.
static const palette_color_t pal_town_field[] = {
    RGB(12, 23, 5), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
};
static const palette_color_t pal_town_floor_deco[] = {
    RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17),
};
static const palette_color_t pal_town_accent[] = {
    RGB(29, 24, 13), RGB(20, 15, 7), RGB(26, 21, 11), RGB(31, 29, 20),
};

BANKREF(biome_town_load_palettes)
void biome_town_load_palettes(void) {
    set_bkg_palette(0, 1u, pal_town_field);
    lcd_note_bkg0(pal_town_field);
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_town_floor_deco);
    set_bkg_palette(PAL_OW_ACCENT, 1u, pal_town_accent);
}

static uint8_t tg_hash(uint8_t town_id, uint8_t salt) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 3571u) ^ (uint16_t)((uint16_t)salt * 149u));
    h ^= (uint16_t)(h >> 7);
    h ^= (uint16_t)(h >> 3);
    return (uint8_t)h;
}

// Tiny local LCG for layout sampling — independent of rand() so a town re-entry regenerates the
// identical layout no matter what other floor-gen code consumed from the shared stream.
static uint16_t tg_rng;
static uint8_t tg_rand(void) {
    tg_rng = (uint16_t)(tg_rng * 25173u + 13849u);
    return (uint8_t)(tg_rng >> 8);
}

// One building rectangle: brick wall ring with a hollow floor interior.
static void tg_building_rect(uint8_t x0, uint8_t y0, uint8_t bw, uint8_t bh) {
    uint8_t x, y;
    for (y = y0; y < (uint8_t)(y0 + bh); y++)
        for (x = x0; x < (uint8_t)(x0 + bw); x++)
            BIT_CLR(floor_bits, TILE_IDX(x, y));
    for (y = (uint8_t)(y0 + 1u); y < (uint8_t)(y0 + bh - 1u); y++)
        for (x = (uint8_t)(x0 + 1u); x < (uint8_t)(x0 + bw - 1u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));
}

// 1 if a candidate rect (inflated so buildings keep a ≥2-cell gap) touches a placed building.
static uint8_t tg_rects_clash(uint8_t x0, uint8_t y0, uint8_t bw, uint8_t bh) {
    uint8_t i;
    for (i = 0u; i < town_state->count; i++) {
        const TownBuilding *b = &town_state->buildings[i];
        if ((uint8_t)(x0 + bw + 2u) <= b->x || (uint8_t)(b->x + b->w + 2u) <= x0) continue;
        if ((uint8_t)(y0 + bh + 2u) <= b->y || (uint8_t)(b->y + b->h + 2u) <= y0) continue;
        return 1u;
    }
    return 0u;
}

BANKREF(town_exit_at)
uint8_t town_exit_at(uint8_t x, uint8_t y) BANKED { // 1 if (x,y) is one of the 4 road mouths
    uint8_t i;
    for (i = 0u; i < 4u; i++)
        if (town_state->exit_x[i] == x && town_state->exit_y[i] == y) return 1u;
    return 0u;
}

BANKREF(town_roof_update)
uint8_t town_roof_update(uint8_t px, uint8_t py) BANKED { // 1 = inside-building state changed → repaint
    uint8_t i, inside = 255u;
    for (i = 0u; i < town_state->count; i++) {
        const TownBuilding *b = &town_state->buildings[i];
        if (px > b->x && px < (uint8_t)(b->x + b->w - 1u)
                && py > b->y && py < (uint8_t)(b->y + b->h - 1u)) { inside = i; break; }
    }
    if (inside == town_state->inside_idx) return 0u;
    town_state->inside_idx = inside;
    return 1u;
}

BANKREF(town_generate_interior)
void town_generate_interior(uint8_t town_id) BANKED {
    uint8_t x, y, i, n;
    const uint8_t target = (uint8_t)(5u + (uint8_t)(tg_hash(town_id, 0u) % 16u)); // 5..20 buildings
    uint8_t dims = (uint8_t)(44u + (uint8_t)(target * 3u)); // 59..104 → clamped below
    uint8_t w, h, cx, cy;
    if (dims > MAP_W) dims = MAP_W;
    active_map_w = dims; // storage is the full MAP_W×MAP_H bitset; this floor only uses dims²
    active_map_h = dims;
    w = dims; h = dims;
    cx = (uint8_t)(w >> 1);
    cy = (uint8_t)(h >> 1);
    tg_rng = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 40503u));

    // Open yard. Ring 0 (map edge) stays wall → drawn as the pine border; ring 1 stays wall →
    // the brick town wall (both in overworld_cell_render's town branch).
    for (y = 2u; y < (uint8_t)(h - 2u); y++)
        for (x = 2u; x < (uint8_t)(w - 2u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));

    // Main road cross. Carving the full centre column/row also opens the N/S/E/W mouths through
    // both border rings in the same pass; every carved cell is a road cell (mask read by render).
    road_clear_all(); // hub-only place_overworld_roads never ran for this floor — mask is stale
    for (y = 0u; y < h; y++) { BIT_SET(floor_bits, TILE_IDX(cx, y)); road_set(TILE_IDX(cx, y)); }
    for (x = 0u; x < w; x++) { BIT_SET(floor_bits, TILE_IDX(x, cy)); road_set(TILE_IDX(x, cy)); }
    town_state->exit_x[0] = cx;                town_state->exit_y[0] = 0u;                // N
    town_state->exit_x[1] = cx;                town_state->exit_y[1] = (uint8_t)(h - 1u); // S
    town_state->exit_x[2] = 0u;                town_state->exit_y[2] = cy;                // W
    town_state->exit_x[3] = (uint8_t)(w - 1u); town_state->exit_y[3] = cy;                // E
    player_spawn_x = cx;                                    // south mouth = spawn = stairs glyph;
    player_spawn_y = (uint8_t)(h - 1u);                     // the other 3 mouths exit via town_exit_at

    townroof_clear_all(); // mandatory: lighting_reset skips towns, so the buffer still holds the last dungeon's fog
    town_state->inside_idx = 255u;
    town_state->count = 0u;

    { // Rejection-sample the building rects; landing short of `target` on a crowded roll is fine.
        uint16_t tries;
        for (tries = 0u; tries < 250u && town_state->count < target; tries++) {
            uint8_t bw = (uint8_t)(5u + (uint8_t)(tg_rand() % 3u)); // 5..7
            uint8_t bh = (uint8_t)(5u + (uint8_t)(tg_rand() % 3u));
            uint8_t x0 = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(w - 6u - bw)));
            uint8_t y0 = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(h - 6u - bh)));
            if (cx >= (uint8_t)(x0 - 1u) && cx <= (uint8_t)(x0 + bw)) continue; // keep 1 clear cell off the
            if (cy >= (uint8_t)(y0 - 1u) && cy <= (uint8_t)(y0 + bh)) continue; // main road axes
            if (tg_rects_clash(x0, y0, bw, bh)) continue;
            tg_building_rect(x0, y0, bw, bh);
            town_state->buildings[town_state->count].x = x0;
            town_state->buildings[town_state->count].y = y0;
            town_state->buildings[town_state->count].w = bw;
            town_state->buildings[town_state->count].h = bh;
            town_state->count++;
        }
    }

    n = 0u; // features (generate_level zeroed ow_feature_count)
    ow_features[n].x = cx; ow_features[n].y = cy; // stone well at the road junction
    ow_features[n].type = OW_FEAT_FOUNTAIN; ow_features[n].aux = 0u;
    n++;

    for (i = 0u; i < town_state->count; i++) {
        const TownBuilding *b = &town_state->buildings[i];
        uint8_t bcx = (uint8_t)(b->x + (uint8_t)(b->w >> 1));
        uint8_t bcy = (uint8_t)(b->y + (uint8_t)(b->h >> 1));
        uint8_t adx = (cx > bcx) ? (uint8_t)(cx - bcx) : (uint8_t)(bcx - cx);
        uint8_t ady = (cy > bcy) ? (uint8_t)(cy - bcy) : (uint8_t)(bcy - cy);
        uint8_t door_x, door_y;
        int8_t sx = 0, sy = 0; // door's outward direction (toward the facing road axis)
        if (adx <= ady) { // column road is nearer → door on the E or W wall
            door_y = bcy;
            if (cx > bcx) { door_x = (uint8_t)(b->x + b->w - 1u); sx = 1; }
            else          { door_x = b->x;                        sx = -1; }
        } else {          // row road is nearer → door on the N or S wall
            door_x = bcx;
            if (cy > bcy) { door_y = (uint8_t)(b->y + b->h - 1u); sy = 1; }
            else          { door_y = b->y;                        sy = -1; }
        }
        BIT_SET(floor_bits, TILE_IDX(door_x, door_y)); // carve the door gap in the wall ring

        { // roof bits over the interior — the wall ring (and its door cell) stays visible
            uint8_t rx, ry;
            for (ry = (uint8_t)(b->y + 1u); ry < (uint8_t)(b->y + b->h - 1u); ry++)
                for (rx = (uint8_t)(b->x + 1u); rx < (uint8_t)(b->x + b->w - 1u); rx++)
                    townroof_set(TILE_IDX(rx, ry));
        }

        { // cosmetic side road: straight run from outside the door toward the facing axis; stops
          // at the first wall (another building/pine placed later can't cut it — pines skip roads)
          // or on meeting an existing road cell. Roads are visual only — movement ignores them.
            uint8_t rx = (uint8_t)((int8_t)door_x + sx);
            uint8_t ry = (uint8_t)((int8_t)door_y + sy);
            while (rx > 0u && ry > 0u && rx < (uint8_t)(w - 1u) && ry < (uint8_t)(h - 1u)) {
                uint16_t idx = TILE_IDX(rx, ry);
                if (!BIT_GET(floor_bits, idx)) break;
                if (road_bit(idx)) break;
                road_set(idx);
                rx = (uint8_t)((int8_t)rx + sx);
                ry = (uint8_t)((int8_t)ry + sy);
            }
        }

        if (n < MAX_OW_FEATURES) { // signpost in front of the door, shifted perpendicular so it
            uint8_t sgx = (uint8_t)((int8_t)door_x + sx + ((sy != 0) ? 1 : 0)); // doesn't sit in the walk path
            uint8_t sgy = (uint8_t)((int8_t)door_y + sy + ((sx != 0) ? 1 : 0));
            if (!BIT_GET(floor_bits, TILE_IDX(sgx, sgy))) { // shifted spot is a wall — fall back onto the path cell
                sgx = (uint8_t)((int8_t)door_x + sx);
                sgy = (uint8_t)((int8_t)door_y + sy);
            }
            ow_features[n].x = sgx; ow_features[n].y = sgy;
            ow_features[n].type = OW_FEAT_SIGNPOST;
            ow_features[n].aux = (uint8_t)(SIGN_KIND_BUILDING | (uint8_t)(tg_rand() & 7u));
            n++;
        }

        if (i < 8u && n < MAX_OW_FEATURES) { // villager inside the first few buildings
            ow_features[n].x = bcx; ow_features[n].y = bcy;
            ow_features[n].type = OW_FEAT_SIGNPOST;
            ow_features[n].aux = (uint8_t)(SIGN_KIND_NPC | i);
            n++;
        }
    }

    for (i = 0u; i < 40u && n < MAX_OW_FEATURES; i++) { // deco pines on open grass, off roads/aprons
        uint8_t px = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(w - 6u)));
        uint8_t py = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(h - 6u)));
        uint16_t idx = TILE_IDX(px, py);
        uint8_t k, clash = 0u;
        if (!BIT_GET(floor_bits, idx)) continue; // wall / building
        if (road_bit(idx)) continue;             // keep every road lane clear
        for (k = 0u; k < town_state->count; k++) { // 1-cell apron around each building (door approaches)
            const TownBuilding *b = &town_state->buildings[k];
            if (px >= (uint8_t)(b->x - 1u) && px <= (uint8_t)(b->x + b->w)
                    && py >= (uint8_t)(b->y - 1u) && py <= (uint8_t)(b->y + b->h)) { clash = 1u; break; }
        }
        if (clash) continue;
        for (k = 0u; k < n; k++)
            if (ow_features[k].x == px && ow_features[k].y == py) { clash = 1u; break; }
        if (clash) continue;
        BIT_CLR(floor_bits, idx); // pine is a blocking wall cell, drawn by the TREE feature
        ow_features[n].x = px; ow_features[n].y = py;
        ow_features[n].type = OW_FEAT_TREE; ow_features[n].aux = 0u;
        n++;
    }
    ow_feature_count = n;
}
