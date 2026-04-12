#ifndef STATES_BANKED_H
#define STATES_BANKED_H

#include <gb/gb.h>

void state_title_enter(void) BANKED;
void state_char_create_enter(void) BANKED;
void state_gameplay_enter(void) BANKED;
void state_gameplay_tick(void) BANKED;
void state_game_over_enter(void) BANKED;
void state_stats_enter(void) BANKED;
void state_stats_tick(void) BANKED;
void state_inventory_enter(void) BANKED;
void state_inventory_tick(void) BANKED;
void state_ability_enter(void) BANKED;
void state_ability_tick(void) BANKED;
void state_transition_enter(void) BANKED;

#endif // STATES_BANKED_H
