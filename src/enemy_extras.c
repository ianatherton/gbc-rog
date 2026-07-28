/* Auto-banked companion to enemy.c — contains functions that can live in any bank.
   Pinned to bank 25 (2026-07-27): autobank had been parking it in bank 0, and widening
   enemy_hp[] to uint16_t grew its three spawn-time stores enough to overflow HOME into bank 1.
   Every function here is BANKED in enemy.h, so callers far-call it either way. */
#pragma bank 25

#include "enemy.h"
#include "globals.h"
#include "map.h"
#include "ui.h"
#include "lcd.h" // lcd_hp_panic_flash_trigger — enemy_resolve_hit, evicted here from bank 2

BANKREF_EXTERN(enemy_place_slot_far)
BANKREF_EXTERN(enemy_effective_max_hp)

static void push_lit(const char *s) { // bank-25 ROM literal → RAM before the bank-5 ui call, else it garbles
    char buf[16];
    uint8_t i = 0u;
    while (s[i] && i < 15u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

/* Moved out of bank 2 to free space; callers use BANKED mechanism unchanged. */
BANKREF(enemy_type_short_name_copy)
void enemy_type_short_name_copy(uint8_t t, char *out, uint8_t cap) BANKED {
    static const char *const n[NUM_ENEMY_TYPES] = {
        "SNAKE", "SLIME", "RAT", "BAT", "BIG SKELL", "IMP", "SKELETON", "GORGON", "BIG SLIME", "SPHINX",
        "GHOST"
    };
    const char *s = (t < NUM_ENEMY_TYPES) ? n[t] : "?";
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
}

/* Moved out of bank 2 (2026-07-27) to pay back the bytes the Ghost's MOVE_PHASE hook cost there.
   It was already BANKED, so every call site is unchanged. The RAM-buffer trick below is still
   required — ui_combat_log_push_pal is BANKED into bank 5 and the trampoline remaps ROM before
   the callee dereferences the pointer, so a ROM literal from this bank would read back garbage. */
BANKREF(enemy_resolve_hit)
uint8_t enemy_resolve_hit(uint8_t slot) BANKED { // one strike: log line + subtract HP; returns 1 if dodged, 0 if landed
    uint8_t hit = enemy_effective_damage(enemy_type[slot]);
    uint16_t hp_before = player_hp;
    char logbuf[20];
    uint8_t p, d; // d consumed while formatting digits

    if (player_dodge && (uint8_t)(DIV_REG % 100u) < player_dodge) {
        logbuf[0] = 'D'; logbuf[1] = 'O'; logbuf[2] = 'D'; logbuf[3] = 'G'; logbuf[4] = 'E'; logbuf[5] = 0;
        ui_combat_log_push_pal(logbuf, PAL_UI);
        return 1u; // hit fully avoided — no HP change, no panic-flash check
    }
    if (player_armor) hit = (uint8_t)(((uint16_t)hit * (100u - player_armor)) / 100u);
    if (zerk_turns && hit < 255u) hit++; // Zerk Mode downside — after armor, before formatting so the log matches

    p = 0; d = hit;
    logbuf[p++] = 'Y'; logbuf[p++] = 'O'; logbuf[p++] = 'U'; logbuf[p++] = ' '; logbuf[p++] = '-';
    if (d >= 100u) { logbuf[p++] = (char)('0' + d / 100u); d %= 100u; logbuf[p++] = (char)('0' + d / 10u); d %= 10u; }
    else if (d >= 10u) { logbuf[p++] = (char)('0' + d / 10u); d %= 10u; }
    logbuf[p++] = (char)('0' + d);
    logbuf[p] = 0;
    ui_combat_log_push_pal(logbuf, PAL_LIFE_UI);
    if (player_hp > hit) player_hp -= hit;
    else                 player_hp  = 0;
    if (player_hp_max > 0u) {
        // pct > 30 ⟺ hp*10 > hp_max*3: hp*100 would wrap uint16 above hp 655.
        uint16_t thresh = player_hp_max * 3u;
        if (hp_before * 10u > thresh && player_hp * 10u <= thresh) lcd_hp_panic_flash_trigger();
    }
    return 0u; // hit landed (armor may have absorbed it, but it connected)
}

/* MOVE_PHASE turn — the Ghost. Two jobs in one call so bank 2 pays for a single bcall:
   (1) pick the step, ignoring terrain entirely (the caller skips its is_walkable gate for
       MOVE_PHASE, so this walks straight through rock);
   (2) update enemy_hidden[slot].
   Visibility is decided from the DESTINATION, not the current tile, so a ghost that closes to
   melee is already solid on the frame the player sees it arrive — and landing on the player's
   tile (a strike, Chebyshev 0) counts as adjacent, so it can never hit you while invisible. */
BANKREF(enemy_ghost_step)
void enemy_ghost_step(uint8_t slot, uint8_t px, uint8_t py, uint8_t *nx, uint8_t *ny) BANKED {
    uint8_t sx = enemy_x[slot], sy = enemy_y[slot];
    uint8_t vanish_pct = enemy_defs[enemy_type[slot]].param;
    uint8_t hdist = (px > sx) ? (uint8_t)(px - sx) : (uint8_t)(sx - px);
    uint8_t vdist = (py > sy) ? (uint8_t)(py - sy) : (uint8_t)(sy - py);
    uint8_t dx, dy, cheb;

    /* One king-move toward the player along the dominant axis — mirrors step_direct in enemy.c
       (a bank-2 static; duplicated rather than exported for a handful of bytes). The player is
       always in bounds, so stepping toward them can never leave the map. */
    *nx = sx; *ny = sy;
    if (hdist >= vdist) { if (px > sx) *nx = (uint8_t)(sx + 1u); else if (px < sx) *nx = (uint8_t)(sx - 1u); }
    else                { if (py > sy) *ny = (uint8_t)(sy + 1u); else if (py < sy) *ny = (uint8_t)(sy - 1u); }

    dx   = (px > *nx) ? (uint8_t)(px - *nx) : (uint8_t)(*nx - px);
    dy   = (py > *ny) ? (uint8_t)(py - *ny) : (uint8_t)(*ny - py);
    cheb = (dx > dy) ? dx : dy;

    if (cheb <= 1u)                 enemy_hidden[slot] = 0u;         // king-adjacent (or striking): always solid
    else if (enemy_hidden[slot])    enemy_hidden[slot]--;            // still fading back in
    else if ((uint8_t)(rand() % 100u) < vanish_pct)
        enemy_hidden[slot] = (uint8_t)(GHOST_HIDE_TURNS_MIN + (uint8_t)(rand() % GHOST_HIDE_TURNS_SPAN));
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
        enemy_status[ni] = 0u; enemy_hidden[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
        enemy_persistent[ni] = 0u; // transient: vanishes on revisit, no gravestone
        enemy_place_slot_far(ni, tx, ty);
        if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
        spawned++;
    }
    if (spawned > 0u) push_lit("SLIME SPLITS!");
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
                enemy_status[ni] = 0u; enemy_hidden[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
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
        enemy_status[ni] = 0u; enemy_hidden[ni] = 0u; enemy_force_active[ni] = 0u; enemy_alive[ni] = 1u;
        enemy_persistent[ni] = 0u; // transient: vanishes on revisit, no gravestone
        enemy_place_slot_far(ni, tx, ty);
        if (ni >= num_enemies) num_enemies = (uint8_t)(ni + 1u);
        spawned++;
    }
    if (spawned > 0u) push_lit("GORGON SUMMONS!");
}
