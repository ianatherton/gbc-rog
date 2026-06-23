#pragma bank 20

#include "equipment.h"
#include "globals.h"

const EquipStatDef equip_stat_defs[ITEM_KIND_COUNT] = {
    /* ITEM_KIND_POTION      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_SCROLL      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_KEY         */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_CANDLE      */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_SCROLL_ROOT */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_RUSTY_SWORD */ { EQUIP_SLOT_WEAPON,  0,  4, 0, 10, 0, 0, 0 },
    /* ITEM_KIND_BOOK_HEAL   */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_HELMET      */ { EQUIP_SLOT_HEAD,    5,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_TUNIC       */ { EQUIP_SLOT_BODY,   10,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_BOOTS       */ { EQUIP_SLOT_FEET,    0,  0, 2,  0, 0, 0, 0 },
    /* ITEM_KIND_BOW         */ { EQUIP_SLOT_NONE,    0,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_AXE         */ { EQUIP_SLOT_WEAPON,  0,  6, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_SHIELD      */ { EQUIP_SLOT_OFFHAND,10,  0, 0,  0, 0, 0, 0 },
    /* ITEM_KIND_MACE        */ { EQUIP_SLOT_WEAPON,  0,  5, 0,  0, 0, 0, 0 },
    /* ── 30 rings (EQUIP_SLOT_RING). Fields: slot, hp_max, damage, light, crit, armor, magdef, dodge.
       The "+N" mod adds to every nonzero field (mod_clamped), so hybrids scale on both stats. ── */
    /* Might  (damage)        */ { EQUIP_SLOT_RING, 0,  2, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  4, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  6, 0,  0,  0,  0,  0 },
    /* Keen   (crit%)         */ { EQUIP_SLOT_RING, 0,  0, 0,  5,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0, 10,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0, 15,  0,  0,  0 },
    /* Rage   (damage)        */ { EQUIP_SLOT_RING, 0,  3, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  6, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  9, 0,  0,  0,  0,  0 },
    /* Guard  (armor%)        */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  5,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0, 10,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0, 15,  0,  0 },
    /* Veil   (magdef%)       */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0,  5,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0, 10,  0 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0, 15,  0 },
    /* Vigor  (hp + armor%)   */ { EQUIP_SLOT_RING, 5,  0, 0,  0,  3,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING,10,  0, 0,  0,  6,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING,15,  0, 0,  0,  9,  0,  0 },
    /* Valor  (damage + hp)   */ { EQUIP_SLOT_RING, 4,  1, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING, 8,  2, 0,  0,  0,  0,  0 },
    /*                        */ { EQUIP_SLOT_RING,12,  3, 0,  0,  0,  0,  0 },
    /* Hunter (crit% + dodge%)*/ { EQUIP_SLOT_RING, 0,  0, 0,  4,  0,  0,  3 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  8,  0,  0,  6 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0, 12,  0,  0,  9 },
    /* Mystic (magdef%+dodge%)*/ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0,  4,  3 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0,  8,  6 },
    /*                        */ { EQUIP_SLOT_RING, 0,  0, 0,  0,  0, 12,  9 },
    /* Storm  (damage + dodge%)*/{ EQUIP_SLOT_RING, 0,  1, 0,  0,  0,  0,  4 },
    /*                        */ { EQUIP_SLOT_RING, 0,  2, 0,  0,  0,  0,  8 },
    /*                        */ { EQUIP_SLOT_RING, 0,  3, 0,  0,  0,  0, 12 },
};

/* Adds the item's "+N" mod_level to a nonzero base stat, clamped so a nonzero base stat
   never drops to 0 (a -1 mod never fully negates it) and never exceeds the given cap. */
static uint8_t mod_clamped(uint8_t base, int8_t mod, uint8_t cap) {
    int16_t eff = (int16_t)base + mod;
    if (eff < 1) return 1u;
    if (eff > (int16_t)cap) return cap;
    return (uint8_t)eff;
}

void items_equip_apply(uint8_t kind, uint8_t inv_slot, uint8_t now_equipped) BANKED {
    const EquipStatDef *d;
    int8_t mod;
    if (kind >= ITEM_KIND_COUNT) return;
    d = &equip_stat_defs[kind];
    mod = (inv_slot < INVENTORY_MAX_SLOTS) ? inventory_mod_level[inv_slot] : 0;

    if (d->hp_max) {
        uint8_t applied = mod_clamped(d->hp_max, mod, 255u);
        if (now_equipped) { player_hp_max = (uint8_t)(player_hp_max + applied); }
        else {
            player_hp_max = (player_hp_max > applied) ? (uint8_t)(player_hp_max - applied) : 1u;
            if (player_hp > player_hp_max) player_hp = player_hp_max;
        }
    }
    if (d->damage) {
        uint8_t applied = mod_clamped(d->damage, mod, 255u);
        if (now_equipped) player_damage = (uint8_t)(player_damage + applied);
        else              player_damage = (player_damage > applied) ? (uint8_t)(player_damage - applied) : 1u;
    }
    if (d->light_bonus) {
        uint8_t applied = mod_clamped(d->light_bonus, mod, 255u);
        if (now_equipped) { uint16_t nb = (uint16_t)player_light_bonus + applied;
                            player_light_bonus = (nb > 255u) ? 255u : (uint8_t)nb; }
        else              { player_light_bonus = (player_light_bonus > applied) ? (uint8_t)(player_light_bonus - applied) : 0u; }
    }
    if (d->crit_chance) {
        uint8_t applied = mod_clamped(d->crit_chance, mod, 100u);
        if (now_equipped) { uint16_t nc = (uint16_t)player_crit_chance + applied;
                            player_crit_chance = (nc > 100u) ? 100u : (uint8_t)nc; }
        else              { player_crit_chance = (player_crit_chance > applied) ? (uint8_t)(player_crit_chance - applied) : 0u; }
    }
    if (d->armor) {
        uint8_t applied = mod_clamped(d->armor, mod, 100u);
        if (now_equipped) { uint16_t na = (uint16_t)player_armor + applied;
                            player_armor = (na > 100u) ? 100u : (uint8_t)na; }
        else              { player_armor = (player_armor > applied) ? (uint8_t)(player_armor - applied) : 0u; }
    }
    if (d->magdef) {
        uint8_t applied = mod_clamped(d->magdef, mod, 100u);
        if (now_equipped) { uint16_t nm = (uint16_t)player_magdef + applied;
                            player_magdef = (nm > 100u) ? 100u : (uint8_t)nm; }
        else              { player_magdef = (player_magdef > applied) ? (uint8_t)(player_magdef - applied) : 0u; }
    }
    if (d->dodge) {
        uint8_t applied = mod_clamped(d->dodge, mod, 100u);
        if (now_equipped) { uint16_t nd = (uint16_t)player_dodge + applied;
                            player_dodge = (nd > 100u) ? 100u : (uint8_t)nd; }
        else              { player_dodge = (player_dodge > applied) ? (uint8_t)(player_dodge - applied) : 0u; }
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

uint8_t equipped_inv_index(uint8_t slot) BANKED {
    uint8_t i, k;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
        k = inventory_kind[i];
        if (inventory_equipped[i] && k < ITEM_KIND_COUNT && equip_stat_defs[k].slot == slot)
            return i;
    }
    return 255u;
}
