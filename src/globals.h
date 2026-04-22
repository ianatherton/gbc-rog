#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include "defs.h"
#include "game_state.h"

extern uint8_t  player_hp;
extern uint8_t  player_hp_max;
extern uint8_t  player_level;
extern uint8_t  player_damage;
extern uint16_t player_xp;
extern uint8_t  floor_num;
extern uint16_t run_seed;
extern uint8_t  player_class; // 0=KNIGHT 1=SCOUNDREL 2=WITCH 3=ZERKER (char create)

extern uint8_t  g_player_x, g_player_y, g_prev_j;
extern uint16_t g_run_entropy;

extern uint8_t  look_cx, look_cy;
extern uint8_t  selected_belt_slot; // gameplay belt: 0..BELT_SLOT_COUNT-1
extern uint8_t  belt_slot_charges[BELT_SLOT_COUNT]; // uses remaining per slot (0 = hide digit until wired)
extern uint8_t  witch_shot_cooldown_turns;

#define MAX_ENEMY_ALIVE_SLOTS MAX_ENEMIES
extern uint8_t enemy_alive[MAX_ENEMIES];
extern uint8_t dead_enemy_pool[MAX_ENEMIES];
extern uint8_t dead_enemy_pool_count;

/* Set by STATE_TRANSITION (pit floor); state_gameplay_enter skips full regen */
extern uint8_t gameplay_soft_reenter;

#endif // GLOBALS_H
