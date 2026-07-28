#pragma bank 7

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "ally.h"
#include "items.h"
#include "ui.h"
#include "combat.h"
#include "enemy.h"
#include "targeting.h"
#include "entity_sprites.h"
#include "defs.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(ally_summon_fox)
BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(entity_sprites_run_projectile)
BANKREF_EXTERN(enemy_try_drop_item)

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

BANKREF(abilities_scoundrel_new_run_init)
void abilities_scoundrel_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_BOW, 0);
    inventory_add(ITEM_KIND_BOW, 0); // stacks → 40 arrows total
}

// rank currently unused — fox stats are fixed; scroll (rank 0) summons the same fox.
static void cast_call_fox(uint8_t px, uint8_t py, AbilityResult *out) {
    push_short("Call Fox");
    ally_summon_fox(px, py);
    out->consumed_turn = 1u;
}

// Placeholder buff: TODO real "every other move is a free turn" (needs a state_gameplay turn hook).
static void cast_sprint(uint8_t rank, AbilityResult *out) {
    (void)rank;
    push_short("Sprint!");
    out->consumed_turn = 1u;
}

// Placeholder: snaps the nearest foe now (dmg + long root). TODO: drop a real ground trap
// that triggers when an enemy steps onto it.
static void cast_bear_trap(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed;
    if (!targeting_find_nearest_visible(px, py, 4u, &ei, &tx, &ty, &too_far)) { push_short("No Target"); return; }
    killed = combat_damage_enemy(ei, (uint8_t)(2u + rank), 0u);
    if (!killed) enemy_status[ei] = (uint8_t)(4u + rank);
    sfx_lunge_hit();
    push_short("Bear Trap!");
    out->consumed_turn = 1u;
    if (killed) { out->did_kill = 1u; out->kill_x = tx; out->kill_y = ty; }
}

// Ranged dart that poisons (root stands in for DoT — no damage-over-time system yet).
static void cast_poison_dart(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed;
    if (!targeting_find_nearest_visible(px, py, 5u, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return;
    }
    entity_sprites_run_projectile(px, py, tx, ty, (uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET), PAL_XP_UI);
    killed = combat_damage_enemy(ei, (uint8_t)(2u + rank), 0u);
    if (!killed && enemy_status[ei] < (uint8_t)(2u + rank)) enemy_status[ei] = (uint8_t)(2u + rank);
    sfx_spell_zap();
    push_short("Poison Dart!");
    out->consumed_turn = 1u;
    if (killed) { out->did_kill = 1u; out->kill_x = tx; out->kill_y = ty; }
}

// Placeholder: rolls the drop table at the player's tile for a chance at loot. TODO: require +
// consume a nearby headstone/corpse, with rank raising the odds.
static void cast_graverob(uint8_t rank, AbilityResult *out) {
    (void)rank;
    if (enemy_try_drop_item(g_player_x, g_player_y)) push_short("Graverob: loot!");
    else push_short("Graverob: dust");
    out->consumed_turn = 1u;
}

// Timed bow buff: range 4→8 and full-damage shots while sniper_turns > 0 (checked in bow_shoot.c).
// Fizzles without a bow — no turn, cooldown, or scroll spent. Bow is class-agnostic, so the
// rank-0 scroll works for anyone carrying one.
static void cast_sniper_mode(uint8_t rank, AbilityResult *out) {
    static const uint8_t sniper_dur[4] = {3u, 4u, 6u, 8u};
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++)
        if (inventory_kind[i] == ITEM_KIND_BOW) break;
    if (i == INVENTORY_MAX_SLOTS) {
        push_short("Need Bow");
        return;
    }
    sniper_turns = sniper_dur[rank];
    push_short("Sniper Mode!");
    out->consumed_turn = 1u;
}

BANKREF(ability_scoundrel_cast)
void ability_scoundrel_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    switch (spell_idx) {
        case 0u: cast_call_fox(px, py, out);          break;
        case 1u: cast_sprint(rank, out);              break;
        case 2u: cast_bear_trap(rank, px, py, out);   break;
        case 3u: cast_poison_dart(rank, px, py, out); break;
        case 4u: cast_graverob(rank, out);            break;
        case 5u: cast_sniper_mode(rank, out);         break;
        default: break;
    }
}
