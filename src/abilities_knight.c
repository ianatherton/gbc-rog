#pragma bank 6

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "ui.h"
#include <gbdk/platform.h>

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

BANKREF(ability_knight_cast_belt)
void ability_knight_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot; (void)px; (void)py;
    if (player_level < 1u) return;
    if (knight_shield_active) {
        push_short("shield is up");
        return; // no consumed_turn — toggling re-cast is free, no double-charge
    }
    knight_shield_active = 1u;
    push_short("holy fire shield");
    out->consumed_turn = 1u; // raising the shield counts as a player action
}
