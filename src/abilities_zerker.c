#pragma bank 9

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "combat.h"
#include "enemy.h"
#include "items.h"
#include "equipment.h"
#include "targeting.h"
#include "entity_sprites.h"
#include "defs.h"
#include "globals.h"
#include "ui.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(combat_crit_roll)
BANKREF_EXTERN(entity_sprites_run_projectile)
BANKREF_EXTERN(equipped_kind_in_slot)

#define ZERKER_AXE_OFF ((uint8_t)(TILE_ZERKER_WHIRLWIND_VRAM - TILESET_VRAM_OFFSET)) // axe shares the whirlwind tile

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

/* Hit every live enemy within Chebyshev `radius` of (px,py) for dmg, optionally stunning
   survivors. Excludes the caster's own tile. Returns 1 if anything was hit; last kill → *out. */
static uint8_t zerk_aoe(uint8_t px, uint8_t py, uint8_t radius, uint8_t dmg, uint8_t stun, AbilityResult *out) {
    uint8_t ei, hits = 0u;
    for (ei = 0u; ei < num_enemies; ei++) {
        uint8_t ex, ey, dx, dy, killed;
        if (!enemy_alive[ei] || enemy_hidden[ei]) continue; // phased-out Ghost takes no AoE
        ex = enemy_x[ei]; ey = enemy_y[ei];
        dx = (ex > px) ? (uint8_t)(ex - px) : (uint8_t)(px - ex);
        dy = (ey > py) ? (uint8_t)(ey - py) : (uint8_t)(py - ey);
        if (dx > radius || dy > radius || (dx == 0u && dy == 0u)) continue;
        killed = combat_damage_enemy(ei, dmg, 0u);
        if (stun && !killed && enemy_stun[ei] < stun) enemy_stun[ei] = stun;
        hits = 1u;
        if (killed) { out->did_kill = 1u; out->kill_x = ex; out->kill_y = ey; }
    }
    return hits;
}

static const uint8_t whirl_radius[4] = {1u, 1u, 2u, 3u}; // rank 0/scroll = 1 tile; ranks 1-3 widen the spin

static void cast_whirlwind(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t dmg = (rank == 0u) ? player_damage
                : combat_crit_roll((player_damage > 127u) ? 255u : (uint8_t)(player_damage << 1)); // 2x base; crit doubles again
    uint8_t hits = zerk_aoe(px, py, whirl_radius[rank], dmg, 0u, out);
    sfx_whirlwind_cast();
    push_short(hits ? "Whirlwind" : "Whirlwind Miss");
    out->consumed_turn = 1u;
}

// Placeholder: strikes + heavily stuns the nearest visible foe in place. TODO: blink the
// player adjacent to the target first (moving g_player_x/y needs a bank-2 redraw path).
static void cast_charge(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed;
    if (!targeting_find_nearest_visible(px, py, 6u, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return;
    }
    killed = combat_damage_enemy(ei, (uint8_t)(player_damage + rank), 0u);
    if (!killed) enemy_stun[ei] = (uint8_t)(5u + rank);
    sfx_lunge_hit();
    push_short("Charge!");
    out->consumed_turn = 1u;
    if (killed) { out->did_kill = 1u; out->kill_x = tx; out->kill_y = ty; }
}

static void cast_thunder_wave(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t hits = zerk_aoe(px, py, 3u, (uint8_t)(player_damage + rank), (uint8_t)(2u + rank), out);
    sfx_whirlwind_cast();
    push_short(hits ? "Thunder Wave!" : "Thunder Wave");
    out->consumed_turn = 1u;
}

// Placeholder: roots every visible foe (they cower). TODO: switch their AI to a skittish
// rat/flee behavior (enemy behavior is driven by enemy_type — needs sprite/stat handling).
static void cast_shout_fear(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, turns = (uint8_t)(3u + rank);
    uint8_t cam_tx = (uint8_t)(camera_px >> 3);
    uint8_t cam_ty = (uint8_t)(camera_py >> 3);
    (void)px; (void)py;
    for (ei = 0u; ei < num_enemies; ei++) {
        if (!enemy_alive[ei] || enemy_hidden[ei]) continue; // phased-out Ghost takes no AoE
        if (enemy_x[ei] < cam_tx || enemy_x[ei] >= (uint8_t)(cam_tx + GRID_W)) continue;
        if (enemy_y[ei] < cam_ty || enemy_y[ei] >= (uint8_t)(cam_ty + GRID_H)) continue;
        if (enemy_status[ei] < turns) enemy_status[ei] = turns;
    }
    push_short("Shout: Fear!");
    out->consumed_turn = 1u;
}

// Ranged attack at 2x melee damage; requires an axe equipped in the weapon slot.
static void cast_throw_axe(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed, dmg;
    if (equipped_kind_in_slot(EQUIP_SLOT_WEAPON) != ITEM_KIND_AXE) { push_short("Need Axe"); return; }
    if (!targeting_find_nearest_visible(px, py, 5u, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return;
    }
    entity_sprites_run_projectile(px, py, tx, ty, ZERKER_AXE_OFF, PAL_XP_UI);
    dmg = (uint8_t)(((player_damage > 127u) ? 255u : (uint8_t)(player_damage << 1)) + rank);
    killed = combat_damage_enemy(ei, dmg, 0u);
    sfx_lunge_hit();
    push_short("Throw Axe!");
    out->consumed_turn = 1u;
    if (killed) { out->did_kill = 1u; out->kill_x = tx; out->kill_y = ty; }
}

// Timed rage buff: 2x basic-melee + axe-cleave damage (combat.c), +1 damage taken per landed
// enemy hit (enemy_extras.c) while zerk_turns > 0. Deliberately does NOT multiply Whirlwind /
// Throw Axe — those read player_damage raw (Throw Axe already 2x) and would stack to 4x.
static void cast_zerk_mode(uint8_t rank, AbilityResult *out) {
    static const uint8_t zerk_dur[4] = {3u, 4u, 6u, 8u};
    zerk_turns = zerk_dur[rank];
    push_short("ZERK MODE!");
    out->consumed_turn = 1u;
}

BANKREF(abilities_zerker_new_run_init)
void abilities_zerker_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_AXE, 0);
}

BANKREF(ability_zerker_cast)
void ability_zerker_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    switch (spell_idx) {
        case 0u: cast_whirlwind(rank, px, py, out);   break;
        case 1u: cast_charge(rank, px, py, out);      break;
        case 2u: cast_thunder_wave(rank, px, py, out); break;
        case 3u: cast_shout_fear(rank, px, py, out);  break;
        case 4u: cast_throw_axe(rank, px, py, out);   break;
        case 5u: cast_zerk_mode(rank, out);           break;
        default: break;
    }
}
