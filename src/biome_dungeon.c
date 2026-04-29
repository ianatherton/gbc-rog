#pragma bank 10

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Dungeon roster — keeps the original 6 enemy types from the pre-biome era.
static const EnemyDef defs[] = {
    /* ENEMY_SERPENT  */ { TILE_SPIDER_1,   TILE_SPIDER_1,   2, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 }, // J1 only — J2 reserved for Bat
    /* ENEMY_ADDER    */ { TILE_MONSTER_1,  TILE_MONSTER_1,  1, 1, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 }, // J3 green
    /* ENEMY_RAT      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J4 red–rose
    /* ENEMY_BAT      */ { TILE_BAT_1,      TILE_BAT_2,      1, 1, PAL_ENEMY_BAT,      MOVE_BLINK,  3 }, // J2/K2 — blink up to 3 squares to land adjacent to player
    /* ENEMY_SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, PAL_ENEMY_SKELETON, MOVE_CHASE,  0 }, // violet
    /* ENEMY_GOBLIN   */ { TILE_MONSTER_2,  TILE_MONSTER_3,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // magenta
};

BANKREF(biome_dungeon_copy_defs)
void biome_dungeon_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED {
    uint8_t n = (uint8_t)(sizeof defs / sizeof defs[0]);
    if (n > NUM_ENEMY_TYPES) n = NUM_ENEMY_TYPES;
    memcpy(out, defs, (uint16_t)n * sizeof(EnemyDef));
    *out_count = n;
}
