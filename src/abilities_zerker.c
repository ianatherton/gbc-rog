#pragma bank 9

#include "abilities_class.h"
#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Stub — zerker rage / charge / cleave plug in here.
BANKREF(ability_zerker_cast_belt)
void ability_zerker_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot;
    (void)px;
    (void)py;
    (void)out;
}
