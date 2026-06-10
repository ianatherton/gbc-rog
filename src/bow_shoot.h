#ifndef BOW_SHOOT_H
#define BOW_SHOOT_H

#include "ability_dispatch.h"
#include <gbdk/platform.h>

// Fires a single arrow at the nearest visible enemy (like the witch bolt, one bolt).
// Sets out->consumed_turn=1 only if an arrow was actually loosed — callers spend a
// stack item only when consumed_turn is set, so a wasted shot keeps the arrow.
void bow_shoot_use(AbilityResult *out) BANKED;

#endif // BOW_SHOOT_H
