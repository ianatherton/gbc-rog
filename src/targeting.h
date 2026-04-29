#ifndef TARGETING_H
#define TARGETING_H

#include <stdint.h>

// HOME-resident targeting helpers — purple ability banks (6-9) call these without
// triggering a bank switch (HOME is always mapped). Inputs are tile coords.

// Find the closest revealed live enemy within max_range (Chebyshev / king-move).
// Returns 1 if a target was found and writes *out_slot/*out_tx/*out_ty.
// Sets *out_too_far=1 if any live revealed enemy exists but is beyond max_range.
uint8_t targeting_find_nearest_visible(uint8_t px, uint8_t py, uint8_t max_range,
                                       uint8_t *out_slot,
                                       uint8_t *out_tx, uint8_t *out_ty,
                                       uint8_t *out_too_far);

#endif
