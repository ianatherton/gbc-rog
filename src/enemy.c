#pragma bank 2

#include "enemy.h"
#include "globals.h"
#include "map.h"
#include "ui.h"
#include "entity_sprites.h"
#include "lcd.h"
#include "perf.h"
#include <string.h>

// enemy_defs[] is defined in biome.c (HOME) — populated from bank 10/11/12 by biome_load_active

uint8_t enemy_x[MAX_ENEMIES];    // map column; ENEMY_DEAD means slot unused
uint8_t enemy_y[MAX_ENEMIES];    // map row
uint8_t enemy_type[MAX_ENEMIES]; // index into enemy_defs
uint8_t enemy_hp[MAX_ENEMIES];   // hits remaining; player reduces before kill
uint8_t num_enemies;             // live count ≤ NUM_ENEMIES after spawn

uint8_t corpse_x[MAX_CORPSES];
uint8_t corpse_y[MAX_CORPSES];
uint8_t corpse_tile[MAX_CORPSES]; // L1–L5 floor deco (random at kill)
uint8_t num_corpses;

uint8_t enemy_occ[BITSET_BYTES];

#define ENEMY_SLOT_HASH_SIZE 64u
#define ENEMY_SLOT_EMPTY 0xFFFFu
#define ENEMY_SLOT_TOMBSTONE 0xFFFEu
static uint16_t enemy_slot_keys[ENEMY_SLOT_HASH_SIZE];
static uint8_t enemy_slot_vals[ENEMY_SLOT_HASH_SIZE];

#define CORPSE_SLOT_HASH_SIZE 64u
#define CORPSE_SLOT_EMPTY 0xFFFFu
#define CORPSE_SLOT_TOMBSTONE 0xFFFEu
static uint16_t corpse_slot_keys[CORPSE_SLOT_HASH_SIZE];
static uint8_t corpse_slot_vals[CORPSE_SLOT_HASH_SIZE];

static uint8_t enemy_slot_hash_idx(uint16_t tile_idx) {
    return (uint8_t)(tile_idx & (ENEMY_SLOT_HASH_SIZE - 1u)); // size is power-of-two
}

void enemy_grids_init(void) {
    uint8_t i;
    memset(enemy_occ, 0, sizeof enemy_occ);
    for (i = 0; i < ENEMY_SLOT_HASH_SIZE; i++) {
        enemy_slot_keys[i] = ENEMY_SLOT_EMPTY;
        enemy_slot_vals[i] = ENEMY_DEAD;
    }
    for (i = 0; i < CORPSE_SLOT_HASH_SIZE; i++) {
        corpse_slot_keys[i] = CORPSE_SLOT_EMPTY;
        corpse_slot_vals[i] = ENEMY_DEAD;
    }
}

void enemy_place_slot(uint8_t slot, uint8_t x, uint8_t y) {
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = enemy_slot_hash_idx(idx);
    uint8_t probe;
    uint8_t first_tombstone = ENEMY_DEAD;
    BIT_SET(enemy_occ, idx);
    for (probe = 0; probe < ENEMY_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (ENEMY_SLOT_HASH_SIZE - 1u));
        if (enemy_slot_keys[p] == idx) {
            enemy_slot_vals[p] = slot;
            return;
        }
        if (enemy_slot_keys[p] == ENEMY_SLOT_TOMBSTONE && first_tombstone == ENEMY_DEAD)
            first_tombstone = p;
        if (enemy_slot_keys[p] == ENEMY_SLOT_EMPTY) {
            uint8_t w = (first_tombstone != ENEMY_DEAD) ? first_tombstone : p;
            enemy_slot_keys[w] = idx;
            enemy_slot_vals[w] = slot;
            return;
        }
    }
    if (first_tombstone != ENEMY_DEAD) {
        enemy_slot_keys[first_tombstone] = idx;
        enemy_slot_vals[first_tombstone] = slot;
    }
}

void enemy_clear_slot(uint8_t x, uint8_t y) BANKED {
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = enemy_slot_hash_idx(idx);
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

void corpse_clear_slot(uint8_t x, uint8_t y) {
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = (uint8_t)(idx & (CORPSE_SLOT_HASH_SIZE - 1u));
    uint8_t probe;
    for (probe = 0; probe < CORPSE_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (CORPSE_SLOT_HASH_SIZE - 1u));
        if (corpse_slot_keys[p] == CORPSE_SLOT_EMPTY) return;
        if (corpse_slot_keys[p] == idx) {
            corpse_slot_keys[p] = CORPSE_SLOT_TOMBSTONE;
            corpse_slot_vals[p] = ENEMY_DEAD;
            return;
        }
    }
}

static const uint8_t CORPSE_DECO_OFF[2] = { // defs.h L column — one picked per corpse
    TILE_FLOOR_DECO_2, TILE_FLOOR_DECO_3
};

uint8_t enemy_anim_toggle; // flips each ENEMY_ANIM_DIV_TICKS of DIV accumulation
uint8_t enemy_attack_slots[MAX_ENEMIES];
uint8_t enemy_attack_count;
uint8_t enemy_force_active[MAX_ENEMIES];

void enemy_type_short_name_copy(uint8_t t, char *out, uint8_t cap) BANKED {
    static const char *const n[NUM_ENEMY_TYPES] = {
        "SERPENT", "SLIMESKULL", "RAT", "BAT", "SKELETON", "GOBLIN"
    };
    const char *s = (t < NUM_ENEMY_TYPES) ? n[t] : "?";
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; } // bytes copy while bank 2 is mapped — safe across the bcall return
    out[i] = 0;
}

uint8_t enemy_effective_max_hp(uint8_t type) BANKED {
    uint8_t scale_floor;
    if (type >= NUM_ENEMY_TYPES) return 1u;
    scale_floor = (floor_num > 1u) ? (uint8_t)(floor_num - 1u) : 1u; // entry floor doesn't inflate stats; floor 2 starts baseline
    { uint16_t v = (uint16_t)enemy_defs[type].max_hp * (uint16_t)scale_floor;
      return (v > 255u) ? 255u : (uint8_t)v; }
}

uint8_t enemy_effective_damage(uint8_t type) BANKED {
    uint8_t scale_floor;
    if (type >= NUM_ENEMY_TYPES) return 1u;
    scale_floor = (floor_num > 1u) ? (uint8_t)(floor_num - 1u) : 1u; // keep damage curve aligned with HP scaling
    { uint16_t v = (uint16_t)enemy_defs[type].damage * (uint16_t)scale_floor;
      return (v > 255u) ? 255u : (uint8_t)v; }
}

static uint8_t   anim_last_div; // previous DIV_REG sample for delta
static uint32_t anim_ticks;     // running sum of DIV deltas (uint32 avoids overflow in long play)

void enemy_anim_reset(void) { // sync to hardware timer on level load
    anim_last_div = DIV_REG;
    anim_ticks    = 0;
}

uint8_t enemy_anim_update(void) { // call every frame from main loop
    uint8_t div = DIV_REG;
    anim_ticks += (uint8_t)(div - anim_last_div); // unsigned delta wraps correctly for 8-bit counter
    anim_last_div = div;
    if (anim_ticks >= ENEMY_ANIM_DIV_TICKS) {
        anim_ticks -= ENEMY_ANIM_DIV_TICKS;
        enemy_anim_toggle ^= 1; // swap glyph frame
        return 1;               // caller should redraw enemy tiles
    }
    return 0;
}

uint8_t enemy_at(uint8_t x, uint8_t y) { // slot map keeps occupied lookups O(1)
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = enemy_slot_hash_idx(idx);
    uint8_t probe;
    for (probe = 0; probe < ENEMY_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (ENEMY_SLOT_HASH_SIZE - 1u));
        if (enemy_slot_keys[p] == ENEMY_SLOT_EMPTY) return ENEMY_DEAD;
        if (enemy_slot_keys[p] == idx) return enemy_slot_vals[p];
    }
    return ENEMY_DEAD;
}

uint8_t corpse_sheet_at(uint8_t x, uint8_t y) { // O(1) when no corpse (common case)
    uint16_t idx = TILE_IDX(x, y);
    uint8_t h = (uint8_t)(idx & (CORPSE_SLOT_HASH_SIZE - 1u));
    uint8_t probe;
    for (probe = 0; probe < CORPSE_SLOT_HASH_SIZE; probe++) {
        uint8_t p = (uint8_t)((h + probe) & (CORPSE_SLOT_HASH_SIZE - 1u));
        uint16_t k = corpse_slot_keys[p];
        if (k == CORPSE_SLOT_EMPTY) return 255;
        if (k == idx) return corpse_tile[corpse_slot_vals[p]];
    }
    return 255;
}

uint8_t corpse_at(uint8_t x, uint8_t y) { return corpse_sheet_at(x, y) != 255; }

uint8_t corpse_deco_random(void) BANKED { return CORPSE_DECO_OFF[rand() & 1u]; } // L2/L3

void spawn_enemies(void) { // random placement with collision checks
    uint8_t i;
    num_enemies = 0;
    for (i = 0; i < MAX_ENEMIES; i++) enemy_force_active[i] = 0u;
    if (floor_num == 1u) return; // entry floor is a safe 20x20 no-monster zone
    for (i = 0; i < NUM_ENEMIES; i++) {
        uint8_t attempts;
        for (attempts = 0; attempts < 100; attempts++) {
            uint8_t tx = (uint8_t)(rand() % MAP_W);
            uint8_t ty = (uint8_t)(rand() % MAP_H);
            if ((tx != player_spawn_x || ty != player_spawn_y)
                    && is_walkable(tx, ty)
                    && enemy_at(tx, ty) == ENEMY_DEAD) {
                enemy_x[num_enemies]    = tx;
                enemy_y[num_enemies]    = ty;
                enemy_type[num_enemies] = (uint8_t)(rand() % (enemy_defs_count ? enemy_defs_count : 1u));
                enemy_hp[num_enemies]   = enemy_effective_max_hp(enemy_type[num_enemies]);
                enemy_alive[num_enemies] = 1u;
                enemy_force_active[num_enemies] = 0u;
                enemy_place_slot(num_enemies, tx, ty);
                num_enemies++;
                break;
            }
        }
    }
}

void enemy_set_force_active(uint8_t slot, uint8_t on) {
    if (slot >= MAX_ENEMIES) return;
    enemy_force_active[slot] = on ? 1u : 0u;
}

uint8_t enemy_get_force_active(uint8_t slot) {
    if (slot >= MAX_ENEMIES) return 0u;
    return enemy_force_active[slot];
}

static void step_direct(uint8_t sx, uint8_t sy,
                         uint8_t tx, uint8_t ty,
                         uint8_t *nx, uint8_t *ny) { // one king-move toward target along dominant axis
    int8_t  dx    = 0, dy = 0;
    uint8_t hdist = (tx > sx) ? tx - sx : sx - tx;
    uint8_t vdist = (ty > sy) ? ty - sy : sy - ty;
    if      (sx < tx) dx =  1;
    else if (sx > tx) dx = -1;
    if      (sy < ty) dy =  1;
    else if (sy > ty) dy = -1;
    if (hdist >= vdist) { if (dx) *nx = (uint8_t)((int16_t)sx + dx); } // prefer horizontal if tie or larger
    else                { if (dy) *ny = (uint8_t)((int16_t)sy + dy); }
}

static void step_nav_chase(uint8_t sx, uint8_t sy,
                           uint8_t px, uint8_t py, uint8_t player_node,
                           uint8_t *next_hop_cache,
                           uint8_t *nx, uint8_t *ny) { // graph-guided chase with per-turn nav cache
    uint8_t enemy_node  = nearest_nav_node(sx, sy);

    if (enemy_node == NAV_NO_LINK || player_node == NAV_NO_LINK
            || enemy_node == player_node) { // degenerate: same node or missing graph
        step_direct(sx, sy, px, py, nx, ny);
        return;
    }

    if (next_hop_cache[enemy_node] == NAV_NO_LINK)
        next_hop_cache[enemy_node] = nav_next_step(enemy_node, player_node);
    uint8_t next_node = next_hop_cache[enemy_node];
    if (next_node == NAV_NO_LINK) { // BFS failed (disconnected)
        step_direct(sx, sy, px, py, nx, ny);
        return;
    }

    step_direct(sx, sy, nav_nodes[next_node].x, nav_nodes[next_node].y, nx, ny); // move toward corridor target
}

static void step_random(uint8_t sx, uint8_t sy,
                         uint8_t *nx, uint8_t *ny) { // pick among cardinal neighbours
    uint8_t attempt;
    for (attempt = 0; attempt < 4; attempt++) {
        uint8_t d  = rand() >> 6; // prefer high bits of rand()
        uint8_t cx = sx, cy = sy;
        if      (d == 0 && sy > 0)         cy = sy - 1;
        else if (d == 1 && sy < MAP_H - 1) cy = sy + 1;
        else if (d == 2 && sx > 0)         cx = sx - 1;
        else if (d == 3 && sx < MAP_W - 1) cx = sx + 1;
        else continue; // direction invalid for this d — retry
        if (is_walkable(cx, cy)) { *nx = cx; *ny = cy; return; }
    }
}

void enemy_resolve_hit(uint8_t slot) BANKED { // one strike: log line + subtract HP
    uint8_t hit = enemy_effective_damage(enemy_type[slot]);
    uint8_t hp_before = player_hp;
    char logbuf[20];
    uint8_t p = 0, d = hit; // d consumed while formatting digits
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

uint8_t move_enemies(uint8_t px, uint8_t py) { // resolve moves; record strikes — HP applied later in enemy_resolve_hit per hit
    uint8_t perf_stamp = perf_stamp_now();
    uint8_t i;
    uint8_t player_node = nearest_nav_node(px, py);
    uint8_t next_hop_cache[MAX_NAV_NODES];
    for (i = 0; i < MAX_NAV_NODES; i++) next_hop_cache[i] = NAV_NO_LINK;
    enemy_attack_count = 0;
    for (i = 0; i < num_enemies; i++) {
        if (!enemy_alive[i]) continue;

        uint8_t sx = enemy_x[i], sy = enemy_y[i];
        uint8_t nx = sx,         ny = sy; // default no move
        const EnemyDef *def = &enemy_defs[enemy_type[i]];
#if ENEMY_SLEEP_OFFSCREEN
        if (!enemy_force_active[i]) {
            uint8_t dx = (sx > px) ? (uint8_t)(sx - px) : (uint8_t)(px - sx);
            uint8_t dy = (sy > py) ? (uint8_t)(sy - py) : (uint8_t)(py - sy);
            uint8_t md = (uint8_t)(dx + dy);
            if (md > ENEMY_WAKE_MANHATTAN && !lighting_is_revealed(sx, sy)) continue;
        }
#endif

        switch (def->move_style) {
            case MOVE_CHASE:
                step_nav_chase(sx, sy, px, py, player_node, next_hop_cache, &nx, &ny);
                break;
            case MOVE_RANDOM:
                step_random(sx, sy, &nx, &ny);
                break;
            case MOVE_WANDER:
                if (rand() & 1) step_nav_chase(sx, sy, px, py, player_node, next_hop_cache, &nx, &ny);
                else            step_random(sx, sy, &nx, &ny);
                break;
        }

        if (nx == sx && ny == sy)              continue; // AI chose stay
        if (enemy_at(nx, ny) != ENEMY_DEAD)    continue; // don't stack enemies

        if (nx == px && ny == py) { // combat on player's tile — every adjacent step-in can connect same turn
            if (enemy_attack_count < MAX_ENEMIES) enemy_attack_slots[enemy_attack_count++] = i;
            continue; // do not move onto player tile; main applies HP + UI per hit before lunge
        }

        if (!is_walkable(nx, ny)) continue; // wall blocked proposed step

        enemy_clear_slot(sx, sy);
        enemy_x[i] = nx;
        enemy_y[i] = ny;
        if (tile_at(nx, ny) == TILE_PIT) {
            enemy_alive[i] = 0u;
            entity_sprites_enemy_poof_begin(i);
            if (dead_enemy_pool_count < MAX_ENEMIES)
                dead_enemy_pool[dead_enemy_pool_count++] = i;
        } else {
            enemy_place_slot(i, nx, ny);
        }
    }
    perf_record(PERF_ENEMY_MOVE, perf_stamp_elapsed(&perf_stamp));
    return enemy_attack_count ? 1u : 0u;
}
