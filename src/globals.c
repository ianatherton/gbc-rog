#include "globals.h"
#include "game_state.h"

volatile GameState       current_state        = STATE_NONE;
volatile GameState       next_state           = STATE_TITLE;
volatile TransitionKind  pending_transition   = TRANS_NONE;
uint8_t                  gameplay_soft_reenter = 0u;

uint8_t  player_hp  = PLAYER_HP_BASE_MAX;
uint8_t  player_hp_max = PLAYER_HP_BASE_MAX;
uint8_t  player_level = 1;
uint8_t  player_damage = 1;
uint16_t player_xp  = 0;
uint8_t  floor_num  = 1;
uint16_t run_seed   = 12345;
uint8_t  player_class = 0;

uint8_t  g_player_x, g_player_y, g_prev_j;
uint16_t g_run_entropy;

uint8_t  look_cx, look_cy;
uint8_t  selected_belt_slot = 0u;
uint8_t  belt_slot_charges[BELT_SLOT_COUNT] = { 0 };

uint8_t enemy_alive[MAX_ENEMIES];
uint8_t dead_enemy_pool[MAX_ENEMIES];
uint8_t dead_enemy_pool_count;
