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
uint8_t                  gameplay_soft_reenter; // 0 — set by transition only
uint8_t                  level_is_revisit;      // 0 — set in level_init_display from deepest_floor (direction-independent)
uint8_t                  deepest_floor;         // deepest floor reached this run; reset to 1 on new run
uint8_t                  entered_from_below;    // 1 = ascended via stairs-up (spawn at pit); 0 = descend/fresh (spawn at stairs-up)

uint8_t  player_hp  = PLAYER_HP_BASE_MAX;
uint8_t  player_hp_max = PLAYER_HP_BASE_MAX;
uint8_t  player_level = 1;
uint8_t  player_damage = 1; 
uint16_t player_xp;                            // BSS 0 — level_init zeroes on fresh run
uint8_t  player_crit_chance;                   // BSS 0 — level_init zeroes on fresh run
uint8_t  player_armor;                         // BSS 0 — level_init zeroes on fresh run
uint8_t  player_magdef;                        // BSS 0 — level_init zeroes on fresh run
uint8_t  player_dodge;                         // BSS 0 — level_init zeroes on fresh run
uint8_t  floor_num  = 1;
uint16_t run_seed   = 12345;
uint8_t  player_class;                       // 0 = knight
uint8_t  floor_biome;                        // level_init sets before spawn

uint8_t  g_player_x, g_player_y, g_prev_j;
uint16_t g_run_entropy;

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

volatile uint8_t inv_desc_scx; // BSS 0 — see globals.h
