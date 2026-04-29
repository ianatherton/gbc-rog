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

// Deterministic biome rotation by floor — keeps runs reproducible from run_seed without WRAM history.
// floors 1..3 dungeon, 4..6 crypt, 7..9 cavern, then loop with seed-based offset.
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed) {
    uint16_t band = ((uint16_t)floor_n - 1u) / 3u;
    uint16_t mix = band ^ (seed >> 4);
    return (uint8_t)(mix % BIOME_COUNT);
}
