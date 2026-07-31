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
BANKREF_EXTERN(encounter_chest_drop_item)

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

// Dash: implemented as its dual — instead of the player gaining turns, every enemy loses
// 1-4 turns via the existing enemy_stun mechanic (no move, no melee, free debuff icons).
// Zero new systems and zero bank-2 cost; functionally identical to a speed buff.
static void cast_sprint(uint8_t rank, AbilityResult *out) {
    static const uint8_t sprint_freeze[4] = {1u, 2u, 3u, 4u};
    uint8_t i, dur = sprint_freeze[rank];
    for (i = 0u; i < num_enemies; i++) {
        if (!enemy_alive[i] || enemy_hidden[i]) continue; // phased Ghost can't be caught flat-footed
        if (enemy_stun[i] < dur) enemy_stun[i] = dur;
    }
    sfx_dodge_woosh();
    push_short("Sprint!");
    out->consumed_turn = 1u;
}

// Real ground trap: armed at the caster's tile (one trap max — recast moves it). The trigger
// scan lives in spells_tick_cooldowns (bank 27): first enemy standing on the tile takes
// player_damage + 2*rank and is rooted 4-7 turns if it survives. Cleared on floor change.
static void cast_bear_trap(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    trap_x = px;
    trap_y = py;
    trap_armed = (uint8_t)(rank + 1u);
    sfx_spell_zap();
    push_short("Trap set...");
    out->consumed_turn = 1u;
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

// Requires standing on a corpse; robbing consumes it (1 bit per corpse slot, reset per floor
// entry — a revisit regrows robbable graves, the ladder trip is the cost). Rank sets the odds;
// a payout uses the chest roll (guaranteed drop, best-of-two "+N") on the player's tile.
static void cast_graverob(uint8_t rank, AbilityResult *out) {
    static const uint8_t rob_pct[4] = {40u, 60u, 80u, 100u};
    static const uint8_t rob_bitmask[8] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}; // LUT — SDCC variable-shift miscompile
    uint8_t ci;
    for (ci = 0u; ci < num_corpses; ci++)
        if (corpse_x[ci] == g_player_x && corpse_y[ci] == g_player_y) break;
    if (ci >= num_corpses) { push_short("No corpse here"); return; } // fizzle — turn kept
    if (corpse_robbed[ci >> 3] & rob_bitmask[ci & 7u]) { push_short("Already robbed"); return; }
    corpse_robbed[ci >> 3] |= rob_bitmask[ci & 7u]; // consumed even on a dust roll
    if ((uint8_t)(rand() % 100u) < rob_pct[rank] && encounter_chest_drop_item(g_player_x, g_player_y))
        push_short("Graverob: loot!");
    else
        push_short("Graverob: dust");
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
