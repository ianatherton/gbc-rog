#pragma bank 13

#include "items.h"
#include "globals.h"
#include "ui.h"
#include <string.h>

static const uint8_t kind_tile[ITEM_KIND_COUNT] = {
    TILE_ITEM_3, // POTION — I3
    TILE_ITEM_2, // SCROLL
    TILE_ITEM_3, // KEY
    TILE_ITEM_5, // GEM
};

static const uint8_t kind_pal[ITEM_KIND_COUNT] = {
    PAL_LIFE_UI, // POTION — red ramp
    PAL_XP_UI,   // SCROLL — gold ramp
    PAL_UI,      // KEY    — white/grey
    PAL_LADDER,  // GEM    — warm fire ramp
};

static const char *const kind_name[ITEM_KIND_COUNT] = {
    "POTION", "SCROLL", "KEY", "GEM",
};

uint8_t items_kind_tile(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return 0u;
    return kind_tile[kind];
}

uint8_t items_kind_palette(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return PAL_UI;
    return kind_pal[kind];
}

void items_kind_name_copy(uint8_t kind, char *out, uint8_t cap) BANKED {
    const char *s = (kind < ITEM_KIND_COUNT) ? kind_name[kind] : "?";
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
}

uint8_t inventory_first_empty(void) BANKED {
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++)
        if (inventory_kind[i] == ITEM_KIND_NONE) return i;
    return 255u;
}

uint8_t inventory_count_used(void) BANKED {
    uint8_t i, n = 0u;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++)
        if (inventory_kind[i] != ITEM_KIND_NONE) n++;
    return n;
}

void inventory_clear_all(void) BANKED {
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) inventory_kind[i] = ITEM_KIND_NONE;
}

uint8_t inventory_add(uint8_t kind) BANKED {
    uint8_t s = inventory_first_empty();
    if (s == 255u) return 0u;
    inventory_kind[s] = kind;
    return 1u;
}

void inventory_remove(uint8_t slot) BANKED {
    uint8_t i;
    if (slot >= INVENTORY_MAX_SLOTS) return;
    for (i = slot; (uint8_t)(i + 1u) < INVENTORY_MAX_SLOTS; i++)
        inventory_kind[i] = inventory_kind[(uint8_t)(i + 1u)];
    inventory_kind[INVENTORY_MAX_SLOTS - 1u] = ITEM_KIND_NONE;
}

void items_use_belt(uint8_t item_idx, AbilityResult *out) BANKED {
    uint8_t kind;
    out->consumed_turn = 0u;
    out->did_kill = 0u;
    if (item_idx >= BELT_ITEM_SLOT_COUNT) return;
    kind = inventory_kind[item_idx];
    if (kind == ITEM_KIND_NONE) return;
    {
        char log[20];
        const char *prefix = "Used ";
        char namebuf[10];
        uint8_t i = 0u, k = 0u;
        items_kind_name_copy(kind, namebuf, sizeof namebuf);
        while (prefix[i]) { log[i] = prefix[i]; i++; }
        while (namebuf[k] && i < 19u) { log[i++] = namebuf[k++]; }
        log[i] = 0;
        ui_combat_log_push(log);
    }
    if (kind == ITEM_KIND_POTION) {
        uint8_t heal = 5u;
        if ((uint16_t)player_hp + (uint16_t)heal >= (uint16_t)player_hp_max) player_hp = player_hp_max;
        else player_hp = (uint8_t)(player_hp + heal);
    }
    inventory_remove(item_idx);
    out->consumed_turn = 1u;
}
