/* Auto-banked companion to enemy.c — contains functions that can live in any bank. */

#include "enemy.h"
#include "globals.h"
#include "map.h"
#include "ui.h"

BANKREF_EXTERN(enemy_place_slot_far)
BANKREF_EXTERN(enemy_effective_max_hp)

/* Moved out of bank 2 to free space; callers use BANKED mechanism unchanged. */
BANKREF(enemy_type_short_name_copy)
void enemy_type_short_name_copy(uint8_t t, char *out, uint8_t cap) BANKED {
    static const char *const n[NUM_ENEMY_TYPES] = {
        "SNAKE", "SLIME", "RAT", "BAT", "BIG SKELL", "IMP", "SKELETON"
    };
    const char *s = (t < NUM_ENEMY_TYPES) ? n[t] : "?";
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
}

/* Cardinal offsets: 0xFF == (uint8_t)-1, caught by >= MAP_W/H bounds check. */
static const uint8_t slime_ox[4] = {0u, 0u, 0xFFu, 1u}; /* N S W E */
static const uint8_t slime_oy[4] = {0xFFu, 1u, 0u, 0u};

BANKREF(enemy_slime_split)
void enemy_slime_split(uint8_t type, uint8_t dx, uint8_t dy, uint8_t px, uint8_t py) BANKED {
    uint8_t d, ni, tx, ty, spawned = 0u;
    if (type != ENEMY_SLIME || !(rand() & 1u)) return;
    for (d = 0u; d < 4u && spawned < 3u; d++) {
        tx = (uint8_t)(dx + slime_ox[d]);
        ty = (uint8_t)(dy + slime_oy[d]);
        if (tx >= MAP_W || ty >= MAP_H) continue;
        if (tx == px && ty == py) continue;
        {
            uint16_t tidx = TILE_IDX(tx, ty);
            if (!BIT_GET(floor_bits, tidx) || BIT_GET(enemy_occ, tidx)) continue;
        }
        /* Find a free slot: prefer recently freed pool entries, then scan. */
        if (dead_enemy_pool_count > 0u) {
            ni = dead_enemy_pool[--dead_enemy_pool_count];
        } else {
            for (ni = 0u; ni < MAX_ENEMIES; ni++) {
                if (!enemy_alive[ni]) break;
            }
            if (ni >= MAX_ENEMIES) break;
        }
        enemy_x[ni] = tx; enemy_y[ni] = ty;
        enemy_type[ni] = ENEMY_SLIME;
        enemy_hp[ni] = enemy_effective_max_hp(ENEMY_SLIME);
        enemy_status[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
        enemy_place_slot_far(ni, tx, ty);
        if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
        spawned++;
    }
    if (spawned > 0u) ui_combat_log_push("SLIME SPLITS!");
}
