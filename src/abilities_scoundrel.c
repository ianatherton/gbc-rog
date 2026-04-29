#pragma bank 7

#include "abilities_class.h"
#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Stub — scoundrel daggers / smokebombs / lockpicks land here in subsequent PRs.
BANKREF(ability_scoundrel_cast_belt)
void ability_scoundrel_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot;
    (void)px;
    (void)py;
    (void)out;
}
