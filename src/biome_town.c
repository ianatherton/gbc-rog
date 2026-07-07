#pragma bank 29

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h"
#include "map.h"
#include "dungeon.h"
#include <gb/cgb.h>
#include <gbdk/platform.h>

// Town interior (floors TOWN_FLOOR_BASE + 0..2): a 20×20 safe zone entered from the hub town's
// door. Looks like a piece of the overworld: grass field (hub palettes via apply_field/wall_palette
// town branches), sand road cross, deco pines, and brick buildings (dungeon wall art) housing the
// NPCs. Fully lit — no fog, no braziers (lighting.c treats BIOME_TOWN like the hub). No enemies,
// no items. Layout is deterministic from (run_seed, town_id). The door cell (player spawn,
// bottom-centre) doubles as the exit — stepping back onto it arms the LEAVE TOWN confirm.

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
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_town_floor_deco);
    set_bkg_palette(PAL_OW_ACCENT, 1u, pal_town_accent);
}

static uint8_t tg_hash(uint8_t town_id, uint8_t salt) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 3571u) ^ (uint16_t)((uint16_t)salt * 149u));
    h ^= (uint16_t)(h >> 7);
    h ^= (uint16_t)(h >> 3);
    return (uint8_t)h;
}

// One building rectangle: brick wall ring with a hollow floor interior. Overlapping calls merge
// into multi-rectangular (L-shaped) buildings — the later interior carve re-opens the shared wall.
static void tg_building_rect(uint8_t x0, uint8_t y0, uint8_t bw, uint8_t bh) {
    uint8_t x, y;
    for (y = y0; y < (uint8_t)(y0 + bh); y++)
        for (x = x0; x < (uint8_t)(x0 + bw); x++)
            BIT_CLR(floor_bits, TILE_IDX(x, y));
    for (y = (uint8_t)(y0 + 1u); y < (uint8_t)(y0 + bh - 1u); y++)
        for (x = (uint8_t)(x0 + 1u); x < (uint8_t)(x0 + bw - 1u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));
}

BANKREF(town_generate_interior)
void town_generate_interior(uint8_t town_id) BANKED {
    uint8_t x, y, i, n;
    const uint8_t w = active_map_w, h = active_map_h; // 20×20 (map_gen sets dims for FLOORKIND_TOWN)
    const uint8_t cx = (uint8_t)(w >> 1), cy = (uint8_t)(h >> 1);
    const uint8_t door_x = cx, door_y = (uint8_t)(h - 2u);
    uint8_t ax, ay, bx, by; // jittered top-left corners of buildings A (NW) and B (NE)

    for (y = 1u; y < (uint8_t)(h - 1u); y++) // open grass yard inside the brick town wall ring
        for (x = 1u; x < (uint8_t)(w - 1u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));

    // Three brick buildings, doors facing the plaza, one NPC inside each.
    ax = (uint8_t)(2u + (tg_hash(town_id, 1u) & 1u));  // A: 5×5, top-left, door on the east wall
    ay = (uint8_t)(3u + (tg_hash(town_id, 2u) & 1u));
    tg_building_rect(ax, ay, 5u, 5u);
    BIT_SET(floor_bits, TILE_IDX((uint8_t)(ax + 4u), (uint8_t)(ay + 2u))); // door

    bx = (uint8_t)(12u + (tg_hash(town_id, 3u) & 1u)); // B: 5×5, top-right, door on the west wall
    by = (uint8_t)(3u + (tg_hash(town_id, 4u) & 1u));
    tg_building_rect(bx, by, 5u, 5u);
    BIT_SET(floor_bits, TILE_IDX(bx, (uint8_t)(by + 2u))); // door

    tg_building_rect(13u, 12u, 5u, 4u); // C: L-shaped (two merged rects), bottom-right, door west
    tg_building_rect(15u, 14u, 4u, 4u);
    BIT_SET(floor_bits, TILE_IDX(13u, 13u)); // door

    player_spawn_x = door_x; // door = spawn = exit cell (drawn as the stairs-up glyph)
    player_spawn_y = door_y;
    BIT_SET(floor_bits, TILE_IDX(door_x, door_y));
    BIT_SET(floor_bits, TILE_IDX(door_x, (uint8_t)(door_y - 1u))); // guarantee a step off the door

    n = 0u; // features (generate_level zeroed ow_feature_count): fountain, NPCs, deco pines
    ow_features[n].x = cx; ow_features[n].y = cy; // stone well at the road junction
    ow_features[n].type = OW_FEAT_FOUNTAIN; ow_features[n].aux = 0u;
    BIT_SET(floor_bits, TILE_IDX(cx, cy));
    n++;
    { // one villager inside each building
        static const uint8_t ndx[3] = { 2u, 2u, 2u }; // interior offset from each building's corner
        uint8_t nx_[3], ny_[3];
        nx_[0] = (uint8_t)(ax + ndx[0]); ny_[0] = (uint8_t)(ay + 2u);
        nx_[1] = (uint8_t)(bx + ndx[1]); ny_[1] = (uint8_t)(by + 2u);
        nx_[2] = 15u;                    ny_[2] = 13u; // inside C's main room
        for (i = 0u; i < 3u; i++) {
            ow_features[n].x = nx_[i]; ow_features[n].y = ny_[i];
            ow_features[n].type = OW_FEAT_SIGNPOST;
            ow_features[n].aux = (uint8_t)(SIGN_KIND_NPC | i);
            n++;
        }
    }
    for (i = 0u; i < 10u && n < (uint8_t)(MAX_OW_FEATURES - 1u); i++) { // up to ~6 deco pines on open grass
        uint8_t px = (uint8_t)(2u + (tg_hash(town_id, (uint8_t)(60u + i)) % (uint8_t)(w - 4u)));
        uint8_t py = (uint8_t)(2u + (tg_hash(town_id, (uint8_t)(80u + i)) % (uint8_t)(h - 5u)));
        uint8_t k, clash = 0u;
        if (px == cx || py == cy) continue;                       // keep the road cross clear
        if (px == door_x && py >= (uint8_t)(door_y - 2u)) continue; // keep the door approach clear
        if (!BIT_GET(floor_bits, TILE_IDX(px, py))) continue;     // building wall — skip
        // skip building interiors (a pine indoors would wall an NPC in)
        if (px > ax && px < (uint8_t)(ax + 4u) && py > ay && py < (uint8_t)(ay + 4u)) continue;
        if (px > bx && px < (uint8_t)(bx + 4u) && py > by && py < (uint8_t)(by + 4u)) continue;
        if (px > 13u && px < 18u && py > 12u && py < 17u) continue;
        for (k = 0u; k < n; k++)
            if (ow_features[k].x == px && ow_features[k].y == py) { clash = 1u; break; }
        if (clash) continue;
        BIT_CLR(floor_bits, TILE_IDX(px, py)); // pine is a blocking wall cell, drawn by the TREE feature
        ow_features[n].x = px; ow_features[n].y = py;
        ow_features[n].type = OW_FEAT_TREE; ow_features[n].aux = 0u;
        n++;
    }
    ow_feature_count = n;
}
