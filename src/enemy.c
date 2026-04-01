#include "enemy.h" // AI, spawning, animation tied to DIV_REG
#include "map.h"   // is_walkable, tile_at, nearest_nav_node, nav_next_step, TILE_PIT
#include "ui.h"    // ui_combat_log_push
#include <string.h>

const EnemyDef enemy_defs[NUM_ENEMY_TYPES] = { // one OCP ramp per type; snakes share green only
    /* ENEMY_SERPENT  */ { TILE_SPIDER_1,   TILE_SPIDER_2,   2, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE  },   // J1/J2 green
    /* ENEMY_ADDER    */ { TILE_MONSTER_1,  TILE_MONSTER_1,  1, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE  },   // J3 green
    /* ENEMY_RAT      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  1, 1, PAL_ENEMY_RAT,      MOVE_WANDER },    // J4 red–rose
    /* ENEMY_BAT      */ { TILE_MONSTER_3,  TILE_MONSTER_3,  1, 1, PAL_ENEMY_BAT,      MOVE_RANDOM },   // J5 aqua–turquoise
    /* ENEMY_SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, PAL_ENEMY_SKELETON, MOVE_CHASE  },   // J3/J4 violet
    /* ENEMY_GOBLIN   */ { TILE_MONSTER_2,  TILE_MONSTER_3,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE  },   // J4/J5 magenta
};

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
uint8_t corpse_occ[BITSET_BYTES];

void enemy_grids_init(void) {
    memset(enemy_occ, 0, sizeof enemy_occ);
    memset(corpse_occ, 0, sizeof corpse_occ);
}

static const uint8_t CORPSE_DECO_OFF[2] = { // defs.h L column — one picked per corpse
    TILE_FLOOR_DECO_2, TILE_FLOOR_DECO_3
};

uint8_t enemy_anim_toggle; // flips each ENEMY_ANIM_DIV_TICKS of DIV accumulation
uint8_t enemy_attack_slots[MAX_ENEMIES];
uint8_t enemy_attack_count;

static const char *enemy_type_name(uint8_t t) { // short labels for combat log (must match enemy_defs order)
    static const char *const n[NUM_ENEMY_TYPES] = {
        "SERPENT", "ADDER", "RAT", "BAT", "SKELETON", "GOBLIN"
    };
    return (t < NUM_ENEMY_TYPES) ? n[t] : "?";
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

uint8_t enemy_at(uint8_t x, uint8_t y) { // O(1) when empty (common case); short scan when occupied
    uint16_t idx = TILE_IDX(x, y);
    uint8_t i;
    if (!BIT_GET(enemy_occ, idx)) return ENEMY_DEAD;
    for (i = 0; i < num_enemies; i++)
        if (enemy_x[i] != ENEMY_DEAD && enemy_x[i] == x && enemy_y[i] == y) return i;
    return ENEMY_DEAD;
}

uint8_t corpse_sheet_at(uint8_t x, uint8_t y) { // O(1) when no corpse (common case)
    uint16_t idx = TILE_IDX(x, y);
    uint8_t i;
    if (!BIT_GET(corpse_occ, idx)) return 255;
    for (i = 0; i < num_corpses; i++)
        if (corpse_x[i] == x && corpse_y[i] == y) return corpse_tile[i];
    return 255;
}

uint8_t corpse_at(uint8_t x, uint8_t y) { return corpse_sheet_at(x, y) != 255; }

uint8_t corpse_deco_random(void) { return CORPSE_DECO_OFF[rand() & 1u]; } // L2/L3

void spawn_enemies(void) { // random placement with collision checks
    uint8_t i;
    num_enemies = 0;
    for (i = 0; i < NUM_ENEMIES; i++) {
        uint8_t attempts;
        for (attempts = 0; attempts < 100; attempts++) {
            uint8_t tx = (uint8_t)(rand() & (MAP_W - 1)); // MAP_W is power-of-2
            uint8_t ty = (uint8_t)(rand() & (MAP_H - 1));
            if ((tx != player_spawn_x || ty != player_spawn_y)
                    && is_walkable(tx, ty)
                    && enemy_at(tx, ty) == ENEMY_DEAD) {
                enemy_x[num_enemies]    = tx;
                enemy_y[num_enemies]    = ty;
                enemy_type[num_enemies] = (uint8_t)(rand() % NUM_ENEMY_TYPES);
                enemy_hp[num_enemies]   = enemy_defs[enemy_type[num_enemies]].max_hp;
                BIT_SET(enemy_occ, TILE_IDX(tx, ty));
                num_enemies++;
                break;
            }
        }
    }
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
                             uint8_t px, uint8_t py,
                             uint8_t *nx, uint8_t *ny) { // graph-guided chase with direct fallback
    uint8_t enemy_node  = nearest_nav_node(sx, sy);
    uint8_t player_node = nearest_nav_node(px, py);

    if (enemy_node == NAV_NO_LINK || player_node == NAV_NO_LINK
            || enemy_node == player_node) { // degenerate: same node or missing graph
        step_direct(sx, sy, px, py, nx, ny);
        return;
    }

    uint8_t next_node = nav_next_step(enemy_node, player_node);
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

void enemy_resolve_hit(uint8_t slot) { // one strike: log line + subtract HP
    const EnemyDef *def = &enemy_defs[enemy_type[slot]];
    const char *name = enemy_type_name(enemy_type[slot]);
    char logbuf[24];
    uint8_t p = 0, dmg = def->damage;
    while (*name && p < 18u) logbuf[p++] = *name++;
    logbuf[p++] = ' ';
    logbuf[p++] = '-';
    if (dmg >= 100u) { logbuf[p++] = (char)('0' + dmg / 100u); dmg %= 100u; logbuf[p++] = (char)('0' + dmg / 10u); dmg %= 10u; }
    else if (dmg >= 10u) { logbuf[p++] = (char)('0' + dmg / 10u); dmg %= 10u; }
    logbuf[p++] = (char)('0' + dmg);
    logbuf[p] = 0;
    ui_combat_log_push(logbuf);
    if (player_hp > def->damage) player_hp -= def->damage;
    else                         player_hp  = 0;
}

uint8_t move_enemies(uint8_t px, uint8_t py) { // resolve moves; record strikes — HP applied later in enemy_resolve_hit per hit
    uint8_t i;
    memset(enemy_occ, 0, sizeof enemy_occ); // rebuild from ground truth each turn — guarantees consistency
    for (i = 0; i < num_enemies; i++)
        if (enemy_x[i] != ENEMY_DEAD)
            BIT_SET(enemy_occ, TILE_IDX(enemy_x[i], enemy_y[i]));
    enemy_attack_count = 0;
    for (i = 0; i < num_enemies; i++) {
        if (enemy_x[i] == ENEMY_DEAD) continue;

        uint8_t sx = enemy_x[i], sy = enemy_y[i];
        uint8_t nx = sx,         ny = sy; // default no move
        const EnemyDef *def = &enemy_defs[enemy_type[i]];

        switch (def->move_style) {
            case MOVE_CHASE:
                step_nav_chase(sx, sy, px, py, &nx, &ny);
                break;
            case MOVE_RANDOM:
                step_random(sx, sy, &nx, &ny);
                break;
            case MOVE_WANDER:
                if (rand() & 1) step_nav_chase(sx, sy, px, py, &nx, &ny);
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

        BIT_CLR(enemy_occ, TILE_IDX(sx, sy)); // vacate old cell
        enemy_x[i] = nx;
        enemy_y[i] = ny;
        if (tile_at(nx, ny) == TILE_PIT) {
            enemy_x[i] = ENEMY_DEAD; // fell in pit — remove silently
        } else {
            BIT_SET(enemy_occ, TILE_IDX(nx, ny));
        }
    }
    return enemy_attack_count ? 1u : 0u;
}
