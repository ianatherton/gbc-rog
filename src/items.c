#pragma bank 13

#include "items.h"
#include <string.h>

// Placeholder item table — replace once item drop / belt-equip system is wired.
static const ItemDef defs[] = {
    /* potion */ { TILE_ITEM_1, PAL_LIFE_UI, 0u },
    /* scroll */ { TILE_ITEM_2, PAL_XP_UI,   1u },
    /* key    */ { TILE_ITEM_3, PAL_UI,      2u },
};

BANKREF(items_copy_defs)
void items_copy_defs(ItemDef *out, uint8_t *out_count) BANKED {
    uint8_t n = (uint8_t)(sizeof defs / sizeof defs[0]);
    if (n > ITEMS_MAX_DEFS) n = ITEMS_MAX_DEFS;
    memcpy(out, defs, (uint16_t)n * sizeof(ItemDef));
    *out_count = n;
}
