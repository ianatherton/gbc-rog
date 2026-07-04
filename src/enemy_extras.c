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
        "SNAKE", "SLIME", "RAT", "BAT", "BIG SKELL", "IMP", "SKELETON", "GORGON", "BIG SLIME", "SPHINX"
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
    if (type != ENEMY_SLIME || !(rand() & 1u)) return; // SLIME_BIG uses the guaranteed death-spawn instead
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
        enemy_persistent[ni] = 0u; // transient: vanishes on revisit, no gravestone
        enemy_place_slot_far(ni, tx, ty);
        if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
        spawned++;
    }
    if (spawned > 0u) ui_combat_log_push("SLIME SPLITS!");
}

#define ENEMY_SLIME_BIG_SPAWN_CAP 10u

// Guaranteed pop on the elite's death (any kill method — see combat.c's combat_damage_enemy):
// ring-searches outward (Chebyshev radius 1..3) for free/walkable/non-player tiles and fills
// up to ENEMY_SLIME_BIG_SPAWN_CAP with transient copies of its base type (elite_base_type).
BANKREF(enemy_slime_big_death_spawn)
void enemy_slime_big_death_spawn(uint8_t dx, uint8_t dy) BANKED {
    uint8_t r, ni, tx, ty, spawned = 0u;
    int8_t ox, oy;
    for (r = 1u; r <= 3u && spawned < ENEMY_SLIME_BIG_SPAWN_CAP; r++) {
        for (oy = (int8_t)-r; oy <= (int8_t)r && spawned < ENEMY_SLIME_BIG_SPAWN_CAP; oy++) {
            for (ox = (int8_t)-r; ox <= (int8_t)r && spawned < ENEMY_SLIME_BIG_SPAWN_CAP; ox++) {
                if (ox != (int8_t)-r && ox != (int8_t)r && oy != (int8_t)-r && oy != (int8_t)r) continue; // ring perimeter only
                tx = (uint8_t)(dx + ox);
                ty = (uint8_t)(dy + oy);
                if (tx >= MAP_W || ty >= MAP_H) continue;
                if (tx == g_player_x && ty == g_player_y) continue;
                {
                    uint16_t tidx = TILE_IDX(tx, ty);
                    if (!BIT_GET(floor_bits, tidx) || BIT_GET(enemy_occ, tidx)) continue;
                }
                if (dead_enemy_pool_count > 0u) {
                    ni = dead_enemy_pool[--dead_enemy_pool_count];
                } else {
                    for (ni = 0u; ni < MAX_ENEMIES; ni++) {
                        if (!enemy_alive[ni]) break;
                    }
                    if (ni >= MAX_ENEMIES) return;
                }
                enemy_x[ni] = tx; enemy_y[ni] = ty;
                enemy_type[ni] = elite_base_type;
                enemy_hp[ni] = enemy_effective_max_hp(elite_base_type);
                enemy_status[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
                enemy_persistent[ni] = 0u; // transient: vanishes on revisit, no gravestone
                enemy_place_slot_far(ni, tx, ty);
                if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
                spawned++;
            }
        }
    }
    if (spawned > 0u) { // "<BASE> SWARM!" — RAM buffer: this bank's literal would garble in the bank-5 push
        char buf[16];
        uint8_t i;
        enemy_type_short_name_copy(elite_base_type, buf, 9u);
        for (i = 0u; buf[i]; i++) ;
        buf[i++] = ' '; buf[i++] = 'S'; buf[i++] = 'W'; buf[i++] = 'A';
        buf[i++] = 'R'; buf[i++] = 'M'; buf[i++] = '!'; buf[i] = 0;
        ui_combat_log_push(buf);
    }
}

BANKREF(enemy_gorgon_summon)
void enemy_gorgon_summon(uint8_t slot) BANKED {
    uint8_t d, ni, tx, ty, spawned = 0u, snake_count = 0u, snake_cap;
    if (!enemy_alive[slot] || enemy_type[slot] != ENEMY_GORGON) return;
    for (ni = 0u; ni < num_enemies; ni++)
        if (enemy_alive[ni] && enemy_type[ni] == ENEMY_SNAKE) snake_count++;
    if (snake_count >= 5u) return;
    snake_cap = (uint8_t)(5u - snake_count);
    if (snake_cap > 2u) snake_cap = 2u;
    for (d = 0u; d < 4u && spawned < snake_cap; d++) {
        tx = (uint8_t)(enemy_x[slot] + slime_ox[d]);
        ty = (uint8_t)(enemy_y[slot] + slime_oy[d]);
        if (tx >= MAP_W || ty >= MAP_H) continue;
        {
            uint16_t tidx = TILE_IDX(tx, ty);
            if (!BIT_GET(floor_bits, tidx) || BIT_GET(enemy_occ, tidx)) continue;
        }
        if (dead_enemy_pool_count > 0u) {
            ni = dead_enemy_pool[--dead_enemy_pool_count];
        } else {
            for (ni = 0u; ni < MAX_ENEMIES; ni++)
                if (!enemy_alive[ni]) break;
            if (ni >= MAX_ENEMIES) break;
        }
        enemy_x[ni] = tx; enemy_y[ni] = ty;
        enemy_type[ni]   = ENEMY_SNAKE;
        enemy_hp[ni]     = enemy_effective_max_hp(ENEMY_SNAKE);
        enemy_status[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
        enemy_persistent[ni] = 0u; // transient: vanishes on revisit, no gravestone
        enemy_place_slot_far(ni, tx, ty);
        if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
        spawned++;
    }
    if (spawned > 0u) ui_combat_log_push("GORGON SUMMONS!");
}
