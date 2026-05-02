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
extern uint8_t  floor_num;
extern uint16_t run_seed;
extern uint8_t  player_class; // 0=KNIGHT 1=SCOUNDREL 2=WITCH 3=ZERKER (char create)
extern uint8_t  floor_biome;  // BIOME_* — set by level_init before spawn; selects bank 10/11/12 enemy roster

extern uint8_t  g_player_x, g_player_y, g_prev_j;
extern uint16_t g_run_entropy;

extern uint8_t  look_cx, look_cy;
extern uint8_t  selected_belt_slot; // gameplay belt: 0..BELT_SLOT_COUNT-1
extern uint8_t  belt_slot_charges[BELT_SLOT_COUNT]; // uses remaining per slot (0 = hide digit until wired)
extern uint8_t  witch_shot_cooldown_turns;
extern uint8_t  zerker_whirlwind_cooldown_turns;
extern uint8_t  knight_shield_active; // holy fire shield buff — set by ability_knight_cast_belt, cleared on floor gen
extern uint8_t  ally_active[MAX_ALLIES];
extern uint8_t  ally_x[MAX_ALLIES];
extern uint8_t  ally_y[MAX_ALLIES];
extern uint8_t  ally_type[MAX_ALLIES];    // ALLY_TYPE_* — dispatch in ally_fox_* / future ally_* AI
extern uint8_t  ally_chase_ei[MAX_ALLIES]; // ENEMY_DEAD = follow player / wander; else blink+strike that enemy slot

#define MAX_ENEMY_ALIVE_SLOTS MAX_ENEMIES
extern uint8_t enemy_alive[MAX_ENEMIES];
extern uint8_t dead_enemy_pool[MAX_ENEMIES];
extern uint8_t dead_enemy_pool_count;

/* Set by STATE_TRANSITION (pit floor); state_gameplay_enter skips full regen */
extern uint8_t gameplay_soft_reenter;

extern uint8_t inventory_kind[INVENTORY_MAX_SLOTS]; // ITEM_KIND_NONE = empty
extern uint8_t ground_item_kind[MAX_GROUND_ITEMS];   // ITEM_KIND_NONE when slot free
extern uint8_t ground_item_x[MAX_GROUND_ITEMS];
extern uint8_t ground_item_y[MAX_GROUND_ITEMS];
extern uint8_t pending_pickup_slot; // ground_item_* index queued for STATE_PICKUP; 255 = none

#endif // GLOBALS_H
