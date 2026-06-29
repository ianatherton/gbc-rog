#pragma bank 13

#include "items.h"
#include "globals.h"
#include "map.h"
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
    ITEM_CAT_EQUIPMENT,  // MACE
    /* 30 rings (Might,Keen,Rage,Guard,Veil,Vigor,Valor,Hunter,Mystic,Storm × T1/T2/T3) */
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Might
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Keen
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Rage
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Guard
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Veil
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Vigor
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Valor
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Hunter
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Mystic
    ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, ITEM_CAT_EQUIPMENT, // Storm
    ITEM_CAT_CONSUMABLE, // SCROLL_PORT6 (Port: Flr6)
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
    TILE_ITEM_2,          // MACE — I2
    /* 30 rings — all share the I16 ring tile (TILE_RING_OFF); tier shown by palette */
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Might
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Keen
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Rage
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Guard
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Veil
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Vigor
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Valor
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Hunter
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Mystic
    TILE_RING_OFF, TILE_RING_OFF, TILE_RING_OFF, // Storm
    TILE_SCROLL_BELT_OFF, // SCROLL_PORT6 — reuses the scroll art
};

static const uint8_t kind_pal[ITEM_KIND_COUNT] = {
    PAL_LIFE_UI,      // POTION
    PAL_XP_UI_BG,        // SCROLL (Death)
    PAL_LIFE_UI,      // KEY
    PAL_LADDER,       // CANDLE
    PAL_ENEMY_SNAKE,  // SCROLL_ROOT — green
    PAL_WALL_BG,      // RUSTY_SWORD — warm brownish ramp
    PAL_XP_UI_BG,        // BOOK_HEAL — blue
    PAL_WALL_BG,      // HELMET — warm metal ramp
    PAL_WALL_BG,      // TUNIC — warm metal ramp
    PAL_LADDER,       // BOOTS — earthy/leather
    PAL_LADDER,       // BOW — torch ramp
    PAL_WALL_BG,      // AXE — warm metal ramp
    PAL_WALL_BG,      // SHIELD — warm metal ramp
    PAL_WALL_BG,      // MACE — warm metal ramp
    /* 30 rings — tier tint: T1 bronze(PAL_WALL_BG) / T2 silver(PAL_CORPSE grey) / T3 gold(PAL_XP_UI_BG).
       Silver uses slot 0 (grey ramp == the old PAL_UI ramp) so the multi-color ring icon keeps its
       grey mid-tones; PAL_UI now points at the red heart palette, which would tint silver red. */
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Might
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Keen
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Rage
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Guard
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Veil
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Vigor
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Valor
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Hunter
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Mystic
    PAL_WALL_BG, PAL_CORPSE, PAL_XP_UI_BG, // Storm
    PAL_ENEMY_SNAKE, // SCROLL_PORT6 — green tint to read apart from the gold Death Scroll
};

static const char *const kind_name[ITEM_KIND_COUNT] = {
    "Heal Potion", "Death Scroll", "BigHeal Potion", "Candle", "Root Scroll",
    "Rusty Sword", "Book: Heal", "Helmet", "Tunic", "Boots", "Bow & Arrow",
    "Axe", "Shield", "Mace",
    /* 30 rings — 3 tiers per type share a name; tier is conveyed by palette tint */
    "Might Ring",  "Might Ring",  "Might Ring",
    "Keen Ring",   "Keen Ring",   "Keen Ring",
    "Rage Ring",   "Rage Ring",   "Rage Ring",
    "Guard Ring",  "Guard Ring",  "Guard Ring",
    "Veil Ring",   "Veil Ring",   "Veil Ring",
    "Vigor Ring",  "Vigor Ring",  "Vigor Ring",
    "Valor Ring",  "Valor Ring",  "Valor Ring",
    "Hunter Ring", "Hunter Ring", "Hunter Ring",
    "Mystic Ring", "Mystic Ring", "Mystic Ring",
    "Storm Ring",  "Storm Ring",  "Storm Ring",
    "Port: Flr6",
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
    "+5 attack, chance to stun. A spiked iron mace.",
    /* 30 rings — desc per (type,tier); + modifier adjusts each stat on top of these */
    "+2 attack. A plain iron band.",        // Might T1
    "+4 attack. A bevelled steel band.",    // Might T2
    "+6 attack. A rune-etched warband.",    // Might T3
    "+5% crit. A sharp-edged ring.",        // Keen T1
    "+10% crit. A faceted gem ring.",       // Keen T2
    "+15% crit. A keen glass sliver.",      // Keen T3
    "+3 attack. A jagged red band.",        // Rage T1
    "+6 attack. A blood-warm band.",        // Rage T2
    "+9 attack. A wrathful iron coil.",     // Rage T3
    "+5% armor. A thick banded ring.",      // Guard T1
    "+10% armor. A plated steel ring.",     // Guard T2
    "+15% armor. A bulwark signet.",        // Guard T3
    "+5% magic def. A cool jade ring.",     // Veil T1
    "+10% magic def. A warded ring.",       // Veil T2
    "+15% magic def. A silent veil ring.",  // Veil T3
    "+5 HP, +3% armor. A sturdy ring.",     // Vigor T1
    "+10 HP, +6% armor. A hale ring.",      // Vigor T2
    "+15 HP, +9% armor. A titan band.",     // Vigor T3
    "+1 atk, +4 HP. A brave band.",         // Valor T1
    "+2 atk, +8 HP. A valiant ring.",       // Valor T2
    "+3 atk, +12 HP. A hero's ring.",       // Valor T3
    "+4% crit, +3% dodge. A lithe ring.",   // Hunter T1
    "+8% crit, +6% dodge. A hawk ring.",    // Hunter T2
    "+12% crit, +9% dodge. A predator ring.", // Hunter T3
    "+4% mdef, +3% dodge. A misty ring.",   // Mystic T1
    "+8% mdef, +6% dodge. A seer ring.",    // Mystic T2
    "+12% mdef, +9% dodge. An arcane ring.", // Mystic T3
    "+1 atk, +4% dodge. A breezy ring.",    // Storm T1
    "+2 atk, +8% dodge. A gale ring.",      // Storm T2
    "+3 atk, +12% dodge. A tempest ring.",  // Storm T3
    "Warps you to floor 6. A torn travel scroll.", // SCROLL_PORT6
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

/* "+N "/"-N " prefix (range is always -1..+10, so at most 2 digits) ahead of the kind name;
   no prefix at all when mod_level is 0 (the common case). */
void items_kind_display_name_copy(uint8_t kind, int8_t mod_level, char *out, uint8_t cap) BANKED {
    const char *s;
    uint8_t i = 0u, k;
    if (cap == 0u) return;
    if (mod_level != 0) {
        int8_t mag = (mod_level < 0) ? (int8_t)(-mod_level) : mod_level;
        if ((uint8_t)(i + 1u) < cap) out[i++] = (mod_level < 0) ? '-' : '+';
        if (mag >= 10) {
            if ((uint8_t)(i + 1u) < cap) out[i++] = '1';
            mag = (int8_t)(mag - 10);
        }
        if ((uint8_t)(i + 1u) < cap) out[i++] = (char)('0' + mag);
        if ((uint8_t)(i + 1u) < cap) out[i++] = ' ';
    }
    s = (kind < ITEM_KIND_COUNT) ? kind_name[kind] : "?";
    k = 0u;
    while (s[k] && (uint8_t)(i + 1u) < cap) { out[i++] = s[k++]; }
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
        inventory_mod_level[i] = 0;
    }
}

uint8_t inventory_add(uint8_t kind, int8_t mod_level) BANKED {
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
            inventory_mod_level[s] = mod_level;
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
        inventory_kind[i]      = inventory_kind[(uint8_t)(i + 1u)];
        inventory_equipped[i]  = inventory_equipped[(uint8_t)(i + 1u)];
        inventory_count[i]     = inventory_count[(uint8_t)(i + 1u)];
        inventory_mod_level[i] = inventory_mod_level[(uint8_t)(i + 1u)];
    }
    inventory_kind[INVENTORY_MAX_SLOTS - 1u]      = ITEM_KIND_NONE;
    inventory_equipped[INVENTORY_MAX_SLOTS - 1u]  = 0u;
    inventory_count[INVENTORY_MAX_SLOTS - 1u]     = 0u;
    inventory_mod_level[INVENTORY_MAX_SLOTS - 1u] = 0;

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
                    int8_t  tm = inventory_mod_level[j];
                    inventory_kind[j]      = inventory_kind[lb];
                    inventory_equipped[j]  = inventory_equipped[lb];
                    inventory_count[j]     = inventory_count[lb];
                    inventory_mod_level[j] = inventory_mod_level[lb];
                    inventory_kind[lb]     = tk;
                    inventory_equipped[lb] = te;
                    inventory_count[lb]    = tc;
                    inventory_mod_level[lb]= tm;
                    break;
                }
            }
            if (j >= INVENTORY_MAX_SLOTS) {
                /* no usable items remain — push equipment to first empty non-belt slot */
                for (j = BELT_ITEM_SLOT_COUNT; j < INVENTORY_MAX_SLOTS; j++) {
                    if (inventory_kind[j] == ITEM_KIND_NONE) {
                        inventory_kind[j]      = inventory_kind[lb];
                        inventory_equipped[j]  = inventory_equipped[lb];
                        inventory_count[j]     = inventory_count[lb];
                        inventory_mod_level[j] = inventory_mod_level[lb];
                        inventory_kind[lb]      = ITEM_KIND_NONE;
                        inventory_equipped[lb]  = 0u;
                        inventory_count[lb]     = 0u;
                        inventory_mod_level[lb] = 0;
                        break;
                    }
                }
                if (j >= INVENTORY_MAX_SLOTS) {
                    inventory_kind[lb]      = ITEM_KIND_NONE;
                    inventory_equipped[lb]  = 0u;
                    inventory_count[lb]     = 0u;
                    inventory_mod_level[lb] = 0;
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
    } else if (kind == ITEM_KIND_SCROLL_PORT6) {
        // Warp to floor 6. State_gameplay sees pending_transition set after the belt use and bounces
        // to STATE_TRANSITION (TRANS_FLOOR_PORT) before any enemy turn runs on the floor we're leaving.
        pending_port_floor = BOSS2_FLOOR_NUM;
        pending_transition = TRANS_FLOOR_PORT;
    }
    if (items_kind_category(kind) != ITEM_CAT_REUSABLE)
        inventory_remove(item_idx);
    out->consumed_turn = 1u;
}

/* Weighted drop table: consumables weight 5, equipment/reusables weight 4, bow/axe/shield/mace weight 3-4.
   Total 58 entries. The bow lands as a full quiver (ITEM_BOW_STACK_QTY) at pickup. */
static const uint8_t drop_table[58] = {
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
    ITEM_KIND_MACE,        ITEM_KIND_MACE,        ITEM_KIND_MACE,
};

/* "+N" modifier roll: weighted toward 0, with a rare (~1%) reroll across the full -1..+10
   range so the occasional "masterwork" item can still show up. */
static const int8_t mod_table[16] = { 0,0,0,0,0,0,0,0, -1,-1, 1,1,1, 2,2, 3 };
int8_t item_roll_mod_level(void) BANKED {
    int8_t m = mod_table[rand() % 16u];
    if ((rand() % 100u) == 0u) m = (int8_t)(rand() % 12u) - 1;
    return m;
}

/* Random ring kind: type uniform across 10, tier weighted toward T1 (60% / 30% / 10%).
   kind = ITEM_KIND_RING_FIRST + type*3 + tier. The caller still rolls the "+N" modifier. */
uint8_t ring_roll_kind(void) BANKED {
    uint8_t type = (uint8_t)(rand() % 10u);
    uint8_t r = (uint8_t)(rand() % 10u);
    uint8_t tier = (r < 6u) ? 0u : (r < 9u) ? 1u : 2u;
    return (uint8_t)(ITEM_KIND_RING_FIRST + type * 3u + tier);
}

uint8_t enemy_try_drop_item(uint8_t dx, uint8_t dy) BANKED {
    uint8_t gi;
    uint8_t kind;
    if (map_tile_is_stairs_or_ladder(dx, dy)) return 0u; // never block stairs/ladders with loot
    if ((rand() % 20u) >= 3u) return 0u;
    for (gi = 0u; gi < MAX_GROUND_ITEMS; gi++) {
        if (ground_item_kind[gi] == ITEM_KIND_NONE) {
            kind = drop_table[rand() % 58u];
            if ((rand() % 100u) < RING_DROP_PCT) kind = ring_roll_kind();
            ground_item_kind[gi] = kind;
            ground_item_x[gi] = dx;
            ground_item_y[gi] = dy;
            ground_item_mod_level[gi] = (items_kind_category(kind) == ITEM_CAT_EQUIPMENT) ? item_roll_mod_level() : 0;
            return 1u;
        }
    }
    return 0u;
}
