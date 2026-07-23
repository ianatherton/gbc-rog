#pragma bank 6

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "items.h"
#include "ui.h"
#include "music.h"
#include <gbdk/platform.h>

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

BANKREF(abilities_knight_new_run_init)
void abilities_knight_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_SHIELD, 0);
}

// rank currently unused: reflect damage scales with player_level (combat.c) either way,
// so the scroll (rank 0) version is "weaker" only in that the reader lacks knight levels.
static void cast_shield(uint8_t rank, AbilityResult *out) {
    (void)rank;
    if (knight_shield_active) {
        push_short("Shield Is Up");
        return; // no consumed_turn — toggling re-cast is free, no double-charge
    }
    knight_shield_active = 1u;
    sfx_shield_sparkle();
    push_short("Holy Fire Shield");
    out->consumed_turn = 1u; // raising the shield counts as a player action
}

BANKREF(ability_knight_cast)
void ability_knight_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)px; (void)py;
    switch (spell_idx) {
        case 0u: cast_shield(rank, out); break;
        default: break; // spells 1..5 not designed yet
    }
}
