#pragma bank 12

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Cavern roster — bats + snakes + slimes + rats.
static const EnemyDef defs[] = {
    /* ENEMY_BAT   */ { TILE_BAT_1,      TILE_BAT_2,      1, 1, PAL_ENEMY_BAT,   MOVE_BLINK,  3 }, // J2/K2 — blink up to 3 squares
    /* ENEMY_SNAKE */ { TILE_SNAKE_1,    TILE_SNAKE_2,    2, 1, PAL_ENEMY_SNAKE, MOVE_CHASE,  0 }, // J5/K5
    /* ENEMY_RAT   */ { TILE_RAT_OFF,    TILE_RAT_OFF,    1, 1, PAL_ENEMY_RAT,   MOVE_WANDER, 0 }, // J16 flip-anim
    /* ENEMY_SLIME */ { TILE_SLIME_1_OFF, TILE_SLIME_2_OFF, 1, 1, PAL_ENEMY_SNAKE, MOVE_CHASE, 0 }, // J11/K11
};

BANKREF(biome_cavern_copy_defs)
void biome_cavern_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) { // plain: biome.c maps this bank before calling
    out[ENEMY_BAT]   = defs[0];
    out[ENEMY_SNAKE] = defs[1];
    out[ENEMY_RAT]   = defs[2];
    out[ENEMY_SLIME] = defs[3];
    out_active[0] = ENEMY_BAT;
    out_active[1] = ENEMY_SNAKE;
    out_active[2] = ENEMY_RAT;
    out_active[3] = ENEMY_SLIME;
    *out_count = 4;
}
