#include "biome.h"
#include "globals.h"
#include "tileset.h"
#include <gb/gb.h>

EnemyDef enemy_defs[NUM_ENEMY_TYPES];        // HOME storage — biome_load_active fills this; indexed by type ID
uint8_t  enemy_active_types[NUM_ENEMY_TYPES]; // type IDs present this floor
uint8_t  enemy_active_count;

BANKREF_EXTERN(enemies_miniboss)
extern const uint8_t enemies_miniboss_tiles[]; // bank 27 — 8 big-slime quadrant tiles: f1 TL/TR/BL/BR, f2 TL/TR/BL/BR

BANKREF_EXTERN(biome_dungeon_copy_defs)
BANKREF_EXTERN(biome_crypt_copy_defs)
BANKREF_EXTERN(biome_cavern_copy_defs)
BANKREF_EXTERN(biome_boss_copy_defs)
BANKREF_EXTERN(biome_boss_load_palettes)
BANKREF_EXTERN(biome_overworld_copy_defs)
BANKREF_EXTERN(biome_overworld_load_palettes)
BANKREF_EXTERN(biome_miniboss_copy_defs)

// Dispatch table indexed by biome ID — adding a biome is one new bank file plus one row here
// (and BIOME_*/BIOME_COUNT in biome.h). Rows hold plain fn pointers; we map the bank ourselves.
typedef struct { uint8_t bank; BiomeCopyDefsFn copy_defs; BiomeLoadPalettesFn load_palettes; } BiomeEntry;
static const BiomeEntry biome_table[BIOME_COUNT] = {
    /* BIOME_DUNGEON */ { BANK(biome_dungeon_copy_defs), biome_dungeon_copy_defs, NULL },
    /* BIOME_CRYPT   */ { BANK(biome_crypt_copy_defs),   biome_crypt_copy_defs,   NULL },
    /* BIOME_CAVERN  */ { BANK(biome_cavern_copy_defs),  biome_cavern_copy_defs,  NULL },
    /* BIOME_BOSS    */ { BANK(biome_boss_copy_defs),    biome_boss_copy_defs,    biome_boss_load_palettes },
    /* BIOME_OVERWORLD */ { BANK(biome_overworld_copy_defs), biome_overworld_copy_defs, biome_overworld_load_palettes },
    /* BIOME_MINIBOSS */ { BANK(biome_miniboss_copy_defs), biome_miniboss_copy_defs, NULL },
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
    // Per-biome enemy art: upload the active biome's sprite sheet into the shared scratch slots.
    // (Scaffold: only BIOME_MINIBOSS uses this path; other biomes still rely on the permanent
    // boot-loaded slots in main.c. Migrating a biome here lets its enemy art be load/unloaded
    // per floor — see docs/BANKS.md.) The big-slime's 8 quadrant tiles come from bank 27's
    // enemies_miniboss.png, NOT the shared tileset. Frame 1 goes to 4 dead background cells
    // (194/195/196/198, no restore needed); frame 2 borrows Skeleton/Rat/BigSkell slots, which
    // are free on this floor (none of those spawn here) and get restored on every other floor.
    if (biome_id == BIOME_MINIBOSS) {
        SWITCH_ROM(BANK(enemies_miniboss));
        set_sprite_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_TL_OFF), 1u, enemies_miniboss_tiles + 0u * 16u);
        set_sprite_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_TR_OFF), 1u, enemies_miniboss_tiles + 1u * 16u);
        set_sprite_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_BL_OFF), 1u, enemies_miniboss_tiles + 2u * 16u);
        set_sprite_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_BR_OFF), 1u, enemies_miniboss_tiles + 3u * 16u);
        set_sprite_data(TILE_SKEL_1_VRAM,         1u, enemies_miniboss_tiles + 4u * 16u);
        set_sprite_data(TILE_SKEL_2_VRAM,         1u, enemies_miniboss_tiles + 5u * 16u);
        set_sprite_data(TILE_RAT_VRAM,            1u, enemies_miniboss_tiles + 6u * 16u);
        set_sprite_data(TILE_BIG_SKELL_BODY_VRAM, 1u, enemies_miniboss_tiles + 7u * 16u);
        // Small slimes spawn here too; the hub's prefab waypoint stomps their VRAM (217/218), so restore
        // it from the shared tileset (big-slime's own quadrants overwrite the 194-198 town cells above).
        SWITCH_ROM(BANK(tileset));
        set_sprite_data(TILE_SLIME_1_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SLIME_ROM_1 * 16u);
        set_sprite_data(TILE_SLIME_2_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SLIME_ROM_2 * 16u);
    } else if (biome_id == BIOME_OVERWORLD) {
        // Hub draws no enemies, so the 8 coast tiles borrow enemy OBJ slots as BG tiles. Order must
        // match the COAST_VRAM_* aliases in defs.h. The dungeon floors below restore the enemy art.
        SWITCH_ROM(BANK(tileset));
        set_bkg_data(COAST_VRAM_NW, 1u, tileset_tiles + (uint16_t)TILE_COAST_D11 * 16u);
        set_bkg_data(COAST_VRAM_N,  1u, tileset_tiles + (uint16_t)TILE_COAST_E11 * 16u);
        set_bkg_data(COAST_VRAM_NA, 1u, tileset_tiles + (uint16_t)TILE_COAST_F11 * 16u);
        set_bkg_data(COAST_VRAM_NE, 1u, tileset_tiles + (uint16_t)TILE_COAST_G11 * 16u);
        set_bkg_data(COAST_VRAM_SW, 1u, tileset_tiles + (uint16_t)TILE_COAST_D12 * 16u);
        set_bkg_data(COAST_VRAM_S,  1u, tileset_tiles + (uint16_t)TILE_COAST_E12 * 16u);
        set_bkg_data(COAST_VRAM_SA, 1u, tileset_tiles + (uint16_t)TILE_COAST_F12 * 16u);
        set_bkg_data(COAST_VRAM_SE, 1u, tileset_tiles + (uint16_t)TILE_COAST_G12 * 16u);
        // Prefab feature art (towns/waypoints/entrances) into idle hub OBJ slots — see defs.h PREFAB_VRAM_*.
        set_bkg_data(PREFAB_VRAM_ENTRANCE,     1u, tileset_tiles + (uint16_t)TILE_PREFAB_ENTRANCE_D9 * 16u);
        set_bkg_data(PREFAB_VRAM_TOWN_WALL_EW, 1u, tileset_tiles + (uint16_t)TILE_PREFAB_TOWN_WALL_EW * 16u);
        set_bkg_data(PREFAB_VRAM_TOWN_CORNER,  1u, tileset_tiles + (uint16_t)TILE_PREFAB_TOWN_CORNER  * 16u);
        set_bkg_data(PREFAB_VRAM_TOWN_WALL_NS, 1u, tileset_tiles + (uint16_t)TILE_PREFAB_TOWN_WALL_NS * 16u);
        set_bkg_data(PREFAB_VRAM_WP_TL,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_WP_TL * 16u);
        set_bkg_data(PREFAB_VRAM_WP_TR,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_WP_TR * 16u);
        set_bkg_data(PREFAB_VRAM_WP_BL,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_WP_BL * 16u);
        set_bkg_data(PREFAB_VRAM_WP_BR,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_WP_BR * 16u);
    } else {
        SWITCH_ROM(BANK(tileset));
        set_sprite_data(TILE_SKEL_1_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SKEL_ROM_1 * 16u);
        set_sprite_data(TILE_SKEL_2_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SKEL_ROM_2 * 16u);
        set_sprite_data(TILE_RAT_VRAM,    1u, tileset_tiles + (uint16_t)TILE_RAT_ROM * 16u);
        set_sprite_data(TILE_BIG_SKELL_BODY_VRAM, 1u, tileset_tiles + (uint16_t)TILE_BIG_SKELL_BODY_ROM * 16u);
        // Restore gorgon slots too — the hub's coast tiles stomp these (COAST_VRAM_SW/S/SA/SE).
        set_sprite_data(TILE_GORGON_HEAD_L_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_HEAD_L_ROM * 16u);
        set_sprite_data(TILE_GORGON_HEAD_R_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_HEAD_R_ROM * 16u);
        set_sprite_data(TILE_GORGON_BODY_L_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_BODY_L_ROM * 16u);
        set_sprite_data(TILE_GORGON_BODY_R_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_BODY_R_ROM * 16u);
        // Restore the enemy art the hub's prefab tiles stomp: gorgon feet (drawn on the boss floor, which
        // takes this branch) and the small slime (drawn on dungeon/cavern; miniboss restores it too).
        set_sprite_data(TILE_GORGON_FEET_L_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_FEET_L_ROM * 16u);
        set_sprite_data(TILE_GORGON_FEET_R_VRAM, 1u, tileset_tiles + (uint16_t)TILE_GORGON_FEET_R_ROM * 16u);
        set_sprite_data(TILE_SLIME_1_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SLIME_ROM_1 * 16u);
        set_sprite_data(TILE_SLIME_2_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SLIME_ROM_2 * 16u);
    }
    SWITCH_ROM(sb);
}

// Floor 1: always dungeon. Floor 3: always miniboss. Floor 5: always boss. Other floors:
// pseudo-random dungeon/crypt/cavern from run_seed so the same seed still reproduces the
// same sequence without storing prior floors in WRAM.
static const uint8_t random_biomes[BIOME_RANDOM_COUNT] = { BIOME_DUNGEON, BIOME_CRYPT, BIOME_CAVERN };
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed) {
    uint16_t h;
    if (floor_n == 0u)                 return BIOME_OVERWORLD; // floor 0 is the top-level hub
    if (floor_n <= 1u)                 return BIOME_DUNGEON;
    if (floor_n == MINIBOSS_FLOOR_NUM) return BIOME_MINIBOSS;
    if (floor_n == BOSS_FLOOR_NUM)     return BIOME_BOSS;
    h = (uint16_t)(seed ^ (uint16_t)((uint16_t)floor_n * 2053u));
    h ^= (uint16_t)(h >> 8);
    h ^= (uint16_t)((uint16_t)floor_n * 6361u);
    h ^= (uint16_t)(h >> 7);
    return random_biomes[h % BIOME_RANDOM_COUNT]; // excludes BIOME_BOSS/OVERWORLD/MINIBOSS from random selection
}
