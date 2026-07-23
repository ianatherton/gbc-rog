#ifndef ABILITIES_CLASS_H
#define ABILITIES_CLASS_H

#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Per-class BANKED entry points — each lives in its own purple bank (6-9).
// HOME (ability_dispatch.c) is the ONLY caller; classes never cross-call each other.
// spell_idx = local idx 0..SPELLS_PER_CLASS-1; rank 1..max = trained strength,
// rank 0 = GENERIC-SCROLL strength (weaker numbers, castable by any class).
// Pure effect code: no cooldown/level gating in here (HOME centralizes it) —
// each just writes *out: consumed_turn / did_kill / kill_x / kill_y.

void ability_knight_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_scoundrel_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_witch_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED;
void ability_zerker_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED;

void abilities_knight_new_run_init(void) BANKED;
void abilities_scoundrel_new_run_init(void) BANKED;
void abilities_witch_new_run_init(void) BANKED;
void abilities_zerker_new_run_init(void) BANKED;

#endif
