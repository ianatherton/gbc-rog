#ifndef ITEMS_H
#define ITEMS_H

#include "defs.h"
#include <gbdk/platform.h>

// Coral bank 13 — ground items / pickup tables. HOME mediates all access.
typedef struct {
    uint8_t tile;     // sheet offset
    uint8_t palette;  // CGB palette
    uint8_t kind;     // 0=potion 1=scroll 2=key (placeholder ids)
} ItemDef;

#define ITEMS_MAX_DEFS 8u

void items_copy_defs(ItemDef *out, uint8_t *out_count) BANKED;

#endif
