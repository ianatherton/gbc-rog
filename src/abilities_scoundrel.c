#pragma bank 7

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "ally.h"
#include "items.h"
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

BANKREF(abilities_scoundrel_new_run_init)
void abilities_scoundrel_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_BOW, 0);
    inventory_add(ITEM_KIND_BOW, 0); // stacks → 40 arrows total
}

// rank currently unused — fox stats are fixed; scroll (rank 0) summons the same fox.
static void cast_call_fox(uint8_t px, uint8_t py, AbilityResult *out) {
    push_short("Call Fox");
    ally_summon_fox(px, py);
    out->consumed_turn = 1u;
}

BANKREF(ability_scoundrel_cast)
void ability_scoundrel_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)rank;
    switch (spell_idx) {
        case 0u: cast_call_fox(px, py, out); break;
        default: break; // spells 1..5 not designed yet
    }
}
