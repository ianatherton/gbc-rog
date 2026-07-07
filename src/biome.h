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
#define BIOME_MINIBOSS 5u // RETIRED as a floor biome — miniboss is FLOORKIND_MINIBOSS now (dungeon.h); id kept for table stability
#define BIOME_BOSS2   6u // Sphinx roster/palette bank row — loaded via biome_apply_floor_kind on boss floors, never a floor biome
#define BIOME_TOWN    7u // town interior (floors 46+, bank 29): safe zone with NPCs + heal fountain
#define BIOME_COUNT   8u
#define BIOME_RANDOM_COUNT 3u // per-dungeon biome picks from dungeon/crypt/cavern only

// HOME-resident roster cache; populated by biome_load_active() at floor-gen time.
// enemy.c / entity_sprites.c read this directly without bank switching.
extern EnemyDef enemy_defs[NUM_ENEMY_TYPES];
extern uint8_t  enemy_active_types[NUM_ENEMY_TYPES]; // type IDs available this floor
extern uint8_t  enemy_active_count;

void biome_load_active(uint8_t biome_id); // HOME — fills enemy_defs[] from the biome bank
uint8_t biome_pick_for_floor(uint8_t floor_n, uint16_t seed); // HOME — deterministic biome per floor
void biome_apply_floor_kind(void); // HOME — miniboss/boss roster+art overlay after biome_load_active (reads floor_kind)

// Miniboss elite art builder (bank 28, dungeon_floors.c): pixel-doubles elite_base_type's two
// frames from the ROM tileset into the quadrant slots (frame 1: 194-198 dead cells; frame 2:
// the Gorgon slots 225-229, restored by biome_load_active's else-branch every floor).
BANKREF_EXTERN(dungeon_elite_load_art)
void dungeon_elite_load_art(void) BANKED;

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
void biome_boss2_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);
void biome_town_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count);

typedef void (*BiomeLoadPalettesFn)(void);
void biome_boss_load_palettes(void); // overrides OCP4 with green+tan ramp for gorgon body/feet
void biome_boss2_load_palettes(void); // OCP4 (PAL_SPHINX_BODY) sphinx ramp
void biome_overworld_load_palettes(void); // dark-green field (BG slot 0 + floor-deco color 0)

// Sphinx boss (bank 24, co-located with bosses.c art). sphinx_load_initial(): reset anim + upload
// frame 0 (called from biome_load_active on floor entry). sphinx_anim_tick(): per gameplay frame on
// BIOME_BOSS2 — two DIV timers re-upload the body/wing tiles (set_sprite_data, in VBlank) for a
// slow leg cycle + faster wingbeat. OAM layout is fixed (entity_sprites), so animation is pure pixel-swap.
BANKREF_EXTERN(sphinx_load_initial)
BANKREF_EXTERN(sphinx_anim_tick)
void sphinx_load_initial(void) BANKED;
void sphinx_anim_tick(void) BANKED;

// sphinx_ai_decide(): per-turn boss AI (bank 24) — advances the 5-grounded / 5-flying cadence,
// updates g_sphinx_mode + sphinx_fire_pending, and returns the movement verb below.
// Keeping the body out of bank 2 (move_enemies) avoids overflowing that chronically-full bank.
#define SPHINX_ACT_GROUNDED 0u  // run the normal MOVE_BLINK chase + melee path
#define SPHINX_ACT_FLY      1u  // flying: caller blinks it toward the player but suppresses melee
BANKREF_EXTERN(sphinx_ai_decide)
uint8_t sphinx_ai_decide(uint8_t sx, uint8_t sy, uint8_t px, uint8_t py) BANKED;

// Hub continent water mask (bank 22) — generate_level carves land from it; render draws coast tiles.
// BANKED so they can be called from bank 2 (render) and bank 10 (map_gen) without manual SWITCH_ROM.
BANKREF_EXTERN(overworld_water_at)
BANKREF_EXTERN(overworld_coast_vram)
BANKREF_EXTERN(overworld_is_desert)
BANKREF_EXTERN(overworld_is_snow)
BANKREF_EXTERN(overworld_carve)
BANKREF_EXTERN(overworld_trigger_at)
BANKREF_EXTERN(overworld_signpost_aux_at)
BANKREF_EXTERN(overworld_signpost_read)
BANKREF_EXTERN(overworld_entrance_id_at)
BANKREF_EXTERN(overworld_place_player_near_entrance)
void    overworld_carve(void) BANKED;                    // floor-0: fill floor_bits with the landmass (one banked call)
uint8_t overworld_trigger_at(uint8_t x, uint8_t y) BANKED; // OW_FEAT_* of the feature whose trigger cell is (x,y), else 255
uint8_t overworld_entrance_id_at(uint8_t x, uint8_t y) BANKED; // dungeon id (entrance ordinal 0..8) at (x,y), else 255
void    overworld_place_player_near_entrance(uint8_t id) BANKED; // hub spawn override: open cell ringing entrance id
uint8_t overworld_signpost_aux_at(uint8_t x, uint8_t y) BANKED; // signpost label code at (x,y), else 255
void    overworld_signpost_read(uint8_t aux) BANKED;     // print the signpost's label to the chat box
uint8_t overworld_water_at(uint8_t x, uint8_t y) BANKED;  // 1 = water (ocean/river/lake), 0 = land
uint8_t overworld_coast_vram(uint8_t mx, uint8_t my) BANKED; // coast tile for a land cell bordering water, 0 = interior land
uint8_t overworld_is_desert(uint8_t mx, uint8_t my) BANKED;  // hub SE sand region
uint8_t overworld_is_snow(uint8_t mx, uint8_t my) BANKED;    // hub NW snow region

// One-call hub cell classifier: folds water/tree/coast/region (+ future prefab features) into a
// single banked entry so render.c does one trampoline per cell instead of 3–4. Returns the VRAM tile
// to draw (0 = interior ground: caller draws its own floor-deco using *region_out for the palette).
// *pal_out is the palette attr for the returned tile (valid only when the return value is non-zero).
// *region_out is always set to OW_REGION_GRASS/DESERT/SNOW. base_tile is tile_at(mx,my).
BANKREF_EXTERN(overworld_cell_render)
uint8_t overworld_cell_render(uint8_t mx, uint8_t my, uint8_t base_tile,
                              uint8_t *pal_out, uint8_t *region_out) BANKED;

// Batched hub strip classifiers: fill render_strip_tiles/attrs for one camera column/row in a single
// banked entry (vs one overworld_cell_render trampoline per cell), fetching water/road mask bits as
// bytes (wram2_read_byte) — one SVBK switch per 8 bits instead of 4-5 per floor cell. Output is
// identical to looping classify_cell over the strip on the hub.
BANKREF_EXTERN(overworld_classify_col_strip)
BANKREF_EXTERN(overworld_classify_row_strip)
void overworld_classify_col_strip(uint8_t mx, uint8_t cam_ty) BANKED;
void overworld_classify_row_strip(uint8_t my, uint8_t cam_tx) BANKED;

// Towns (floors TOWN_FLOOR_BASE+, biome_town.c bank 29). town_generate_interior carves the 20×20
// interior (called by generate_level, bank 10). overworld_town_id_at = OW_FEAT_TOWN ordinal at a
// hub door cell. overworld_step_feature handles walking onto a signpost/NPC (label/dialogue) or a
// town fountain (full heal) — replaces the old two-call signpost hook in state_gameplay.
BANKREF_EXTERN(town_generate_interior)
BANKREF_EXTERN(overworld_town_id_at)
BANKREF_EXTERN(overworld_step_feature)
void    town_generate_interior(uint8_t town_id) BANKED;
uint8_t overworld_town_id_at(uint8_t x, uint8_t y) BANKED;
void    overworld_step_feature(uint8_t x, uint8_t y) BANKED;

// Open-sea animation (bank 22): the whole sea shares one VRAM tile, so rewriting that tile's 16 pixel bytes
// each tick scrolls EVERY water cell at once — O(1), no per-cell map writes. water_anim_tick() runs per
// gameplay frame on the overworld (caller must be in VBlank); water_anim_reset() syncs the timer on entry.
// water_anim_base[] (globals.h) holds the base F10 pixels, captured in main.c while the tileset is paged in.
BANKREF_EXTERN(water_anim_tick)
BANKREF_EXTERN(water_anim_reset)
void water_anim_tick(void) BANKED;
void water_anim_reset(void) BANKED;

#endif
