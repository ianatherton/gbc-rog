#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include "defs.h"
#include "game_state.h"
#include "items.h"

extern uint8_t  player_hp;
extern uint8_t  player_hp_max;
extern uint8_t  player_level;
extern uint8_t  player_damage;
extern uint16_t player_xp;
extern uint8_t  player_crit_chance; // 0-100 percent; accumulated from equipment
extern uint8_t  player_armor;   // 0-100 percent; mitigates physical damage taken
extern uint8_t  player_magdef;  // 0-100 percent; mitigates magic damage taken
extern uint8_t  player_dodge;   // 0-100 percent; chance to avoid an incoming hit entirely
extern uint8_t  player_stat_points; // unspent level-up points; spent on the STAT screen
extern uint8_t  floor_num;
extern uint16_t run_seed;
extern uint8_t  player_class; // 0=KNIGHT 1=SCOUNDREL 2=WITCH 3=ZERKER (char create)
extern uint8_t  floor_biome;  // BIOME_* — set by level_init before spawn; selects bank 10/11/12 enemy roster
extern uint8_t  overworld_preset; // 0..OVERWORLD_PRESET_COUNT-1 — hub continent layout, picked from run_seed
extern uint8_t  water_anim_base[16]; // base F10 water tile pixels, snapshotted at boot for the sea-scroll animation
extern uint8_t  boss_alive;   // 1 while the boss (FLOORKIND_BOSS) or 2x elite (FLOORKIND_MINIBOSS) lives; suppresses stairs/pit until cleared

extern uint8_t  g_player_x, g_player_y, g_prev_j;
extern uint8_t  g_sphinx_mode, sphinx_fire_pending;
extern uint16_t g_run_entropy;

extern uint8_t  look_cx, look_cy;
extern uint8_t  selected_belt_slot; // gameplay belt: 0..BELT_SLOT_COUNT-1
extern uint8_t  belt_slot_charges[BELT_SLOT_COUNT]; // uses remaining per slot (0 = hide digit until wired)
extern uint8_t  player_hp_prev;       // HP snapshot before last enemy-hit phase; drives ghost hearts in HUD
extern uint8_t  witch_shot_cooldown_turns;
extern uint8_t  zerker_whirlwind_cooldown_turns;
extern uint8_t  book_heal_cooldown_turns;
extern uint8_t  knight_shield_active; // holy fire shield buff — set by ability_knight_cast_belt, cleared on floor gen
extern uint8_t  player_light_bonus;     // candle stack — cleared on floor gen; added to class base light radius
extern uint8_t  ally_active[MAX_ALLIES];
extern uint8_t  ally_x[MAX_ALLIES];
extern uint8_t  ally_y[MAX_ALLIES];
extern uint8_t  ally_type[MAX_ALLIES];    // ALLY_TYPE_* — dispatch in ally_fox_* / future ally_* AI
extern uint8_t  ally_chase_ei[MAX_ALLIES]; // ENEMY_DEAD = follow player / wander; else blink+strike that enemy slot
extern uint8_t  ally_flip_x[MAX_ALLIES];    // fox OAM S_FLIPX — 1 when last tile step was east (+x); art faces left at 0

#define MAX_ENEMY_ALIVE_SLOTS MAX_ENEMIES
extern uint8_t enemy_alive[MAX_ENEMIES];
extern uint8_t dead_enemy_pool[MAX_ENEMIES];
extern uint8_t dead_enemy_pool_count;

extern uint8_t floor_items_picked[MAX_FLOORS];     // bitmask: bit K = floor-spawned slot K picked up
extern uint8_t floor_enemy_dead[MAX_FLOORS * 3u];  // bitmask: 24 bits per floor, bit K = enemy slot K dead

/* Set by STATE_TRANSITION (pit floor); state_gameplay_enter skips full regen */
extern uint8_t gameplay_soft_reenter;
/* Set before level_generate_and_spawn when entering a previously-visited floor (either direction); triggers permanence restoration */
extern uint8_t level_is_revisit;
extern uint8_t floor_visited[7];    // bitmask, bit f = floor f generated before this run (49 floors used; towns 46-48)
extern uint8_t entered_from_below;  // 1 = arrived via stairs-up (ascend) => spawn at pit; 0 = descend/fresh => spawn at stairs-up
extern uint8_t pending_port_floor;  // target floor for TRANS_FLOOR_PORT (Witch's Port scroll warps here)

/* ── Per-dungeon state (see dungeon.h for the floor-number scheme) ── */
extern uint8_t  floor_kind;            // FLOORKIND_* — set alongside floor_biome each floor load
extern uint8_t  floor_boss_type;       // ENEMY_GORGON or ENEMY_SPHINX on FLOORKIND_BOSS floors
extern uint8_t  elite_base_type;       // fodder type the 2x elite was built from (FLOORKIND_MINIBOSS)
extern uint16_t dungeon_complete_mask; // bit k = dungeon k boss beaten + exited; entrance sealed
extern uint8_t  hub_landing_dungeon;   // DUNGEON_NONE = spawn as usual; else land beside entrance k on next hub gen

extern uint8_t inventory_kind[INVENTORY_MAX_SLOTS];     // ITEM_KIND_NONE = empty
extern uint8_t inventory_equipped[INVENTORY_MAX_SLOTS]; // 1=equipped, 0=not; parallel to inventory_kind
extern uint8_t inventory_count[INVENTORY_MAX_SLOTS];    // stack depth; 0 when slot empty, 1+ when occupied
extern int8_t  inventory_mod_level[INVENTORY_MAX_SLOTS]; // -1..+10 "+N" equipment modifier; 0/meaningless for non-equipment kinds
extern uint8_t ground_item_kind[MAX_GROUND_ITEMS];   // ITEM_KIND_NONE when slot free
extern uint8_t ground_item_x[MAX_GROUND_ITEMS];
extern uint8_t ground_item_y[MAX_GROUND_ITEMS];
extern int8_t  ground_item_mod_level[MAX_GROUND_ITEMS]; // parallel to ground_item_kind; rolled at drop/scatter time
extern uint8_t pending_pickup_slot; // ground_item_* index queued for STATE_PICKUP; 255 = none

/* story_ui layout constants — scratch overlays floor_bits[] before first generate_level (see story_ui.c) */
#define G_STORY_BIGBUF_CAP   400u
#define G_STORY_MAX_LINES    40u // wrap + blank lines can exceed 28; tail was dropped from story_line_off[]
#define G_STORY_FIRE_COUNT   18u

// ── Overworld prefab features (towns/waypoints/entrances) ──────────────────────────────────────
// Kept in HOME (globals.c) so map_gen (bank 10, places them) and biome_overworld (bank 22, draws/
// classifies them) both read the tables with no bank switch.
typedef struct { uint8_t w, h, ent_dx, ent_dy, dest_kind; } OwPrefabDef; // dims + entrance cell + sub-map id
typedef struct { uint8_t x, y, type, aux; } OwFeature;                   // top-left tile + OW_FEAT_* id; aux = signpost label code (SIGN_KIND_*)
extern const OwPrefabDef ow_prefab_defs[OW_FEAT_COUNT]; // indexed by OW_FEAT_* type
extern OwFeature ow_features[MAX_OW_FEATURES];          // placed this floor (hub only)
extern uint8_t   ow_feature_count;                      // 0 on non-hub floors

uint8_t player_light_radius(void); // class base + player_light_bonus (HOME)

/* Inventory desc-row smooth scroll: ISR reads this to set SCX on scanlines 120-127 only. */
extern volatile uint8_t inv_desc_scx; // 0 = no override; 1-7 = sub-tile pixel shift

#endif // GLOBALS_H
