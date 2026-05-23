#pragma bank 10

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Dungeon roster — all 6 enemy types.
static const EnemyDef defs[] = {
    /* ENEMY_SNAKE    */ { TILE_SNAKE_1,    TILE_SNAKE_2,    2, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 }, // J5/K5
    /* ENEMY_SLIME    */ { TILE_SLIME_1_OFF, TILE_SLIME_2_OFF, 1, 1, PAL_ENEMY_SNAKE,  MOVE_CHASE,  0 }, // J11/K11
    /* ENEMY_RAT      */ { TILE_RAT_OFF,    TILE_RAT_OFF,    1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J16 flip-anim
    /* ENEMY_BAT      */ { TILE_BAT_1,      TILE_BAT_2,      1, 1, PAL_ENEMY_BAT,      MOVE_BLINK,  3 }, // J2/K2 — blink up to 3 squares
    /* ENEMY_SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, PAL_ENEMY_SKELETON, MOVE_CHASE,  0 }, // J3/J4
    /* ENEMY_IMP      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // J4 flip-anim
};

BANKREF(biome_dungeon_copy_defs)
void biome_dungeon_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) BANKED {
    out[ENEMY_SNAKE]    = defs[0];
    out[ENEMY_SLIME]    = defs[1];
    out[ENEMY_RAT]      = defs[2];
    out[ENEMY_BAT]      = defs[3];
    out[ENEMY_SKELETON] = defs[4];
    out[ENEMY_IMP]      = defs[5];
    out_active[0] = ENEMY_SNAKE;
    out_active[1] = ENEMY_SLIME;
    out_active[2] = ENEMY_RAT;
    out_active[3] = ENEMY_BAT;
    out_active[4] = ENEMY_SKELETON;
    out_active[5] = ENEMY_IMP;
    *out_count = 6;
}
