#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include "defs.h"

extern uint8_t  player_hp;
extern uint8_t  player_hp_max;
extern uint8_t  player_level;
extern uint8_t  player_damage;
extern uint16_t player_xp;
extern uint8_t  floor_num;
extern uint16_t run_seed;
extern uint8_t  combat_idle_turns;
extern uint8_t  player_class; // 0=KNIGHT 1=ROGUE 2=MAGE (char create)

extern uint8_t  g_player_x, g_player_y, g_prev_j;
extern uint16_t g_run_entropy;

extern uint8_t  look_cx, look_cy;

#define MAX_ENEMY_ALIVE_SLOTS MAX_ENEMIES
extern uint8_t enemy_alive[MAX_ENEMIES];
extern uint8_t dead_enemy_pool[MAX_ENEMIES];
extern uint8_t dead_enemy_pool_count;

#endif // GLOBALS_H
