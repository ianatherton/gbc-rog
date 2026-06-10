#pragma bank 10

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Dungeon roster — all 7 enemy types.
static const EnemyDef defs[] = {
    /* ENEMY_SNAKE     */ { TILE_SNAKE_1,       TILE_SNAKE_2,       2, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 }, // J5/K5
    /* ENEMY_SLIME     */ { TILE_SLIME_1_OFF,   TILE_SLIME_2_OFF,   1, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 }, // J11/K11
    /* ENEMY_RAT       */ { TILE_RAT_OFF,       TILE_RAT_OFF,       1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J16 flip-anim
    /* ENEMY_BAT       */ { TILE_BAT_1,         TILE_BAT_2,         1, 1, PAL_ENEMY_BAT,      MOVE_BLINK,  3 }, // J2/K2 — blink up to 3 squares
    /* ENEMY_BIG_SKELL */ { TILE_BIG_SKELL_BODY, TILE_BIG_SKELL_BODY, 6, 3, PAL_ENEMY_SKELETON, MOVE_CHASE, 0 }, // J8 body; head drawn separately at J7
    /* ENEMY_IMP       */ { TILE_MONSTER_2,     TILE_MONSTER_2,     2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // J4 flip-anim
    /* ENEMY_SKELETON  */ { TILE_SKEL_1_OFF,    TILE_SKEL_2_OFF,    3, 2, 0,                  MOVE_CHASE,  0 }, // J10/K10 anim; OCP0 white/grey ramp
};

BANKREF(biome_dungeon_copy_defs)
void biome_dungeon_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) { // plain: biome.c maps this bank before calling
    out[ENEMY_SNAKE]     = defs[0];
    out[ENEMY_SLIME]     = defs[1];
    out[ENEMY_RAT]       = defs[2];
    out[ENEMY_BAT]       = defs[3];
    out[ENEMY_BIG_SKELL] = defs[4];
    out[ENEMY_IMP]       = defs[5];
    out[ENEMY_SKELETON]  = defs[6];
    out_active[0] = ENEMY_SNAKE;
    out_active[1] = ENEMY_SLIME;
    out_active[2] = ENEMY_RAT;
    out_active[3] = ENEMY_BAT;
    out_active[4] = ENEMY_BIG_SKELL;
    out_active[5] = ENEMY_IMP;
    out_active[6] = ENEMY_SKELETON;
    *out_count = 7;
}
