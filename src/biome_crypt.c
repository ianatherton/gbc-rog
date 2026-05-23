#pragma bank 11

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Crypt roster — undead-leaning subset; expand with crypt-only enemy IDs in a follow-up.
static const EnemyDef defs[] = {
    /* ENEMY_SKELETON */ { TILE_MONSTER_1,  TILE_MONSTER_2,  3, 2, PAL_ENEMY_SKELETON, MOVE_CHASE,  0 }, // J3/J4
    /* ENEMY_IMP      */ { TILE_MONSTER_2,  TILE_MONSTER_2,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // J4 flip-anim
    /* ENEMY_RAT      */ { TILE_RAT_OFF,    TILE_RAT_OFF,    1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J16 flip-anim
};

BANKREF(biome_crypt_copy_defs)
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) BANKED {
    out[ENEMY_SKELETON] = defs[0];
    out[ENEMY_IMP]      = defs[1];
    out[ENEMY_RAT]      = defs[2];
    out_active[0] = ENEMY_SKELETON;
    out_active[1] = ENEMY_IMP;
    out_active[2] = ENEMY_RAT;
    *out_count = 3;
}
