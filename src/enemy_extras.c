/* Auto-banked companion to enemy.c — contains functions that can live in any bank. */

#include "enemy.h"
#include "globals.h"
#include "map.h"
#include "ui.h"
#include "lcd.h"

BANKREF_EXTERN(enemy_place_slot_far)
BANKREF_EXTERN(enemy_effective_max_hp)
BANKREF_EXTERN(enemy_effective_damage)

#define ENEMY_SLOT_HASH_SIZE  64u
#define ENEMY_SLOT_EMPTY      0xFFFFu
#define ENEMY_SLOT_TOMBSTONE  0xFFFEu
#define CORPSE_SLOT_HASH_SIZE 64u
#define CORPSE_SLOT_EMPTY     0xFFFFu
#define CORPSE_SLOT_TOMBSTONE 0xFFFEu
extern uint16_t enemy_slot_keys[ENEMY_SLOT_HASH_SIZE];
extern uint8_t  enemy_slot_vals[ENEMY_SLOT_HASH_SIZE];
extern uint16_t corpse_slot_keys[CORPSE_SLOT_HASH_SIZE];
extern uint8_t  corpse_slot_vals[CORPSE_SLOT_HASH_SIZE];

/* Moved out of bank 2 to free space; callers use BANKED mechanism unchanged. */
BANKREF(enemy_type_short_name_copy)
void enemy_type_short_name_copy(uint8_t t, char *out, uint8_t cap) BANKED {
    static const char *const n[NUM_ENEMY_TYPES] = {
        "SNAKE", "SLIME", "RAT", "BAT", "BIG SKELL", "IMP", "SKELETON", "SKEL ARCHR"
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

/* Moved from enemy.c (bank 2) to free space. */
BANKREF(enemy_clear_slot)
void enemy_clear_slot(uint8_t x, uint8_t y) BANKED {
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = (uint8_t)(idx & (ENEMY_SLOT_HASH_SIZE - 1u));
    uint8_t probe;
    BIT_CLR(enemy_occ, idx);
    for (probe = 0; probe < ENEMY_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (ENEMY_SLOT_HASH_SIZE - 1u));
        if (enemy_slot_keys[p] == ENEMY_SLOT_EMPTY) return;
        if (enemy_slot_keys[p] == idx) {
            enemy_slot_keys[p] = ENEMY_SLOT_TOMBSTONE;
            enemy_slot_vals[p] = ENEMY_DEAD;
            return;
        }
    }
}

BANKREF(corpse_place_slot)
void corpse_place_slot(uint8_t slot, uint8_t x, uint8_t y) BANKED {
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = (uint8_t)(idx & (CORPSE_SLOT_HASH_SIZE - 1u));
    uint8_t probe;
    uint8_t first_tombstone = ENEMY_DEAD;
    for (probe = 0; probe < CORPSE_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (CORPSE_SLOT_HASH_SIZE - 1u));
        if (corpse_slot_keys[p] == idx) {
            corpse_slot_vals[p] = slot;
            return;
        }
        if (corpse_slot_keys[p] == CORPSE_SLOT_TOMBSTONE && first_tombstone == ENEMY_DEAD)
            first_tombstone = p;
        if (corpse_slot_keys[p] == CORPSE_SLOT_EMPTY) {
            uint8_t w = (first_tombstone != ENEMY_DEAD) ? first_tombstone : p;
            corpse_slot_keys[w] = idx;
            corpse_slot_vals[w] = slot;
            return;
        }
    }
    if (first_tombstone != ENEMY_DEAD) {
        corpse_slot_keys[first_tombstone] = idx;
        corpse_slot_vals[first_tombstone] = slot;
    }
}

BANKREF(enemy_resolve_hit)
void enemy_resolve_hit(uint8_t slot) BANKED {
    uint8_t hit = enemy_effective_damage(enemy_type[slot]);
    uint8_t hp_before = player_hp;
    char logbuf[20];
    uint8_t p = 0, d = hit;
    logbuf[p++] = 'Y'; logbuf[p++] = 'O'; logbuf[p++] = 'U'; logbuf[p++] = ' '; logbuf[p++] = '-';
    if (d >= 100u) { logbuf[p++] = (char)('0' + d / 100u); d %= 100u; logbuf[p++] = (char)('0' + d / 10u); d %= 10u; }
    else if (d >= 10u) { logbuf[p++] = (char)('0' + d / 10u); d %= 10u; }
    logbuf[p++] = (char)('0' + d);
    logbuf[p] = 0;
    ui_combat_log_push_pal(logbuf, PAL_LIFE_UI);
    if (player_hp > hit) player_hp -= hit;
    else                 player_hp  = 0;
    if (player_hp_max > 0u) {
        uint8_t pct_b = (uint8_t)(((uint16_t)hp_before * 100u) / (uint16_t)player_hp_max);
        uint8_t pct_a = (uint8_t)(((uint16_t)player_hp * 100u) / (uint16_t)player_hp_max);
        if (pct_b > 30u && pct_a <= 30u) lcd_hp_panic_flash_trigger();
    }
}
