#ifndef GAMEPLAY_COLD_H
#define GAMEPLAY_COLD_H

#include <stdint.h>
#include <gbdk/platform.h>

// Cold gameplay helpers, evicted from bank 2 to bank 30.
void belt_select_advance_skip_empty(void) BANKED;
void push_selected_belt_description(void) BANKED;

// Per-move zone classifiers (see gameplay_cold.c). Both are pure: no movement, no turn spent.
uint8_t zone_bump_at(uint8_t nx, uint8_t ny, uint8_t t) BANKED;                    // 0 pass / 1 blocked / 2 broke
uint8_t zone_confirm_at(uint8_t nx, uint8_t ny, uint8_t t, uint8_t *aux) BANKED;   // CONFIRM_*

#endif // GAMEPLAY_COLD_H
