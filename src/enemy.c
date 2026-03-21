#include "enemy.h"
#include "map.h"

/* ── Enemy definition table ──────────────────────────────────────────────── */
// Fields: glyph, glyph_alt, max_hp, damage, palette, move_style
const EnemyDef enemy_defs[NUM_ENEMY_TYPES] = {
    /* ENEMY_SERPENT  */ { 'S', 's', 2, 1, 1, MOVE_CHASE  },
    /* ENEMY_ADDER    */ { 's', 'S', 1, 1, 0, MOVE_CHASE  },
    /* ENEMY_RAT      */ { 'R', 'r', 1, 1, 0, MOVE_WANDER },
    /* ENEMY_BAT      */ { 'B', 'b', 1, 1, 0, MOVE_RANDOM },
    /* ENEMY_SKELETON */ { 'K', 'k', 3, 2, 0, MOVE_CHASE  },
    /* ENEMY_GOBLIN   */ { 'G', 'g', 2, 2, 1, MOVE_CHASE  },
};

/* ── Instance state ──────────────────────────────────────────────────────── */
uint8_t enemy_x[MAX_ENEMIES];
uint8_t enemy_y[MAX_ENEMIES];
uint8_t enemy_type[MAX_ENEMIES];
uint8_t enemy_hp[MAX_ENEMIES];
uint8_t num_enemies;

uint8_t corpse_x[MAX_CORPSES];
uint8_t corpse_y[MAX_CORPSES];
uint8_t num_corpses;

uint8_t enemy_anim_toggle;

static uint8_t   anim_last_div;
static uint32_t anim_ticks;

void enemy_anim_reset(void) {
    anim_last_div = DIV_REG;
    anim_ticks    = 0;
}

/* Returns 1 when a flip occurred and enemy cells should be redrawn. */
uint8_t enemy_anim_update(void) {
    uint8_t div = DIV_REG;
    anim_ticks += (uint8_t)(div - anim_last_div);
    anim_last_div = div;
    if (anim_ticks >= ENEMY_ANIM_DIV_TICKS) {
        anim_ticks -= ENEMY_ANIM_DIV_TICKS;
        enemy_anim_toggle ^= 1;
        return 1;
    }
    return 0;
}

/* ── Lookup helpers ──────────────────────────────────────────────────────── */

uint8_t enemy_at(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0; i < num_enemies; i++)
        if (enemy_x[i] != ENEMY_DEAD && enemy_x[i] == x && enemy_y[i] == y)
            return i;
    return ENEMY_DEAD;
}

uint8_t corpse_at(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0; i < num_corpses; i++)
        if (corpse_x[i] == x && corpse_y[i] == y) return 1;
    return 0;
}

/* ── Spawning ────────────────────────────────────────────────────────────── */

void spawn_enemies(void) {
    uint8_t i;
    num_enemies = 0;
    for (i = 0; i < NUM_ENEMIES; i++) {
        uint8_t attempts;
        for (attempts = 0; attempts < 100; attempts++) {
            uint8_t tx = (uint8_t)(rand() % MAP_W);
            uint8_t ty = (uint8_t)(rand() % MAP_H);
            if ((tx != START_X || ty != START_Y)
                    && is_walkable(tx, ty)
                    && enemy_at(tx, ty) == ENEMY_DEAD) {
                enemy_x[num_enemies]    = tx;
                enemy_y[num_enemies]    = ty;
                enemy_type[num_enemies] = (uint8_t)(rand() % NUM_ENEMY_TYPES);
                enemy_hp[num_enemies]   = enemy_defs[enemy_type[num_enemies]].max_hp;
                num_enemies++;
                break;
            }
        }
    }
}

/* ── Movement helpers ────────────────────────────────────────────────────── */

// One geometric step from (sx,sy) toward (tx,ty); no nav graph involved.
// Used as: final micro-step inside step_nav_chase, and as a fallback.
static void step_direct(uint8_t sx, uint8_t sy,
                         uint8_t tx, uint8_t ty,
                         uint8_t *nx, uint8_t *ny) {
    int8_t  dx    = 0, dy = 0;
    uint8_t hdist = (tx > sx) ? tx - sx : sx - tx;
    uint8_t vdist = (ty > sy) ? ty - sy : sy - ty;
    if      (sx < tx) dx =  1;
    else if (sx > tx) dx = -1;
    if      (sy < ty) dy =  1;
    else if (sy > ty) dy = -1;
    if (hdist >= vdist) { if (dx) *nx = (uint8_t)((int16_t)sx + dx); }
    else                { if (dy) *ny = (uint8_t)((int16_t)sy + dy); }
}

// Nav-graph chase:
//   1. Find nearest node to enemy and player.
//   2. BFS to get first-hop node toward the player.
//   3. step_direct toward that node's position.
// Falls back to plain step_direct if the graph can't route.
static void step_nav_chase(uint8_t sx, uint8_t sy,
                             uint8_t px, uint8_t py,
                             uint8_t *nx, uint8_t *ny) {
    uint8_t enemy_node  = nearest_nav_node(sx, sy);
    uint8_t player_node = nearest_nav_node(px, py);

    if (enemy_node == NAV_NO_LINK || player_node == NAV_NO_LINK
            || enemy_node == player_node) {
        step_direct(sx, sy, px, py, nx, ny);
        return;
    }

    uint8_t next_node = nav_next_step(enemy_node, player_node);
    if (next_node == NAV_NO_LINK) {
        step_direct(sx, sy, px, py, nx, ny);
        return;
    }

    step_direct(sx, sy, nav_nodes[next_node].x, nav_nodes[next_node].y, nx, ny);
}

// Pick a random walkable neighbouring tile.
static void step_random(uint8_t sx, uint8_t sy,
                         uint8_t *nx, uint8_t *ny) {
    uint8_t attempt;
    for (attempt = 0; attempt < 4; attempt++) {
        uint8_t d  = rand() >> 6; // top 2 bits — GBDK's LCG (×9) has garbage low bits
        uint8_t cx = sx, cy = sy;
        if      (d == 0 && sy > 0)         cy = sy - 1;
        else if (d == 1 && sy < MAP_H - 1) cy = sy + 1;
        else if (d == 2 && sx > 0)         cx = sx - 1;
        else if (d == 3 && sx < MAP_W - 1) cx = sx + 1;
        else continue;
        if (is_walkable(cx, cy)) { *nx = cx; *ny = cy; return; }
    }
}

/* ── AI movement ─────────────────────────────────────────────────────────── */

uint8_t move_enemies(uint8_t px, uint8_t py) {
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (enemy_x[i] == ENEMY_DEAD) continue;

        uint8_t sx = enemy_x[i], sy = enemy_y[i];
        uint8_t nx = sx,         ny = sy;
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

        if (nx == sx && ny == sy)              continue;
        if (enemy_at(nx, ny) != ENEMY_DEAD)    continue;

        if (nx == px && ny == py) {
            if (player_hp > def->damage) player_hp -= def->damage;
            else                         player_hp  = 0;
            return player_hp == 0 ? 2 : 1;
        }

        if (!is_walkable(nx, ny)) continue;

        enemy_x[i] = nx;
        enemy_y[i] = ny;
        if (tile_at(nx, ny) == TILE_PIT) enemy_x[i] = ENEMY_DEAD;
    }
    return 0;
}

