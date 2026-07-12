#ifndef ITEMS_H
#define ITEMS_H

#include "defs.h"
#include "ability_dispatch.h"
#include <gbdk/platform.h>

#define ITEM_KIND_POTION      0u
#define ITEM_KIND_SCROLL      1u  // Death Scroll — damages all enemies
#define ITEM_KIND_KEY         2u  // BigHeal Potion in UI; enum KEY retained for saves
#define ITEM_KIND_CANDLE      3u
#define ITEM_KIND_SCROLL_ROOT 4u  // Root Scroll — roots all visible enemies for 12 turns
#define ITEM_KIND_RUSTY_SWORD 5u  // Equipment — +4 player damage while equipped
#define ITEM_KIND_BOOK_HEAL   6u  // Reusable — heals 25% max HP, not consumed
#define ITEM_KIND_HELMET      7u  // Equipment — +5 max HP
#define ITEM_KIND_TUNIC       8u  // Equipment — +10 max HP
#define ITEM_KIND_BOOTS       9u  // Equipment — +2 light radius
#define ITEM_KIND_BOW        10u  // Bow & Arrow — usable; fires one arrow, depletes stack by 1; drops in stacks of 20
#define ITEM_KIND_AXE        11u  // Axe — equipment; cleave hits up to 2 adjacent enemies on melee attack
#define ITEM_KIND_SHIELD     12u  // Shield — equipment; +10 max HP while equipped
#define ITEM_KIND_MACE       13u  // Mace — equipment; chance to stun an enemy on melee hit
/* Rings — 10 types × 3 tiers = 30 kinds, all EQUIP_SLOT_RING. Layout is type-major, tier-minor:
   kind = ITEM_KIND_RING_FIRST + type*3 + (tier-1). Types 0..9:
   0 Might(dmg) 1 Keen(crit) 2 Rage(dmg) 3 Guard(armor) 4 Veil(magdef)
   5 Vigor(hp+armor) 6 Valor(dmg+hp) 7 Hunter(crit+dodge) 8 Mystic(magdef+dodge) 9 Storm(dmg+dodge).
   All share the I16 tile; tier shown by palette tint (T1 bronze / T2 silver / T3 gold). */
#define ITEM_KIND_RING_FIRST 14u
#define ITEM_KIND_RING_COUNT 30u
#define ITEM_KIND_SCROLL_PORT6 44u // "Port: Flr6" — Witch starting scroll; warps the player to floor 6. Not in the drop table
#define ITEM_KIND_WITCH_HAT  45u  // Witch Hat — equipment (HEAD); Witch starts with it equipped. Not in the drop table
#define ITEM_KIND_COUNT      46u
#define ITEM_KIND_NONE      255u

#define ITEM_BOW_STACK_QTY   20u  // arrows granted per bow pickup

#define ITEM_CAT_CONSUMABLE   0u  // used and removed from inventory
#define ITEM_CAT_REUSABLE     1u  // used but kept in inventory; caller manages cooldown
#define ITEM_CAT_EQUIPMENT    2u  // equip/unequip; affects player stats

#define INVENTORY_MAX_SLOTS  30u // 5x6 grid in STATE_INVENTORY
#define MAX_GROUND_ITEMS      8u // floor-scoped pickup pool

uint8_t items_kind_tile(uint8_t kind) BANKED;      // sheet offset; 0 if invalid
uint8_t items_kind_category(uint8_t kind) BANKED;  // ITEM_CAT_*
uint8_t items_kind_palette(uint8_t kind) BANKED; // CGB palette
void    items_kind_name_copy(uint8_t kind, char *out, uint8_t cap) BANKED; // NUL-term, capped — copy required since strings live in bank 13
void    items_kind_desc_copy(uint8_t kind, char *out, uint8_t cap) BANKED; // short description for inventory/pickup display
void    items_kind_display_name_copy(uint8_t kind, int8_t mod_level, char *out, uint8_t cap) BANKED; // "+N "/"-N " prefix (omitted when 0) + kind name

int8_t  item_roll_mod_level(void) BANKED; // -1..+10 "+N" modifier roll, weighted toward 0; call only for ITEM_CAT_EQUIPMENT kinds
uint8_t ring_roll_kind(void) BANKED;      // random ring kind: uniform type, tier weighted toward T1

uint8_t inventory_first_empty(void) BANKED; // 0..INVENTORY_MAX_SLOTS-1, else 255
uint8_t inventory_count_used(void) BANKED;
void    inventory_clear_all(void) BANKED;
uint8_t inventory_add(uint8_t kind, int8_t mod_level) BANKED; // 1=added, 0=full
void    inventory_remove(uint8_t slot) BANKED; // compact upper slots down so belt slots 0..3 stay synced

void    items_use_belt(uint8_t item_idx, AbilityResult *out) BANKED; // belt slots 4..7 → inventory_kind[0..3]
uint8_t enemy_try_drop_item(uint8_t dx, uint8_t dy) BANKED; // 10% chance to place a weighted-random item on ground; returns 1 if dropped

#endif
