#pragma bank 29

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h"
#include "map.h"
#include "dungeon.h"
#include <gbdk/platform.h>

// Town interior (floors TOWN_FLOOR_BASE + 0..2): a 20×20 safe zone entered from the hub town's
// door. No enemies, no items. Layout is deterministic from (run_seed, town_id): a walled yard with
// a few hut blocks, 3 NPCs (signpost-style dialogue on bump) and one heal fountain. The door cell
// (player spawn, bottom-centre) doubles as the exit — stepping back onto it arms the LEAVE TOWN
// confirm (state_gameplay spawn-cell branch → TRANS_FLOOR_UP → hub, landing beside the town).

BANKREF(biome_town_copy_defs)
void biome_town_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for FLOORKIND_TOWN anyway
}

static uint8_t tg_hash(uint8_t town_id, uint8_t salt) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 3571u) ^ (uint16_t)((uint16_t)salt * 149u));
    h ^= (uint16_t)(h >> 7);
    h ^= (uint16_t)(h >> 3);
    return (uint8_t)h;
}

// Carve a rectangular hut (walls) back out of the open yard, keeping clear of the door column.
static void tg_hut(uint8_t hx, uint8_t hy, uint8_t hw, uint8_t hh) {
    uint8_t x, y;
    for (y = hy; y < (uint8_t)(hy + hh); y++)
        for (x = hx; x < (uint8_t)(hx + hw); x++)
            BIT_CLR(floor_bits, TILE_IDX(x, y));
}

BANKREF(town_generate_interior)
void town_generate_interior(uint8_t town_id) BANKED {
    uint8_t x, y, i, n;
    const uint8_t w = active_map_w, h = active_map_h; // 20×20 (map_gen sets dims for FLOORKIND_TOWN)
    const uint8_t door_x = (uint8_t)(w >> 1), door_y = (uint8_t)(h - 2u);

    for (y = 1u; y < (uint8_t)(h - 1u); y++) // open yard inside a solid 1-tile wall ring
        for (x = 1u; x < (uint8_t)(w - 1u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));

    for (i = 0u; i < 3u; i++) { // 2-3 hut blocks along the side walls, never blocking the door lane
        uint8_t hw = (uint8_t)(2u + (tg_hash(town_id, i) & 1u));
        uint8_t hh = (uint8_t)(2u + ((tg_hash(town_id, (uint8_t)(i + 10u)) >> 1) & 1u));
        uint8_t hx = (i & 1u) ? (uint8_t)(w - 2u - hw - (tg_hash(town_id, (uint8_t)(i + 20u)) % 3u))
                              : (uint8_t)(2u + (tg_hash(town_id, (uint8_t)(i + 20u)) % 3u));
        uint8_t hy = (uint8_t)(2u + (tg_hash(town_id, (uint8_t)(i + 30u)) % (uint8_t)(h - 8u)));
        if (i == 2u && (tg_hash(town_id, 99u) & 1u)) break; // sometimes only 2 huts
        if (hx <= (uint8_t)(door_x + 1u) && (uint8_t)(hx + hw) >= (uint8_t)(door_x - 1u)
                && (uint8_t)(hy + hh) >= (uint8_t)(door_y - 3u)) hy = 2u; // keep the door approach open
        tg_hut(hx, hy, hw, hh);
    }

    player_spawn_x = door_x; // door = spawn = exit cell (drawn as the stairs-up glyph)
    player_spawn_y = door_y;
    BIT_SET(floor_bits, TILE_IDX(door_x, door_y));
    BIT_SET(floor_bits, TILE_IDX(door_x, (uint8_t)(door_y - 1u))); // guarantee a step off the door

    n = 0u; // NPCs + fountain as prefab features (generate_level zeroed ow_feature_count)
    {
        const uint8_t cx = (uint8_t)(w >> 1), cy = (uint8_t)(h >> 1);
        ow_features[n].x = cx; ow_features[n].y = cy; // fountain at the plaza centre
        ow_features[n].type = OW_FEAT_FOUNTAIN; ow_features[n].aux = 0u;
        BIT_SET(floor_bits, TILE_IDX(cx, cy));
        n++;
        for (i = 0u; i < 3u; i++) { // 3 NPCs scattered around the plaza
            uint8_t px = (uint8_t)(3u + (tg_hash(town_id, (uint8_t)(40u + i)) % (uint8_t)(w - 6u)));
            uint8_t py = (uint8_t)(3u + (tg_hash(town_id, (uint8_t)(50u + i)) % (uint8_t)(h - 7u)));
            if (px == cx && py == cy) px++;
            if (px == door_x && py >= (uint8_t)(door_y - 1u)) py = (uint8_t)(door_y - 3u);
            BIT_SET(floor_bits, TILE_IDX(px, py)); // NPCs stand on open ground (bump to talk)
            ow_features[n].x = px; ow_features[n].y = py;
            ow_features[n].type = OW_FEAT_SIGNPOST;
            ow_features[n].aux = (uint8_t)(SIGN_KIND_NPC | i);
            n++;
        }
    }
    ow_feature_count = n;
}
