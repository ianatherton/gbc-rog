#ifndef BIOME_H
#define BIOME_H

#include "defs.h"
#include "enemy.h"
#include <gbdk/platform.h>

// Coral banks 10/11/12/21 — only HOME (biome.c) ever SWITCH_ROMs into them.
#define BIOME_DUNGEON 0u
#define BIOME_CRYPT   1u
#define BIOME_CAVERN  2u
#define BIOME_BOSS    3u
#define BIOME_OVERWORLD 4u // top-level hub (floor 0); never appears in the random rotation
#define BIOME_MINIBOSS 5u // elite floor: normal fodder roster + one guaranteed 2x Slime; fixed at MINIBOSS_FLOOR_NUM
#define BIOME_COUNT   6u
#define BIOME_RANDOM_COUNT 3u // random floors pick from dungeon/crypt/cavern only (excludes BIOME_BOSS/OVERWORLD/MINIBOSS)

// HOME-resident roster cache; populated by biome_load_active() at floor-gen time.
// enemy.c / entity_sprites.c read this directly without bank switching.
extern EnemyDef enemy_defs[NUM_ENEMY_TYPES];
extern uint8_t  enemy_active_types[NUM_ENEMY_TYPES]; // type IDs available this floor
extern uint8_t  enemy_active_count;

void biome_load_active(uint8_t biome_id); // HOME — fills enemy_defs[] from the biome bank
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed); // HOME — deterministic biome per floor

// Entry points exposed by the per-biome ROM banks. Plain (non-BANKED) so they can sit in the
// dispatch table in biome.c — biome_load_active() SWITCH_ROMs to the entry's bank before the
// call, so they always run with their bank mapped. Never call these directly from elsewhere.
// out is indexed by type ID so enemy_defs[ENEMY_X] always holds ENEMY_X's stats.
// out_active receives the list of type IDs present; out_count receives the list length.
typedef void (*BiomeCopyDefsFn)(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_dungeon_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_cavern_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_boss_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_overworld_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_miniboss_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);

typedef void (*BiomeLoadPalettesFn)(void);
void biome_boss_load_palettes(void); // overrides OCP4 with green+tan ramp for gorgon body/feet
void biome_overworld_load_palettes(void); // dark-green field (BG slot 0 + floor-deco color 0)

// Hub continent water mask (bank 22) — generate_level carves land from it; render draws coast tiles.
// BANKED so they can be called from bank 2 (render) and bank 10 (map_gen) without manual SWITCH_ROM.
BANKREF_EXTERN(overworld_water_at)
BANKREF_EXTERN(overworld_coast_vram)
BANKREF_EXTERN(overworld_is_desert)
BANKREF_EXTERN(overworld_is_snow)
BANKREF_EXTERN(overworld_carve)
void    overworld_carve(void) BANKED;                    // floor-0: fill floor_bits with the landmass (one banked call)
uint8_t overworld_water_at(uint8_t x, uint8_t y) BANKED;  // 1 = water (ocean/river/lake), 0 = land
uint8_t overworld_coast_vram(uint8_t mx, uint8_t my) BANKED; // coast tile for a land cell bordering water, 0 = interior land
uint8_t overworld_is_desert(uint8_t mx, uint8_t my) BANKED;  // hub SE sand region
uint8_t overworld_is_snow(uint8_t mx, uint8_t my) BANKED;    // hub NW snow region

#endif
