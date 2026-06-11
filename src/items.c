#pragma bank 13

#include "items.h"
#include "globals.h"
#include "ui.h"
#include "scroll_blast.h"
#include "scroll_root.h"
#include "bow_shoot.h"
#include <string.h>

BANKREF_EXTERN(scroll_blast_use)
BANKREF_EXTERN(scroll_root_use)
BANKREF_EXTERN(bow_shoot_use)

static const uint8_t kind_cat[ITEM_KIND_COUNT] = {
    ITEM_CAT_CONSUMABLE, // POTION
    ITEM_CAT_CONSUMABLE, // SCROLL (Death)
    ITEM_CAT_CONSUMABLE, // KEY (BigHeal)
    ITEM_CAT_CONSUMABLE, // CANDLE
    ITEM_CAT_CONSUMABLE, // SCROLL_ROOT
    ITEM_CAT_EQUIPMENT,  // RUSTY_SWORD
    ITEM_CAT_REUSABLE,   // BOOK_HEAL
    ITEM_CAT_EQUIPMENT,  // HELMET
    ITEM_CAT_EQUIPMENT,  // TUNIC
    ITEM_CAT_EQUIPMENT,  // BOOTS
    ITEM_CAT_CONSUMABLE, // BOW — stack item, one arrow spent per shot
    ITEM_CAT_EQUIPMENT,  // AXE
    ITEM_CAT_EQUIPMENT,  // SHIELD
};

static const uint8_t kind_tile[ITEM_KIND_COUNT] = {
    TILE_ITEM_3,          // POTION
    TILE_SCROLL_BELT_OFF, // SCROLL (Death) — I11 art at TILE_SCROLL_I11_VRAM
    TILE_BIGHEAL_BELT_OFF,// KEY — BigHeal; I12 at TILE_BIGHEAL_I12_VRAM
    TILE_LIGHT_6,         // CANDLE
    TILE_SCROLL_BELT_OFF, // SCROLL_ROOT — same scroll art as Death Scroll
    TILE_ITEM_1,          // RUSTY_SWORD — I1
    TILE_BOOK_BELT_OFF,   // BOOK_HEAL — H11 art at TILE_BOOK_H11_VRAM
    TILE_ITEM_5,          // HELMET — I5
    TILE_ITEM_6,          // TUNIC — I6
    TILE_ITEM_7,          // BOOTS — I7
    TILE_BOW_BELT_OFF,    // BOW — I15 art at TILE_BOW_VRAM
    TILE_AXE_BELT_OFF,    // AXE — I10 art; VRAM slot shared with Zerker Whirlwind belt icon
    TILE_SHIELD_BELT_OFF, // SHIELD — I9 art; VRAM slot shared with Knight Shield UI icon
};

static const uint8_t kind_pal[ITEM_KIND_COUNT] = {
    PAL_LIFE_UI,      // POTION
    PAL_XP_UI,        // SCROLL (Death)
    PAL_LIFE_UI,      // KEY
    PAL_LADDER,       // CANDLE
    PAL_ENEMY_SNAKE,  // SCROLL_ROOT — green
    PAL_WALL_BG,      // RUSTY_SWORD — warm brownish ramp
    PAL_XP_UI,        // BOOK_HEAL — blue
    PAL_WALL_BG,      // HELMET — warm metal ramp
    PAL_WALL_BG,      // TUNIC — warm metal ramp
    PAL_LADDER,       // BOOTS — earthy/leather
    PAL_LADDER,       // BOW — torch ramp
    PAL_WALL_BG,      // AXE — warm metal ramp
    PAL_WALL_BG,      // SHIELD — warm metal ramp
};

static const char *const kind_name[ITEM_KIND_COUNT] = {
    "Heal Potion", "Death Scroll", "BigHeal Potion", "Candle", "Root Scroll",
    "Rusty Sword", "Book: Heal", "Helmet", "Tunic", "Boots", "Bow & Arrow",
    "Axe", "Shield",
};

static const char *const kind_desc[ITEM_KIND_COUNT] = {
    "+50% HP. A small vial of crimson liquid.",
    "Blasts all in sight. An arcane scroll.",
    "Restores full HP. A rare golden elixir.",
    "+3 light radius. A stubby wax candle.",
    "Roots all enemies 12 turns. Mossy parchment.",
    "+4 attack. A nicked blade, still sharp.",
    "+25% HP. A dog-eared tome of healing.",
    "+5 max HP. A battered iron helmet.",
    "+10 max HP. Heavy rings of chainmail.",
    "+2 light radius. Worn leather boots.",
    "Looses an arrow at the nearest foe.",
    "Cleaves up to 2 nearby foes on attack.",
    "+10 max HP. A battered iron shield.",
};

uint8_t items_kind_category(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return ITEM_CAT_CONSUMABLE;
    return kind_cat[kind];
}

uint8_t items_kind_tile(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return 0u;
    return kind_tile[kind];
}

uint8_t items_kind_palette(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return PAL_UI;
    return kind_pal[kind];
}

void items_kind_desc_copy(uint8_t kind, char *out, uint8_t cap) BANKED {
    const char *s = (kind < ITEM_KIND_COUNT) ? kind_desc[kind] : "";
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
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
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
        inventory_kind[i]     = ITEM_KIND_NONE;
        inventory_equipped[i] = 0u;
        inventory_count[i]    = 0u;
    }
}

uint8_t inventory_add(uint8_t kind) BANKED {
    uint8_t s;
    uint8_t start = (items_kind_category(kind) == ITEM_CAT_EQUIPMENT) ? BELT_ITEM_SLOT_COUNT : 0u;
    /* bow picks up a full quiver; everything else is a single unit */
    uint8_t qty = (kind == ITEM_KIND_BOW) ? ITEM_BOW_STACK_QTY : 1u;
    /* consumables stack — merge into existing slot of same kind first */
    if (items_kind_category(kind) == ITEM_CAT_CONSUMABLE) {
        for (s = 0u; s < INVENTORY_MAX_SLOTS; s++) {
            if (inventory_kind[s] == kind) {
                uint16_t nc = (uint16_t)inventory_count[s] + qty;
                inventory_count[s] = (nc > 255u) ? 255u : (uint8_t)nc;
                return 1u;
            }
        }
    }
    for (s = start; s < INVENTORY_MAX_SLOTS; s++) {
        if (inventory_kind[s] == ITEM_KIND_NONE) {
            inventory_kind[s]  = kind;
            inventory_count[s] = qty;
            return 1u;
        }
    }
    return 0u;
}

void inventory_remove(uint8_t slot) BANKED {
    uint8_t i, j;
    if (slot >= INVENTORY_MAX_SLOTS) return;
    /* stacked: just decrement, no slot free needed */
    if (inventory_count[slot] > 1u) { inventory_count[slot]--; return; }

    for (i = slot; (uint8_t)(i + 1u) < INVENTORY_MAX_SLOTS; i++) {
        inventory_kind[i]     = inventory_kind[(uint8_t)(i + 1u)];
        inventory_equipped[i] = inventory_equipped[(uint8_t)(i + 1u)];
        inventory_count[i]    = inventory_count[(uint8_t)(i + 1u)];
    }
    inventory_kind[INVENTORY_MAX_SLOTS - 1u]     = ITEM_KIND_NONE;
    inventory_equipped[INVENTORY_MAX_SLOTS - 1u] = 0u;
    inventory_count[INVENTORY_MAX_SLOTS - 1u]    = 0u;

    /* Removing a belt slot compacts everything left, which can pull equipment
       from slot BELT_ITEM_SLOT_COUNT into the last belt slot.  Fix it. */
    if (slot < BELT_ITEM_SLOT_COUNT) {
        uint8_t lb = (uint8_t)(BELT_ITEM_SLOT_COUNT - 1u);
        if (inventory_kind[lb] != ITEM_KIND_NONE &&
            items_kind_category(inventory_kind[lb]) == ITEM_CAT_EQUIPMENT) {
            /* swap with the first usable (non-equipment) item beyond the belt */
            for (j = BELT_ITEM_SLOT_COUNT; j < INVENTORY_MAX_SLOTS; j++) {
                if (inventory_kind[j] != ITEM_KIND_NONE &&
                    items_kind_category(inventory_kind[j]) != ITEM_CAT_EQUIPMENT) {
                    uint8_t tk = inventory_kind[j], te = inventory_equipped[j], tc = inventory_count[j];
                    inventory_kind[j]     = inventory_kind[lb];
                    inventory_equipped[j] = inventory_equipped[lb];
                    inventory_count[j]    = inventory_count[lb];
                    inventory_kind[lb]    = tk;
                    inventory_equipped[lb]= te;
                    inventory_count[lb]   = tc;
                    break;
                }
            }
            if (j >= INVENTORY_MAX_SLOTS) {
                /* no usable items remain — push equipment to first empty non-belt slot */
                for (j = BELT_ITEM_SLOT_COUNT; j < INVENTORY_MAX_SLOTS; j++) {
                    if (inventory_kind[j] == ITEM_KIND_NONE) {
                        inventory_kind[j]     = inventory_kind[lb];
                        inventory_equipped[j] = inventory_equipped[lb];
                        inventory_count[j]    = inventory_count[lb];
                        inventory_kind[lb]     = ITEM_KIND_NONE;
                        inventory_equipped[lb] = 0u;
                        inventory_count[lb]    = 0u;
                        break;
                    }
                }
                if (j >= INVENTORY_MAX_SLOTS) {
                    inventory_kind[lb]     = ITEM_KIND_NONE;
                    inventory_equipped[lb] = 0u;
                    inventory_count[lb]    = 0u;
                }
            }
        }
    }
}

void items_use_belt(uint8_t item_idx, AbilityResult *out) BANKED {
    uint8_t kind;
    out->consumed_turn = 0u;
    out->did_kill = 0u;
    out->lighting_refresh = 0u;
    if (item_idx >= BELT_ITEM_SLOT_COUNT) return;
    kind = inventory_kind[item_idx];
    if (kind == ITEM_KIND_NONE) return;
    if (kind == ITEM_KIND_BOW) {
        /* fires its own projectile + logs its own result; only spend an arrow on a real shot */
        bow_shoot_use(out);
        if (out->consumed_turn) inventory_remove(item_idx);
        return;
    }
    {
        char log[20];
        const char *prefix = "Used ";
        char namebuf[18];
        uint8_t i = 0u, k = 0u;
        items_kind_name_copy(kind, namebuf, sizeof namebuf);
        while (prefix[i]) { log[i] = prefix[i]; i++; }
        while (namebuf[k] && i < 19u) { log[i++] = namebuf[k++]; }
        log[i] = 0;
        ui_combat_log_push(log);
    }
    if (kind == ITEM_KIND_POTION) {
        uint16_t heal = (uint16_t)player_hp_max / 2u; // half max HP (integer div)
        if ((uint16_t)player_hp + heal >= (uint16_t)player_hp_max) player_hp = player_hp_max;
        else player_hp = (uint8_t)((uint16_t)player_hp + heal);
    } else if (kind == ITEM_KIND_SCROLL) {
        scroll_blast_use(out);
    } else if (kind == ITEM_KIND_KEY) {
        player_hp = player_hp_max;
    } else if (kind == ITEM_KIND_CANDLE) {
        uint16_t nb = (uint16_t)player_light_bonus + (uint16_t)CANDLE_LIGHT_BONUS;
        player_light_bonus = (nb > 255u) ? 255u : (uint8_t)nb;
        out->lighting_refresh = 1u;
    } else if (kind == ITEM_KIND_SCROLL_ROOT) {
        scroll_root_use(out);
    } else if (kind == ITEM_KIND_BOOK_HEAL) {
        uint16_t heal = (uint16_t)player_hp_max / 4u; // quarter max HP
        if ((uint16_t)player_hp + heal >= (uint16_t)player_hp_max) player_hp = player_hp_max;
        else player_hp = (uint8_t)((uint16_t)player_hp + heal);
        book_heal_cooldown_turns = 5u;
    }
    if (items_kind_category(kind) != ITEM_CAT_REUSABLE)
        inventory_remove(item_idx);
    out->consumed_turn = 1u;
}

/* Weighted drop table: consumables weight 5, equipment/reusables weight 4, bow/axe/shield weight 3-4.
   Total 55 entries. The bow lands as a full quiver (ITEM_BOW_STACK_QTY) at pickup. */
static const uint8_t drop_table[55] = {
    ITEM_KIND_POTION,      ITEM_KIND_POTION,      ITEM_KIND_POTION,      ITEM_KIND_POTION,      ITEM_KIND_POTION,
    ITEM_KIND_SCROLL,      ITEM_KIND_SCROLL,      ITEM_KIND_SCROLL,      ITEM_KIND_SCROLL,      ITEM_KIND_SCROLL,
    ITEM_KIND_KEY,         ITEM_KIND_KEY,         ITEM_KIND_KEY,         ITEM_KIND_KEY,         ITEM_KIND_KEY,
    ITEM_KIND_CANDLE,      ITEM_KIND_CANDLE,      ITEM_KIND_CANDLE,      ITEM_KIND_CANDLE,      ITEM_KIND_CANDLE,
    ITEM_KIND_SCROLL_ROOT, ITEM_KIND_SCROLL_ROOT, ITEM_KIND_SCROLL_ROOT, ITEM_KIND_SCROLL_ROOT, ITEM_KIND_SCROLL_ROOT,
    ITEM_KIND_RUSTY_SWORD, ITEM_KIND_RUSTY_SWORD, ITEM_KIND_RUSTY_SWORD, ITEM_KIND_RUSTY_SWORD,
    ITEM_KIND_BOOK_HEAL,   ITEM_KIND_BOOK_HEAL,   ITEM_KIND_BOOK_HEAL,   ITEM_KIND_BOOK_HEAL,
    ITEM_KIND_HELMET,      ITEM_KIND_HELMET,      ITEM_KIND_HELMET,      ITEM_KIND_HELMET,
    ITEM_KIND_TUNIC,       ITEM_KIND_TUNIC,       ITEM_KIND_TUNIC,       ITEM_KIND_TUNIC,
    ITEM_KIND_BOOTS,       ITEM_KIND_BOOTS,       ITEM_KIND_BOOTS,       ITEM_KIND_BOOTS,
    ITEM_KIND_BOW,         ITEM_KIND_BOW,         ITEM_KIND_BOW,         ITEM_KIND_BOW,
    ITEM_KIND_AXE,         ITEM_KIND_AXE,         ITEM_KIND_AXE,
    ITEM_KIND_SHIELD,      ITEM_KIND_SHIELD,      ITEM_KIND_SHIELD,
};

uint8_t enemy_try_drop_item(uint8_t dx, uint8_t dy) BANKED {
    uint8_t gi;
    if ((rand() % 20u) >= 3u) return 0u;
    for (gi = 0u; gi < MAX_GROUND_ITEMS; gi++) {
        if (ground_item_kind[gi] == ITEM_KIND_NONE) {
            ground_item_kind[gi] = drop_table[rand() % 55u];
            ground_item_x[gi] = dx;
            ground_item_y[gi] = dy;
            return 1u;
        }
    }
    return 0u;
}
