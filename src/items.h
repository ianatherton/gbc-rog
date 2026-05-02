#ifndef ITEMS_H
#define ITEMS_H

#include "defs.h"
#include "ability_dispatch.h"
#include <gbdk/platform.h>

#define ITEM_KIND_POTION   0u
#define ITEM_KIND_SCROLL   1u
#define ITEM_KIND_KEY      2u
#define ITEM_KIND_GEM      3u
#define ITEM_KIND_COUNT    4u
#define ITEM_KIND_NONE   255u

#define INVENTORY_MAX_SLOTS  16u // 4x4 grid in STATE_INVENTORY
#define MAX_GROUND_ITEMS      8u // floor-scoped pickup pool

uint8_t items_kind_tile(uint8_t kind) BANKED; // sheet offset; 0 if invalid
uint8_t items_kind_palette(uint8_t kind) BANKED; // CGB palette
void    items_kind_name_copy(uint8_t kind, char *out, uint8_t cap) BANKED; // NUL-term, capped — copy required since strings live in bank 13

uint8_t inventory_first_empty(void) BANKED; // 0..INVENTORY_MAX_SLOTS-1, else 255
uint8_t inventory_count_used(void) BANKED;
void    inventory_clear_all(void) BANKED;
uint8_t inventory_add(uint8_t kind) BANKED; // 1=added, 0=full
void    inventory_remove(uint8_t slot) BANKED; // compact upper slots down so belt slots 0..3 stay synced

void    items_use_belt(uint8_t item_idx, AbilityResult *out) BANKED; // belt slots 4..7 → inventory_kind[0..3]

#endif
