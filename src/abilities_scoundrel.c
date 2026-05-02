#pragma bank 7

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "ally.h"
#include "ui.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(ally_summon_fox)

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

BANKREF(ability_scoundrel_cast_belt)
void ability_scoundrel_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot;
    if (player_level < 1u) return;
    push_short("Call Fox");
    ally_summon_fox(px, py);
    out->consumed_turn = 1u;
}
