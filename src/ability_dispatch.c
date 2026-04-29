#include "ability_dispatch.h"
#include "abilities_class.h"
#include "globals.h"
#include <string.h>

BANKREF_EXTERN(ability_knight_cast_belt)
BANKREF_EXTERN(ability_scoundrel_cast_belt)
BANKREF_EXTERN(ability_witch_cast_belt)
BANKREF_EXTERN(ability_zerker_cast_belt)

void ability_dispatch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) {
    memset(out, 0, sizeof *out);
    switch (player_class) {
        case 0u: ability_knight_cast_belt(belt_slot, px, py, out);    break;
        case 1u: ability_scoundrel_cast_belt(belt_slot, px, py, out); break;
        case 2u: ability_witch_cast_belt(belt_slot, px, py, out);     break;
        case 3u: ability_zerker_cast_belt(belt_slot, px, py, out);    break;
        default: break;
    }
}

uint8_t ability_dispatch_belt_ready(uint8_t belt_slot) {
    if (belt_slot != 0u) return 0u; // only slot 0 currently wired across classes
    if (player_class == 0u && player_level >= 1u && !knight_shield_active) return 1u; // knight: ready while shield down
    if (player_class == 2u && player_level >= 1u && witch_shot_cooldown_turns == 0u) return 1u;
    if (player_class == 3u && player_level >= 1u && zerker_whirlwind_cooldown_turns == 0u) return 1u;
    return 0u;
}
