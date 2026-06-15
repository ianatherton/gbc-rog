#include "biome.h"
#include "globals.h"

EnemyDef enemy_defs[NUM_ENEMY_TYPES];        // HOME storage — biome_load_active fills this; indexed by type ID
uint8_t  enemy_active_types[NUM_ENEMY_TYPES]; // type IDs present this floor
uint8_t  enemy_active_count;

BANKREF_EXTERN(biome_dungeon_copy_defs)
BANKREF_EXTERN(biome_crypt_copy_defs)
BANKREF_EXTERN(biome_cavern_copy_defs)
BANKREF_EXTERN(biome_boss_copy_defs)
BANKREF_EXTERN(biome_boss_load_palettes)
BANKREF_EXTERN(biome_overworld_copy_defs)
BANKREF_EXTERN(biome_overworld_load_palettes)

// Dispatch table indexed by biome ID — adding a biome is one new bank file plus one row here
// (and BIOME_*/BIOME_COUNT in biome.h). Rows hold plain fn pointers; we map the bank ourselves.
typedef struct { uint8_t bank; BiomeCopyDefsFn copy_defs; BiomeLoadPalettesFn load_palettes; } BiomeEntry;
static const BiomeEntry biome_table[BIOME_COUNT] = {
    /* BIOME_DUNGEON */ { BANK(biome_dungeon_copy_defs), biome_dungeon_copy_defs, NULL },
    /* BIOME_CRYPT   */ { BANK(biome_crypt_copy_defs),   biome_crypt_copy_defs,   NULL },
    /* BIOME_CAVERN  */ { BANK(biome_cavern_copy_defs),  biome_cavern_copy_defs,  NULL },
    /* BIOME_BOSS    */ { BANK(biome_boss_copy_defs),    biome_boss_copy_defs,    biome_boss_load_palettes },
    /* BIOME_OVERWORLD */ { BANK(biome_overworld_copy_defs), biome_overworld_copy_defs, biome_overworld_load_palettes },
};

void biome_load_active(uint8_t biome_id) {
    const BiomeEntry *e;
    uint8_t sb = CURRENT_BANK; // callers can be banked (level_init, bank 10) — restore their bank
    if (biome_id >= BIOME_COUNT) biome_id = BIOME_DUNGEON;
    floor_biome = biome_id; // sticky — UI/inspect can read which biome is loaded
    e = &biome_table[biome_id];
    SWITCH_ROM(e->bank);
    e->copy_defs(enemy_defs, enemy_active_types, &enemy_active_count);
    if (e->load_palettes) e->load_palettes(); // still in e->bank; HOME-bank GBDK fns always reachable
    SWITCH_ROM(sb);
}

// Floor 1: always dungeon. Floor 3: always boss. Other floors: pseudo-random dungeon/crypt/cavern
// from run_seed so the same seed still reproduces the same sequence without storing prior floors in WRAM.
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed) {
    uint16_t h;
    if (floor_n == 0u)             return BIOME_OVERWORLD; // floor 0 is the top-level hub
    if (floor_n <= 1u)             return BIOME_DUNGEON;
    if (floor_n == BOSS_FLOOR_NUM) return BIOME_BOSS;
    h = (uint16_t)(seed ^ (uint16_t)((uint16_t)floor_n * 2053u));
    h ^= (uint16_t)(h >> 8);
    h ^= (uint16_t)((uint16_t)floor_n * 6361u);
    h ^= (uint16_t)(h >> 7);
    return (uint8_t)(h % BIOME_RANDOM_COUNT); // excludes BIOME_BOSS from random selection
}
