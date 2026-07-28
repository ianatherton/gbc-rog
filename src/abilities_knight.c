#pragma bank 6

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "items.h"
#include "ui.h"
#include "combat.h"
#include "enemy.h"
#include "targeting.h"
#include "entity_sprites.h"
#include "defs.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(entity_sprites_run_projectile)

#define KNIGHT_SHIELD_OFF ((uint8_t)(TILE_KNIGHT_SHIELD_VRAM - TILESET_VRAM_OFFSET))
#define KNIGHT_BOLT_OFF   ((uint8_t)(TILE_WITCH_BOLT_VRAM    - TILESET_VRAM_OFFSET))

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

/* Hit up to max_hits (0 = unlimited) live enemies inside the camera view for dmg,
   optionally flinging a projectile at each. Returns hit count; last kill → *out. */
static uint8_t knight_hit_onscreen(uint8_t px, uint8_t py, uint8_t dmg, uint8_t max_hits,
                                   uint8_t proj_off, AbilityResult *out) {
    uint8_t ei, hits = 0u;
    uint8_t cam_tx = (uint8_t)(camera_px >> 3);
    uint8_t cam_ty = (uint8_t)(camera_py >> 3);
    for (ei = 0u; ei < num_enemies; ei++) {
        uint8_t killed;
        if (!enemy_alive[ei] || enemy_hidden[ei]) continue; // phased-out Ghost takes no AoE
        if (enemy_x[ei] < cam_tx || enemy_x[ei] >= (uint8_t)(cam_tx + GRID_W)) continue;
        if (enemy_y[ei] < cam_ty || enemy_y[ei] >= (uint8_t)(cam_ty + GRID_H)) continue;
        if (max_hits && hits >= max_hits) break;
        if (proj_off) entity_sprites_run_projectile(px, py, enemy_x[ei], enemy_y[ei], proj_off, PAL_XP_UI);
        killed = combat_damage_enemy(ei, dmg, 0u);
        hits++;
        if (killed) { out->did_kill = 1u; out->kill_x = enemy_x[ei]; out->kill_y = enemy_y[ei]; }
    }
    return hits;
}

// Ranged bolt that stuns the nearest visible foe. TODO: also grant +3 light radius for the
// single turn (needs a one-turn light override — a bare player_light_bonus bump would stack).
static void cast_smite(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed;
    if (!targeting_find_nearest_visible(px, py, 5u, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return; // no turn spent
    }
    entity_sprites_run_projectile(px, py, tx, ty, KNIGHT_BOLT_OFF, PAL_XP_UI);
    killed = combat_damage_enemy(ei, (uint8_t)(2u + rank), 0u);
    if (!killed) enemy_stun[ei] = (uint8_t)(2u + rank);
    sfx_shield_sparkle();
    push_short("Smite!");
    out->consumed_turn = 1u;
    if (killed) { out->did_kill = 1u; out->kill_x = tx; out->kill_y = ty; }
}

// Placeholder: strikes every foe in view at once, standing in for a shield that ricochets
// enemy-to-enemy and returns. TODO: real sequential bounce + return path.
static void cast_shield_toss(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t hits = knight_hit_onscreen(px, py, (uint8_t)(2u + rank), 0u, KNIGHT_SHIELD_OFF, out);
    sfx_shield_sparkle();
    push_short(hits ? "Shield Toss!" : "Toss Miss");
    out->consumed_turn = 1u;
}

// Placeholder: a one-shot holy burst on the visible foes. TODO: bless the ground so it
// damages enemies every second turn they stand on it for 4 turns.
static void cast_sanctify(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    knight_hit_onscreen(px, py, (uint8_t)(2u + rank), 0u, 0u, out);
    sfx_shield_sparkle();
    push_short("Sanctify!");
    out->consumed_turn = 1u;
}

// Heal-over-time: 25% max HP per turn for rank+1 turns (rank 3 = full heal; scroll rank 0 = one tick).
// spells_tick_cooldowns owns the per-turn heal; the first tick lands at the end of the cast turn.
static void cast_prayer(uint8_t rank, AbilityResult *out) {
    prayer_hot_turns = (uint8_t)(rank + 1u);
    sfx_shield_sparkle();
    push_short("Prayer!");
    out->consumed_turn = 1u;
}

// Placeholder: scorches the tiles immediately around the knight. TODO: leave a 5-turn fire
// trail on each tile the player walks that burns enemies standing on it.
static void cast_holy_blaze(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, hits = 0u;
    for (ei = 0u; ei < num_enemies; ei++) {
        uint8_t ex, ey, dx, dy, killed;
        if (!enemy_alive[ei] || enemy_hidden[ei]) continue; // phased-out Ghost takes no AoE
        ex = enemy_x[ei]; ey = enemy_y[ei];
        dx = (ex > px) ? (uint8_t)(ex - px) : (uint8_t)(px - ex);
        dy = (ey > py) ? (uint8_t)(ey - py) : (uint8_t)(py - ey);
        if (dx > 1u || dy > 1u || (dx == 0u && dy == 0u)) continue; // adjacent 8 tiles
        killed = combat_damage_enemy(ei, (uint8_t)(2u + rank), 0u);
        hits = 1u;
        if (killed) { out->did_kill = 1u; out->kill_x = ex; out->kill_y = ey; }
    }
    sfx_shield_sparkle();
    push_short("Holy Blaze!");
    out->consumed_turn = 1u;
}

BANKREF(abilities_knight_new_run_init)
void abilities_knight_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_SHIELD, 0);
}

// rank currently unused: reflect damage scales with player_level (combat.c) either way,
// so the scroll (rank 0) version is "weaker" only in that the reader lacks knight levels.
static void cast_shield(uint8_t rank, AbilityResult *out) {
    (void)rank;
    if (knight_shield_active) {
        push_short("Shield Is Up");
        return; // no consumed_turn — toggling re-cast is free, no double-charge
    }
    knight_shield_active = 1u;
    sfx_shield_sparkle();
    push_short("Holy Fire Shield");
    out->consumed_turn = 1u; // raising the shield counts as a player action
}

BANKREF(ability_knight_cast)
void ability_knight_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    switch (spell_idx) {
        case 0u: cast_shield(rank, out);                break;
        case 1u: cast_smite(rank, px, py, out);         break;
        case 2u: cast_shield_toss(rank, px, py, out);   break;
        case 3u: cast_sanctify(rank, px, py, out);      break;
        case 4u: cast_prayer(rank, out);                break;
        case 5u: cast_holy_blaze(rank, px, py, out);    break;
        default: break;
    }
}
