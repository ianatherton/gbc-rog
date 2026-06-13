#ifndef EQUIPMENT_H
#define EQUIPMENT_H

#include "items.h"
#include <gbdk/platform.h>

/* Equipment slots — one item occupies exactly one slot; one item allowed per slot at a time. */
#define EQUIP_SLOT_NONE    0u
#define EQUIP_SLOT_HEAD    1u
#define EQUIP_SLOT_BODY    2u
#define EQUIP_SLOT_FEET    3u
#define EQUIP_SLOT_WEAPON  4u  /* rusty sword and axe both occupy this slot */
#define EQUIP_SLOT_OFFHAND 5u

typedef struct {
    uint8_t slot;        /* EQUIP_SLOT_* */
    uint8_t hp_max;      /* delta to player_hp_max     */
    uint8_t damage;      /* delta to player_damage      */
    uint8_t light_bonus; /* delta to player_light_bonus */
    uint8_t crit_chance; /* delta to player_crit_chance (0-100 pct) */
} EquipStatDef;

extern const EquipStatDef equip_stat_defs[ITEM_KIND_COUNT];

void    items_equip_apply(uint8_t kind, uint8_t now_equipped) BANKED;
uint8_t items_equip_slot(uint8_t kind) BANKED;       /* returns EQUIP_SLOT_* */
uint8_t equipped_kind_in_slot(uint8_t slot) BANKED;  /* returns equipped ITEM_KIND_*, or ITEM_KIND_NONE */

#endif
