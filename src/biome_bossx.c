#pragma bank 24

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "bossx_layout.h"
#include <gb/gb.h>
#include <gb/cgb.h>

// Boss prototypes — the 7 bosses.png creatures beyond gorgon/sphinx, wired as spawnable
// entities with bat-style AI (MOVE_BLINK, blink range 3 — the same style the gorgon and
// sphinx already use) and static frame-1 art. Same bank as bosses_tiles so the art reads
// directly while biome_apply_floor_kind has this bank mapped.
extern const uint8_t bosses_tiles[];

static const uint8_t bossx_pool[BOSSX_POOL_MAX] = BOSSX_POOL_INIT;

// Expand the shared layouts into per-boss source-index lists (entity_sprites.c re-expands
// the same lists into rx/ry offsets — bossx_layout.h is the single source of truth).
#define BOSSX_TILE(s, x, y) s,
static const uint8_t src_hydra[]    = { BOSSX_HYDRA_TILES };
static const uint8_t src_demon[]    = { BOSSX_DEMON_TILES };
static const uint8_t src_gspider[]  = { BOSSX_GSPIDER_TILES };
static const uint8_t src_maraeye[]  = { BOSSX_MARAEYE_TILES };
static const uint8_t src_skelking[] = { BOSSX_SKELKING_TILES };
static const uint8_t src_dragon[]   = { BOSSX_DRAGON_TILES };
static const uint8_t src_mara[]     = { BOSSX_MARA_TILES };
#undef BOSSX_TILE

typedef struct { const uint8_t *src; uint8_t n; } BossArt;
static const BossArt bossx_art[7] = { // indexed by type - ENEMY_HYDRA
    { src_hydra,    (uint8_t)sizeof src_hydra    },
    { src_demon,    (uint8_t)sizeof src_demon    },
    { src_gspider,  (uint8_t)sizeof src_gspider  },
    { src_maraeye,  (uint8_t)sizeof src_maraeye  },
    { src_skelking, (uint8_t)sizeof src_skelking },
    { src_dragon,   (uint8_t)sizeof src_dragon   },
    { src_mara,     (uint8_t)sizeof src_mara     },
};

// One def shared by all prototypes: bat AI (MOVE_BLINK/3), gorgon-class damage. tile/tile_alt
// are unused (custom render branch); base HP is moot while BOSS_HP_TEST_BASE overrides bosses.
static const EnemyDef proto_def = { 0u, 0u, 100, 4, PAL_BOSSX, MOVE_BLINK, 3 };

// Neutral bone/stone ramp — the new sheet art is monochrome; one palette serves all seven.
// Colors map 1:1 onto the art's 2bpp channels (same rule as the sphinx palettes: fix art or
// prep_assets.py, never load-time swaps).
static const palette_color_t pal_bossx[] = {
    RGB(0,  0,  0),   // idx0 transparent for OBJ — unused
    RGB(24, 22, 19),  // pale bone
    RGB(10,  9, 11),  // dark shading
    RGB(31, 30, 28),  // highlight
};

BANKREF(biome_bossx_setup)
void biome_bossx_setup(uint8_t type) { // plain: biome_apply_floor_kind (HOME) maps bank 24 first
    const BossArt *a = &bossx_art[type - ENEMY_HYDRA];
    uint8_t i;
    enemy_defs[type] = proto_def;
    enemy_active_types[0] = type; // roster: only the boss
    enemy_active_count = 1u;
    set_sprite_palette(PAL_BOSSX, 1u, pal_bossx);
    for (i = 0u; i < a->n; i++)
        set_sprite_data(bossx_pool[i], 1u, bosses_tiles + (uint16_t)a->src[i] * 16u);
}
