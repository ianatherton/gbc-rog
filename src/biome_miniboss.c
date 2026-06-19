#pragma bank 27

#include "biome.h"
#include "enemy.h"
#include "defs.h"

// Miniboss roster — fodder is small Slimes (same type as the elite) plus one
// 2x-scaled Slime elite. ENEMY_SLIME_BIG is deliberately absent from out_active:
// enemy.c spawns it once, explicitly, instead of letting it compete with fodder
// in the random roster pick.
static const EnemyDef defs[] = {
    /* ENEMY_SLIME     */ { TILE_SLIME_1_OFF,    TILE_SLIME_2_OFF,    2, 3, PAL_ENEMY_SNAKE, MOVE_CHASE, 0 }, // small — also split-children target
    /* ENEMY_SLIME_BIG */ { TILE_SLIMEBIG_BR_OFF, TILE_SLIMEBIG_BR_OFF, 20, 6, PAL_ENEMY_SNAKE, MOVE_CHASE, 0 }, // tile/tile_alt unused by renderer (custom 2x2 OAM draw); stats here drive HP/dmg only
};

BANKREF(biome_miniboss_copy_defs)
void biome_miniboss_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    out[ENEMY_SLIME]     = defs[0];
    out[ENEMY_SLIME_BIG] = defs[1];
    out_active[0] = ENEMY_SLIME;
    *out_count = 1;
}
