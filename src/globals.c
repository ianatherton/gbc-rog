#include "globals.h"
#include "game_state.h"
#include "defs.h"

uint8_t player_light_radius(void) {
    uint8_t base = (player_class == 1u) ? LIGHT_RADIUS_ROGUE
        : (player_class == 2u || player_class == 3u) ? LIGHT_RADIUS_MAGE
        : LIGHT_RADIUS_KNIGHT;
    return (uint8_t)((uint16_t)base + (uint16_t)player_light_bonus);
}

volatile GameState       current_state        = STATE_NONE;
volatile GameState       next_state           = STATE_TITLE; // must not rely on BSS — random WRAM skips title enter
volatile TransitionKind  pending_transition; // 0 = TRANS_NONE — omit ROM slot so .data stays below 0x4000 (BankPack overlap fix)
uint8_t                  pending_port_floor;  // target floor for TRANS_FLOOR_PORT (Witch Port scroll)
uint8_t                  gameplay_soft_reenter; // 0 — set by transition only
uint8_t                  level_is_revisit;      // 0 — set in level_init_display from floor_visited (direction-independent)
uint8_t                  floor_visited[7];      // bit f = floor f generated before; cleared on fresh run (towns use bits 46-48)
uint8_t                  entered_from_below;    // 1 = ascended via stairs-up (spawn at pit); 0 = descend/fresh (spawn at stairs-up)

uint8_t  floor_kind;            // FLOORKIND_* — see dungeon.h
uint8_t  floor_boss_type;       // boss enemy type on FLOORKIND_BOSS floors
uint8_t  elite_base_type;       // base fodder type of the 2x elite on FLOORKIND_MINIBOSS floors
uint16_t dungeon_complete_mask; // bit k = dungeon k complete (level_init clears on fresh run)
uint8_t  hub_landing_dungeon;   // DUNGEON_NONE unless returning from dungeon k

uint8_t  player_hp  = PLAYER_HP_BASE_MAX;
uint8_t  player_hp_max = PLAYER_HP_BASE_MAX;
uint8_t  player_level = 1;
uint8_t  player_damage = 1; 
uint16_t player_xp;                            // BSS 0 — level_init zeroes on fresh run
uint8_t  player_crit_chance;                   // BSS 0 — level_init zeroes on fresh run
uint8_t  player_armor;                         // BSS 0 — level_init zeroes on fresh run
uint8_t  player_magdef;                        // BSS 0 — level_init zeroes on fresh run
uint8_t  player_dodge;                         // BSS 0 — level_init zeroes on fresh run
uint8_t  player_stat_points;                   // BSS 0 — level_init zeroes on fresh run
uint8_t  floor_num  = 1;
uint16_t run_seed   = 12345;
uint8_t  player_class;                       // 0 = knight
uint8_t  floor_biome;                        // level_init sets before spawn
uint8_t  overworld_preset;                   // 0..OVERWORLD_PRESET_COUNT-1; generate_level picks from run_seed on floor 0

uint8_t  g_player_x, g_player_y, g_prev_j;
uint16_t g_run_entropy;
uint8_t  g_sphinx_mode;                      // SPHINX_GROUNDED/FLYING/AWAY — floor-6 boss state machine
uint8_t  sphinx_fire_pending;                // set by move_enemies, consumed by resolve to fire the ranged bolt

uint8_t  look_cx, look_cy;
uint8_t  selected_belt_slot;                 // BSS 0 = slot 0 — gameplay_enter clears belt vars
uint8_t  belt_slot_charges[BELT_SLOT_COUNT];   // BSS zeroed — digits hidden until wired
uint8_t  player_hp_prev;
uint8_t  witch_shot_cooldown_turns;
uint8_t  zerker_whirlwind_cooldown_turns;
uint8_t  book_heal_cooldown_turns;
uint8_t  knight_shield_active;
uint8_t  player_light_bonus;
uint8_t  ally_active[MAX_ALLIES];
uint8_t  ally_x[MAX_ALLIES];
uint8_t  ally_y[MAX_ALLIES];
uint8_t  ally_type[MAX_ALLIES];
uint8_t  ally_chase_ei[MAX_ALLIES];
uint8_t  ally_flip_x[MAX_ALLIES];

uint8_t enemy_alive[MAX_ENEMIES];
uint8_t dead_enemy_pool[MAX_ENEMIES];
uint8_t dead_enemy_pool_count;

uint8_t floor_items_picked[MAX_FLOORS];
uint8_t floor_enemy_dead[MAX_FLOORS * 3u];

// Arrays left uninitialized to keep GSINIT table small (avoid bank 0/1 boundary overflow).
// inventory_clear_all() runs from level_init at fresh-run, and ground_items_clear() runs from level_generate_and_spawn each floor.
// pending_pickup_slot is set to 255 explicitly in those same paths before any read.
uint8_t inventory_kind[INVENTORY_MAX_SLOTS];
uint8_t inventory_equipped[INVENTORY_MAX_SLOTS];
uint8_t inventory_count[INVENTORY_MAX_SLOTS];
int8_t  inventory_mod_level[INVENTORY_MAX_SLOTS];
uint8_t ground_item_kind[MAX_GROUND_ITEMS];
uint8_t ground_item_x[MAX_GROUND_ITEMS];
uint8_t ground_item_y[MAX_GROUND_ITEMS];
int8_t  ground_item_mod_level[MAX_GROUND_ITEMS];
uint8_t pending_pickup_slot;

// Same GSINIT reasoning as the block above: zeroed explicitly by trade_reset_run() (level_init,
// fresh run) rather than with an initializer.
uint8_t player_tokens;
uint8_t town_shop_sold[TOWN_COUNT];
uint8_t pending_talk_npc;
uint8_t town_barrels_broken[TOWN_COUNT * 3u];

volatile uint8_t inv_desc_scx; // BSS 0 — see globals.h

// Overworld prefab dims, indexed by OW_FEAT_*. ent_dx/dy = walkable trigger cell within the footprint
// (bottom-centre for towns/waypoints so you approach the door from below). dest_kind = sub-map id (Part D).
const OwPrefabDef ow_prefab_defs[OW_FEAT_COUNT] = {
    { 3u, 3u, 1u, 2u, OW_FEAT_TOWN },     // TOWN     3x3, door bottom-centre
    { 2u, 2u, 0u, 1u, OW_FEAT_WAYPOINT }, // WAYPOINT 2x2, entrance bottom-left
    { 1u, 1u, 0u, 0u, OW_FEAT_ENTRANCE }, // ENTRANCE 1x1, the cell itself
    { 2u, 2u, 0u, 1u, OW_FEAT_BOSSDOOR }, // BOSSDOOR 2x2, enter from bottom-left
    { 1u, 1u, 0u, 0u, OW_FEAT_SIGNPOST }, // SIGNPOST 1x1, the cell itself is the trigger
    { 1u, 1u, 0u, 0u, OW_FEAT_FOUNTAIN }, // FOUNTAIN 1x1 (town interior), the cell itself is the trigger
    { 1u, 1u, 0u, 0u, OW_FEAT_TREE },     // TREE     1x1 (town interior deco), blocking wall cell
    { 1u, 1u, 0u, 0u, OW_FEAT_BARREL },   // BARREL   1x1 (town interior deco), blocking wall cell
};
OwFeature ow_features[MAX_OW_FEATURES]; // BSS — filled by generate_level on the hub
uint8_t   ow_feature_count;             // BSS 0
