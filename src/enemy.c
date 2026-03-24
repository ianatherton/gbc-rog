#include "enemy.h" // AI, spawning, animation tied to DIV_REG
#include "map.h"   // is_walkable, tile_at, nearest_nav_node, nav_next_step, TILE_PIT

const EnemyDef enemy_defs[NUM_ENEMY_TYPES] = { // tile_* = defs.h J-col spider / monster art
    /* ENEMY_SERPENT  */ { TILE_SPIDER_1,   TILE_SPIDER_2,   2, 1, 1, MOVE_CHASE  },
    /* ENEMY_ADDER    */ { TILE_MONSTER_1,  TILE_MONSTER_1,  1, 1, 4, MOVE_CHASE  },
    /* ENEMY_RAT      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  1, 1, 5, MOVE_WANDER },
    /* ENEMY_BAT      */ { TILE_MONSTER_3,  TILE_MONSTER_3,  1, 1, 7, MOVE_RANDOM },
    /* ENEMY_SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, 6, MOVE_CHASE  },
    /* ENEMY_GOBLIN   */ { TILE_MONSTER_2,  TILE_MONSTER_3,  2, 2, 2, MOVE_CHASE  },
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

static const uint8_t CORPSE_DECO_OFF[2] = { // defs.h L column — one picked per corpse
    TILE_FLOOR_DECO_2, TILE_FLOOR_DECO_3
};

uint8_t enemy_anim_toggle; // flips each ENEMY_ANIM_DIV_TICKS of DIV accumulation
uint8_t enemy_attack_slot; // set when an enemy hits the player on its turn

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

uint8_t enemy_at(uint8_t x, uint8_t y) { // O(n) scan; n small
    uint8_t i;
    for (i = 0; i < num_enemies; i++)
        if (enemy_x[i] != ENEMY_DEAD && enemy_x[i] == x && enemy_y[i] == y)
            return i;
    return ENEMY_DEAD;
}

uint8_t corpse_sheet_at(uint8_t x, uint8_t y) { // sheet offset for corpse tile at (x,y)
    uint8_t i;
    for (i = 0; i < num_corpses; i++)
        if (corpse_x[i] == x && corpse_y[i] == y) return corpse_tile[i];
    return 255;
}

uint8_t corpse_at(uint8_t x, uint8_t y) { return corpse_sheet_at(x, y) != 255; }

uint8_t corpse_deco_random(void) { return CORPSE_DECO_OFF[(uint8_t)(rand() % 2u)]; } // L2/L3

void spawn_enemies(void) { // random placement with collision checks
    uint8_t i;
    num_enemies = 0;
    for (i = 0; i < NUM_ENEMIES; i++) {
        uint8_t attempts;
        for (attempts = 0; attempts < 100; attempts++) { // try up to 100 random tiles
            uint8_t tx = (uint8_t)(rand() % MAP_W);
            uint8_t ty = (uint8_t)(rand() % MAP_H);
            if ((tx != START_X || ty != START_Y) // not on player
                    && is_walkable(tx, ty)       // open tile
                    && enemy_at(tx, ty) == ENEMY_DEAD) { // no stacking
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

uint8_t move_enemies(uint8_t px, uint8_t py) { // one full enemy phase; return hit/death code
    uint8_t i;
    enemy_attack_slot = ENEMY_DEAD;
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

        if (nx == px && ny == py) { // combat on player's tile
            enemy_attack_slot = i; // lunge anim in main before shake
            if (player_hp > def->damage) player_hp -= def->damage;
            else                         player_hp  = 0;
            return player_hp == 0 ? 2 : 1; // 2 = game over, 1 = damaged
        }

        if (!is_walkable(nx, ny)) continue; // wall blocked proposed step

        enemy_x[i] = nx;
        enemy_y[i] = ny;
        if (tile_at(nx, ny) == TILE_PIT) enemy_x[i] = ENEMY_DEAD; // fell in pit — remove silently
    }
    return 0;
}
