#pragma bank 11

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <string.h>

// Crypt roster — undead-leaning subset; expand with crypt-only enemy IDs in a follow-up.
static const EnemyDef defs[] = {
    /* ENEMY_BIG_SKELL */ { TILE_BIG_SKELL_BODY, TILE_BIG_SKELL_BODY, 6, 3, PAL_ENEMY_SKELETON, MOVE_CHASE, 0 }, // J8 body; head drawn separately at J7
    /* ENEMY_IMP       */ { TILE_MONSTER_2,  TILE_MONSTER_2,  2, 2, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // J4 flip-anim
    /* ENEMY_RAT       */ { TILE_RAT_OFF,    TILE_RAT_OFF,    1, 1, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J16 flip-anim
    /* ENEMY_SKELETON  */ { TILE_SKEL_1_OFF, TILE_SKEL_2_OFF, 3, 2, 0,                  MOVE_CHASE,  0 }, // J10/K10 anim; OCP0 white/grey ramp
};

BANKREF(biome_crypt_copy_defs)
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) { // plain: biome.c maps this bank before calling
    out[ENEMY_BIG_SKELL] = defs[0];
    out[ENEMY_IMP]       = defs[1];
    out[ENEMY_RAT]       = defs[2];
    out[ENEMY_SKELETON]  = defs[3];
    out_active[0] = ENEMY_BIG_SKELL;
    out_active[1] = ENEMY_IMP;
    out_active[2] = ENEMY_RAT;
    out_active[3] = ENEMY_SKELETON;
    *out_count = 4;
}
