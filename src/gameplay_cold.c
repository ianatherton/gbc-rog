#pragma bank 30

// Cold gameplay helpers evicted from bank 2 (chronically full — docs/BANKS.md).
// Everything here runs on a SELECT edge only, so the far-call cost is irrelevant.

#include <gbdk/platform.h>
#include <stdint.h>
#include "defs.h"
#include "globals.h"
#include "items.h"
#include "ui.h"
#include "ability_dispatch.h"
#include "gameplay_cold.h"

// Strings live in HOME (ability_dispatch.c) — always mapped, safe to pass across banks.
static const char *belt_name_for(uint8_t slot) {
    if (slot == 1u && player_class == 2u && player_level >= 3u) return ability_name_swamp_root;
    if (slot != 0u) return 0;
    if (player_class == 0u) return ability_name_holy_fire_shield;
    if (player_class == 1u) return ability_name_call_fox;
    if (player_class == 2u) return ability_name_fetid_bolt;
    if (player_class == 3u) return ability_name_whirlwind;
    return 0;
}

static uint8_t belt_slot_nonempty(uint8_t slot) {
    if (slot < BELT_SLOT_COUNT) return belt_name_for(slot) != 0;
    {
        uint8_t kind = inventory_kind[slot - BELT_SLOT_COUNT];
        return kind != ITEM_KIND_NONE;
    }
}

BANKREF(belt_select_advance_skip_empty)
void belt_select_advance_skip_empty(void) BANKED {
    uint8_t i;
    for (i = 0u; i < BELT_TOTAL_SLOTS; i++) {
        selected_belt_slot = (uint8_t)((selected_belt_slot + 1u) % BELT_TOTAL_SLOTS);
        if (belt_slot_nonempty(selected_belt_slot)) break;
    }
}

BANKREF(push_selected_belt_description)
void push_selected_belt_description(void) BANKED {
    char buf[20];
    uint8_t i = 0u;
    if (selected_belt_slot < BELT_SLOT_COUNT) {
        const char *name = belt_name_for(selected_belt_slot);
        if (!name) return;
        while (name[i] && i < 19u) { buf[i] = name[i]; i++; }
        buf[i] = 0;
        ui_combat_log_push(buf);
    } else {
        uint8_t belt_idx = selected_belt_slot - BELT_SLOT_COUNT;
        uint8_t kind = inventory_kind[belt_idx];
        if (kind == ITEM_KIND_NONE) return;
        items_kind_display_name_copy(kind, inventory_mod_level[belt_idx], buf, sizeof buf);
        if (items_kind_category(kind) == ITEM_CAT_CONSUMABLE) {
            uint8_t p = 0u, split, cnt = inventory_count[selected_belt_slot - BELT_SLOT_COUNT];
            while (buf[p]) p++;
            split = (uint8_t)(p + 1u); // "x" starts after the space
            buf[p++] = ' '; buf[p++] = 'x';
            if (cnt >= 100u) { buf[p++] = (char)('0' + cnt / 100u); cnt = (uint8_t)(cnt % 100u); }
            if (cnt >= 10u)  { buf[p++] = (char)('0' + cnt / 10u);  cnt = (uint8_t)(cnt % 10u);  }
            buf[p++] = (char)('0' + cnt);
            buf[p] = 0;
            ui_combat_log_push_gold_suffix(buf, split);
        } else {
            ui_combat_log_push(buf);
        }
    }
}
