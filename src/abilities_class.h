#ifndef ABILITIES_CLASS_H
#define ABILITIES_CLASS_H

#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Per-class BANKED entry points — each lives in its own purple bank (6-9).
// HOME (ability_dispatch.c) is the ONLY caller; classes never cross-call each other.
// Each writes to *out: consumed_turn / did_kill / kill_x / kill_y.

void ability_knight_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_scoundrel_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_witch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_zerker_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED;

#endif
