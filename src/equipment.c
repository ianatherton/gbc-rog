#pragma bank 20

#include "equipment.h"
#include "globals.h"

const EquipStatDef equip_stat_defs[ITEM_KIND_COUNT] = {
    /* ITEM_KIND_POTION      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_SCROLL      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_KEY         */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_CANDLE      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_SCROLL_ROOT */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_RUSTY_SWORD */ { EQUIP_SLOT_WEAPON,  0,  4, 0, 10 },
    /* ITEM_KIND_BOOK_HEAL   */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_HELMET      */ { EQUIP_SLOT_HEAD,    5,  0, 0,  0 },
    /* ITEM_KIND_TUNIC       */ { EQUIP_SLOT_BODY,   10,  0, 0,  0 },
    /* ITEM_KIND_BOOTS       */ { EQUIP_SLOT_FEET,    0,  0, 2,  0 },
    /* ITEM_KIND_BOW         */ { EQUIP_SLOT_NONE,    0,  0, 0,  0 },
    /* ITEM_KIND_AXE         */ { EQUIP_SLOT_WEAPON,  0,  6, 0,  0 },
    /* ITEM_KIND_SHIELD      */ { EQUIP_SLOT_OFFHAND,10,  0, 0,  0 },
};

void items_equip_apply(uint8_t kind, uint8_t now_equipped) BANKED {
    const EquipStatDef *d;
    if (kind >= ITEM_KIND_COUNT) return;
    d = &equip_stat_defs[kind];

    if (d->hp_max) {
        if (now_equipped) { player_hp_max = (uint8_t)(player_hp_max + d->hp_max); }
        else {
            player_hp_max = (player_hp_max > d->hp_max) ? (uint8_t)(player_hp_max - d->hp_max) : 1u;
            if (player_hp > player_hp_max) player_hp = player_hp_max;
        }
    }
    if (d->damage) {
        if (now_equipped) player_damage = (uint8_t)(player_damage + d->damage);
        else              player_damage = (player_damage > d->damage) ? (uint8_t)(player_damage - d->damage) : 1u;
    }
    if (d->light_bonus) {
        if (now_equipped) { uint16_t nb = (uint16_t)player_light_bonus + d->light_bonus;
                            player_light_bonus = (nb > 255u) ? 255u : (uint8_t)nb; }
        else              { player_light_bonus = (player_light_bonus > d->light_bonus) ? (uint8_t)(player_light_bonus - d->light_bonus) : 0u; }
    }
    if (d->crit_chance) {
        if (now_equipped) { uint16_t nc = (uint16_t)player_crit_chance + d->crit_chance;
                            player_crit_chance = (nc > 100u) ? 100u : (uint8_t)nc; }
        else              { player_crit_chance = (player_crit_chance > d->crit_chance) ? (uint8_t)(player_crit_chance - d->crit_chance) : 0u; }
    }
}

uint8_t items_equip_slot(uint8_t kind) BANKED {
    if (kind >= ITEM_KIND_COUNT) return EQUIP_SLOT_NONE;
    return equip_stat_defs[kind].slot;
}

uint8_t equipped_kind_in_slot(uint8_t slot) BANKED {
    uint8_t i, k;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
        k = inventory_kind[i];
        if (inventory_equipped[i] && k < ITEM_KIND_COUNT && equip_stat_defs[k].slot == slot)
            return k;
    }
    return ITEM_KIND_NONE;
}
