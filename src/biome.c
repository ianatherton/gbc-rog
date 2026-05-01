#include "biome.h"
#include "globals.h"

EnemyDef enemy_defs[NUM_ENEMY_TYPES]; // HOME storage — biome_load_active fills this
uint8_t  enemy_defs_count = 0u;

BANKREF_EXTERN(biome_dungeon_copy_defs)
BANKREF_EXTERN(biome_crypt_copy_defs)
BANKREF_EXTERN(biome_cavern_copy_defs)

void biome_load_active(uint8_t biome_id) {
    floor_biome = biome_id; // sticky — UI/inspect can read which biome is loaded
    switch (biome_id) {
        case BIOME_CRYPT:  biome_crypt_copy_defs(enemy_defs, &enemy_defs_count);  break;
        case BIOME_CAVERN: biome_cavern_copy_defs(enemy_defs, &enemy_defs_count); break;
        default:           biome_dungeon_copy_defs(enemy_defs, &enemy_defs_count); break;
    }
}

// Floor 1: always dungeon (safe entry + matches no-spawn floor). Floor 2+: pseudo-random biome per floor
// from run_seed so the same seed still reproduces the same sequence without storing prior floors in WRAM.
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed) {
    uint16_t h;
    if (floor_n <= 1u) return BIOME_DUNGEON;
    h = (uint16_t)(seed ^ (uint16_t)((uint16_t)floor_n * 2053u));
    h ^= (uint16_t)(h >> 8);
    h ^= (uint16_t)((uint16_t)floor_n * 6361u);
    h ^= (uint16_t)(h >> 7);
    return (uint8_t)(h % BIOME_COUNT);
}
