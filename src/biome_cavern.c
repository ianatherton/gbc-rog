#pragma bank 12

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Cavern roster — bats + serpents + rats; expand with cavern-unique creatures later.
static const EnemyDef defs[] = {
    /* BAT     */ { TILE_BAT_1,      TILE_BAT_2,      1, 1, PAL_ENEMY_BAT,   MOVE_BLINK,  3 }, // J2/K2 — blink up to 3 squares
    /* SERPENT */ { TILE_SPIDER_1,   TILE_SPIDER_1,   2, 1, PAL_ENEMY_SNAKE, MOVE_CHASE,  0 }, // J1 only — J2 reserved for Bat
    /* RAT     */ { TILE_MONSTER_2,  TILE_MONSTER_2,  1, 1, PAL_ENEMY_RAT,   MOVE_WANDER, 0 },
    /* ADDER   */ { TILE_MONSTER_1,  TILE_MONSTER_1,  1, 1, PAL_ENEMY_SNAKE, MOVE_CHASE,  0 },
};

BANKREF(biome_cavern_copy_defs)
void biome_cavern_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED {
    uint8_t n = (uint8_t)(sizeof defs / sizeof defs[0]);
    if (n > NUM_ENEMY_TYPES) n = NUM_ENEMY_TYPES;
    memcpy(out, defs, (uint16_t)n * sizeof(EnemyDef));
    *out_count = n;
}
