#pragma bank 11

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Crypt roster — undead-leaning subset; expand with crypt-only enemy IDs in a follow-up.
static const EnemyDef defs[] = {
    /* SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, PAL_ENEMY_SKELETON, MOVE_CHASE,  0 },
    /* GHOUL    */ { TILE_MONSTER_2,  TILE_MONSTER_3,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 },
    /* RAT      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 },
};

BANKREF(biome_crypt_copy_defs)
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED {
    uint8_t n = (uint8_t)(sizeof defs / sizeof defs[0]);
    if (n > NUM_ENEMY_TYPES) n = NUM_ENEMY_TYPES;
    memcpy(out, defs, (uint16_t)n * sizeof(EnemyDef));
    *out_count = n;
}
