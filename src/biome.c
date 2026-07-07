#include "biome.h"
#include "dungeon.h"
#include "globals.h"
#include "tileset.h"
#include <gb/gb.h>

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
BANKREF_EXTERN(biome_boss2_copy_defs)
BANKREF_EXTERN(biome_boss2_load_palettes)
BANKREF_EXTERN(biome_town_copy_defs)

// Dispatch table indexed by biome ID — adding a biome is one new bank file plus one row here
// (and BIOME_*/BIOME_COUNT in biome.h). Rows hold plain fn pointers; we map the bank ourselves.
typedef struct { uint8_t bank; BiomeCopyDefsFn copy_defs; BiomeLoadPalettesFn load_palettes; } BiomeEntry;
static const BiomeEntry biome_table[BIOME_COUNT] = {
    /* BIOME_DUNGEON */ { BANK(biome_dungeon_copy_defs), biome_dungeon_copy_defs, NULL },
    /* BIOME_CRYPT   */ { BANK(biome_crypt_copy_defs),   biome_crypt_copy_defs,   NULL },
    /* BIOME_CAVERN  */ { BANK(biome_cavern_copy_defs),  biome_cavern_copy_defs,  NULL },
    /* BIOME_BOSS    */ { BANK(biome_boss_copy_defs),    biome_boss_copy_defs,    biome_boss_load_palettes },
    /* BIOME_OVERWORLD */ { BANK(biome_overworld_copy_defs), biome_overworld_copy_defs, biome_overworld_load_palettes },
    /* BIOME_MINIBOSS (retired — kind now; row kept for index stability) */ { BANK(biome_dungeon_copy_defs), biome_dungeon_copy_defs, NULL },
    /* BIOME_BOSS2   */ { BANK(biome_boss2_copy_defs),   biome_boss2_copy_defs,   biome_boss2_load_palettes },
    /* BIOME_TOWN    */ { BANK(biome_town_copy_defs),    biome_town_copy_defs,    NULL },
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
    // Per-biome enemy art. The old BIOME_MINIBOSS upload branch is gone: the elite's 2x art is
    // now built at runtime by dungeon_elite_load_art (bank 28), invoked from biome_apply_floor_kind
    // on FLOORKIND_MINIBOSS floors — miniboss/boss are floor kinds, not biomes (dungeon.h).
    if (biome_id == BIOME_OVERWORLD) {
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
        // Final-dungeon boss door into 4 permanently-free row-7 OBJ slots — no per-floor restore needed.
        set_bkg_data(PREFAB_VRAM_DOOR_TL,      1u, tileset_tiles + (uint16_t)TILE_PREFAB_DOOR_TL * 16u);
        set_bkg_data(PREFAB_VRAM_DOOR_TR,      1u, tileset_tiles + (uint16_t)TILE_PREFAB_DOOR_TR * 16u);
        set_bkg_data(PREFAB_VRAM_DOOR_BL,      1u, tileset_tiles + (uint16_t)TILE_PREFAB_DOOR_BL * 16u);
        set_bkg_data(PREFAB_VRAM_DOOR_BR,      1u, tileset_tiles + (uint16_t)TILE_PREFAB_DOOR_BR * 16u);
        // Snow-biome mountains (B9/C9) borrow the stun/root overlay slots — restored on the non-hub branch.
        set_bkg_data(PREFAB_VRAM_MTN_L,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_MTN_L * 16u);
        set_bkg_data(PREFAB_VRAM_MTN_R,        1u, tileset_tiles + (uint16_t)TILE_PREFAB_MTN_R * 16u);
    } else if (biome_id == BIOME_BOSS2) {
        // Sphinx: upload frame-0 art into the 10 scratch slots (gorgon + skel/rat/big-skull slots,
        // free here). sphinx_anim_tick re-uploads per frame; the else-branch restores those slots
        // on every other floor. BANKED call — its trampoline maps bank 24 (bosses art + code).
        sphinx_load_initial();
        // Upload the stun glyph — the flying Sphinx flings it as its ranged bolt. VRAM 197 is not one
        // of the Sphinx's 10 borrowed slots, but this branch skips the else-branch that normally
        // restores it, so re-upload it explicitly to be safe.
        SWITCH_ROM(BANK(tileset));
        set_sprite_data(TILE_STUN_ICON_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_M13 * 16u);
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
        // Restore the stun/root overlay art the hub's snow mountains stomp (PREFAB_VRAM_MTN_L/R = 197/242).
        set_sprite_data(TILE_STUN_ICON_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_M13 * 16u);
        set_sprite_data(TILE_ROOT_ICON_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_L11 * 16u);
        // Restore the enemy death-poof art the hub's boss door stomps (PREFAB_VRAM_DOOR_BR = M7 = VRAM 236).
        set_sprite_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_POOF_CLOUD), 1u, tileset_tiles + (uint16_t)TILE_POOF_CLOUD * 16u);
    }
    // 2-tile hero art (all biomes). Uploaded here, not at boot, because the title-logo VRAM restore stomps
    // slots 129/145/161 (they're in title_logo_bkg_vram_slot[]) back to the ROM class glyphs when leaving
    // the title. K13/K14/K15/K12 are sheet rows 12–15 (> first-128 upload) → copy from ROM each floor.
    SWITCH_ROM(BANK(tileset));
    set_sprite_data(TILE_PLAYER_BODY_STAND_VRAM,  1u, tileset_tiles + (uint16_t)TILE_SHEET_K14 * 16u);
    set_sprite_data(TILE_PLAYER_BODY_STRIDE_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_K15 * 16u);
    set_sprite_data(TILE_PLAYER_HEAD_VRAM,        1u, tileset_tiles + (uint16_t)TILE_SHEET_K13 * 16u);
    set_sprite_data(TILE_PLAYER_HELMET_VRAM,      1u, tileset_tiles + (uint16_t)TILE_SHEET_HELMET1 * 16u);
    SWITCH_ROM(sb);
}

// ── Floor-kind overlay (runs right after biome_load_active in level_generate_and_spawn) ────────
// Miniboss/boss floors keep the dungeon biome's tileset/walls; only the roster defs (plus Sphinx
// art) are overlaid. HOME on purpose: this SWITCH_ROMs into the boss banks, which a banked caller
// can't do without unmapping itself. Gorgon OBJ art needs no upload here — biome_load_active's
// else-branch just restored slots 225-231 from ROM (that ordering is load-bearing).
// All picks hash from (run_seed, dungeon id) — never rand(), or floor layouts would shift.
void biome_apply_floor_kind(void) {
    uint8_t sb = CURRENT_BANK;
    uint8_t d = FLOOR_DUNGEON_ID(floor_num);
    if (floor_kind == FLOORKIND_BOSS) {
        uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(d + 1u) * 7919u));
        h ^= (uint16_t)(h >> 5);
        floor_boss_type = (h & 1u) ? ENEMY_SPHINX : ENEMY_GORGON;
        if (floor_boss_type == ENEMY_GORGON) {
            SWITCH_ROM(BANK(biome_boss_copy_defs));
            biome_boss_copy_defs(enemy_defs, enemy_active_types, &enemy_active_count);
            biome_boss_load_palettes();
        } else {
            SWITCH_ROM(BANK(biome_boss2_copy_defs));
            biome_boss2_copy_defs(enemy_defs, enemy_active_types, &enemy_active_count);
            biome_boss2_load_palettes();
            sphinx_load_initial(); // BANKED (bank 24) — frame-0 art into the 10 borrowed scratch slots
        }
    } else if (floor_kind == FLOORKIND_MINIBOSS) {
        // Elite = a random fodder type from this biome's roster, scaled up. ENEMY_BIG_SKELL is
        // excluded: its overlay head shares OAM 27-29 with the elite's 2x2 quadrant sprites.
        uint8_t list[NUM_ENEMY_TYPES];
        uint8_t n = 0u, i;
        uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(d + 3u) * 4241u));
        h ^= (uint16_t)(h >> 6);
        for (i = 0u; i < enemy_active_count; i++)
            if (enemy_active_types[i] != ENEMY_BIG_SKELL) list[n++] = enemy_active_types[i];
        if (n) {
            uint16_t hp6;
            elite_base_type = list[h % n];
            enemy_defs[ENEMY_SLIME_BIG] = enemy_defs[elite_base_type]; // palette et al. inherit from the base
            hp6 = (uint16_t)((uint16_t)enemy_defs[elite_base_type].max_hp * 6u);
            enemy_defs[ENEMY_SLIME_BIG].max_hp = (hp6 > 255u) ? 255u : (uint8_t)hp6;
            enemy_defs[ENEMY_SLIME_BIG].damage = (uint8_t)(enemy_defs[elite_base_type].damage * 2u);
            enemy_defs[ENEMY_SLIME_BIG].move_style = MOVE_CHASE;
            dungeon_elite_load_art(); // BANKED (bank 28): 2x-upscaled base sprite into the quadrant slots
        }
    }
    SWITCH_ROM(sb);
}

// One biome per dungeon: hashed from (run_seed, dungeon id) so every floor of dungeon k —
// guardroom through boss — shares the same tileset/roster. Miniboss/boss are floor KINDS
// (dungeon.h), not biomes; BIOME_MINIBOSS/BOSS/BOSS2 are never returned here anymore.
static const uint8_t random_biomes[BIOME_RANDOM_COUNT] = { BIOME_DUNGEON, BIOME_CRYPT, BIOME_CAVERN };
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed) {
    uint16_t h;
    uint8_t d;
    if (floor_n == 0u) return BIOME_OVERWORLD; // floor 0 is the top-level hub
    if (floor_n >= TOWN_FLOOR_BASE) return BIOME_TOWN; // town interiors (46+)
    d = FLOOR_DUNGEON_ID(floor_n);
    h = (uint16_t)(seed ^ (uint16_t)((uint16_t)d * 2053u));
    h ^= (uint16_t)(h >> 8);
    h ^= (uint16_t)((uint16_t)(d + 1u) * 6361u);
    h ^= (uint16_t)(h >> 7);
    return random_biomes[h % BIOME_RANDOM_COUNT];
}
