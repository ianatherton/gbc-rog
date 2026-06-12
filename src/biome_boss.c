#pragma bank 21

#include "biome.h"
#include "enemy.h"
#include "defs.h"

static const EnemyDef defs[] = {
    /* ENEMY_GORGON */ { TILE_GORGON_HEAD_L_OFF, TILE_GORGON_HEAD_R_OFF, 40, 4, PAL_ENEMY_SNAKE, MOVE_CHASE, 0 },
    /* ENEMY_SNAKE  */ { TILE_SNAKE_1,        TILE_SNAKE_2,         4, 2, PAL_ENEMY_SNAKE,    MOVE_CHASE, 0 },
};

BANKREF(biome_boss_copy_defs)
void biome_boss_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    out[ENEMY_GORGON] = defs[0];
    // Snake def needed so enemy_effective_max_hp/damage work for summons
    out[ENEMY_SNAKE]  = defs[1];
    out_active[0] = ENEMY_GORGON; // roster: only Gorgon; snakes appear via summon only
    *out_count = 1u;
}
