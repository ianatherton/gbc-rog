#ifndef BIOME_H
#define BIOME_H

#include "defs.h"
#include "enemy.h"
#include <gbdk/platform.h>

// Coral banks 10/11/12 — only HOME (biome.c) ever SWITCH_ROMs into them.
#define BIOME_DUNGEON 0u
#define BIOME_CRYPT   1u
#define BIOME_CAVERN  2u
#define BIOME_COUNT   3u

// HOME-resident roster cache; populated by biome_load_active() at floor-gen time.
// enemy.c / entity_sprites.c read this directly without bank switching.
extern EnemyDef enemy_defs[NUM_ENEMY_TYPES];
extern uint8_t  enemy_defs_count;

void biome_load_active(uint8_t biome_id); // HOME — fills enemy_defs[] from the biome bank
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed); // HOME — deterministic biome per floor

// BANKED entry points exposed by the per-biome ROM banks. These run while their bank is mapped
// and copy the const roster into the HOME-side enemy_defs[] cache before returning.
void biome_dungeon_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED;
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED;
void biome_cavern_copy_defs(EnemyDef *out, uint8_t *out_count) BANKED;

#endif
