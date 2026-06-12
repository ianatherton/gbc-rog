#pragma bank 21

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <gb/cgb.h>

static const EnemyDef defs[] = {
    /* ENEMY_GORGON */ { TILE_GORGON_HEAD_L_OFF, TILE_GORGON_HEAD_R_OFF, 40, 4, PAL_ENEMY_SNAKE, MOVE_BLINK, 3 },
    /* ENEMY_SNAKE  */ { TILE_SNAKE_1,        TILE_SNAKE_2,         4, 2, PAL_ENEMY_SNAKE,    MOVE_CHASE, 0 },
};

static const palette_color_t pal_gorgon_body[] = {
    RGB(0,  0,  0),   // outline / shadow
    RGB(20, 14,  6),  // tan / warm skin
    RGB(0,  20,  4),  // green / scale
    RGB(28, 22, 14),  // tan highlight
};

BANKREF(biome_boss_load_palettes)
void biome_boss_load_palettes(void) {
    set_sprite_palette(PAL_GORGON_BODY, 1u, pal_gorgon_body);
}

BANKREF(biome_boss_copy_defs)
void biome_boss_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    out[ENEMY_GORGON] = defs[0];
    // Snake def needed so enemy_effective_max_hp/damage work for summons
    out[ENEMY_SNAKE]  = defs[1];
    out_active[0] = ENEMY_GORGON; // roster: only Gorgon; snakes appear via summon only
    *out_count = 1u;
}
