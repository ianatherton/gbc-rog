#ifndef BOW_SHOOT_H
#define BOW_SHOOT_H

#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Fires a single arrow at the nearest visible enemy (like the witch bolt, one bolt).
// Damage = (player_damage / 2, rounded up) + player_crit_chance, then the crit roll;
// Sniper Mode makes the first term full player_damage and extends reach 4→8 tiles.
// Sets out->consumed_turn=1 only if an arrow was actually loosed — callers spend a
// stack item only when consumed_turn is set, so a wasted shot keeps the arrow.
void bow_shoot_use(AbilityResult *out) BANKED;

// Wand: same contract as the bow, but launches the witch's fetid-bolt tile, scales off
// player_magdef instead of crit, and never benefits from Sniper Mode. A fizzle keeps the
// charge and the turn.
void wand_shoot_use(AbilityResult *out) BANKED;

#endif // BOW_SHOOT_H
