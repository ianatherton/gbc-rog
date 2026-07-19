#ifndef GAMEPLAY_COLD_H
#define GAMEPLAY_COLD_H

#include <stdint.h>
#include <gbdk/platform.h>

// Cold (SELECT-edge-only) gameplay helpers, evicted from bank 2 to bank 30.
void belt_select_advance_skip_empty(void) BANKED;
void push_selected_belt_description(void) BANKED;

#endif // GAMEPLAY_COLD_H
