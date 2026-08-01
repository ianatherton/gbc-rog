#pragma bank 8

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "targeting.h"
#include "combat.h"
#include "globals.h"
#include "enemy.h"
#include "items.h"
#include "defs.h"
#include "ui.h"
#include "ally.h"
#include "entity_sprites.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(combat_crit_roll)
BANKREF_EXTERN(entity_sprites_run_projectile)
BANKREF_EXTERN(ally_summon_fox)

#define WITCH_BOLT_RANGE_TILES 4u
#define WITCH_BOLT_BURSTS      3u

// Rank scaling (index = rank; [0] = generic-scroll strength for any class)
static const uint8_t bolt_bonus_dmg[4]  = {0u, 0u, 1u, 2u}; // added to the class formula (rank 1 = legacy numbers)
static const uint8_t root_turns_rank[4] = {6u, 8u, 12u, 16u};
static const uint8_t poison_dmg_rank[4] = {1u, 2u, 3u, 4u};
static const uint8_t cat_dmg_rank[4]    = {1u, 2u, 3u, 4u};
static const uint8_t icicle_dmg_rank[4] = {2u, 3u, 4u, 5u};

#define WITCH_BOLT_OFF ((uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET))

static void push_short(const char *s); // defined below

/* Shared on-screen AoE: hit up to max_hits (0 = unlimited) live enemies inside the
   camera view for dmg, optionally rooting survivors and flinging a projectile at each.
   Returns the hit count; records the last kill in *out. */
static uint8_t witch_hit_onscreen(uint8_t px, uint8_t py, uint8_t dmg, uint8_t max_hits,
                                  uint8_t root_turns, uint8_t proj_off, AbilityResult *out) {
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
        if (root_turns && !killed && enemy_status[ei] < root_turns) enemy_status[ei] = root_turns;
        hits++;
        if (killed) { out->did_kill = 1u; out->kill_x = enemy_x[ei]; out->kill_y = enemy_y[ei]; }
    }
    return hits;
}

// Placeholder: a single fox stands in for the pair of 5-turn toad pets (allies have no
// lifespan system yet, and only one fox may exist at a time). TODO: temp-TTL toad ally type.
static void cast_toad_plague(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    (void)rank;
    if (!ally_summon_fox(px, py)) { push_short("Pet already out"); return; } // fizzle — turn/scroll kept
    sfx_fox_yip();
    push_short("Toad Plague!");
    out->consumed_turn = 1u;
}

// Placeholder: an instant poison burst across the visible enemies stands in for a
// persistent 5-tile cloud line. TODO: lay real damage-over-time cloud tiles.
static void cast_poison_wall(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    witch_hit_onscreen(px, py, poison_dmg_rank[rank], 0u, 0u, WITCH_BOLT_OFF, out);
    sfx_spell_zap();
    push_short("Poison Wall!");
    out->consumed_turn = 1u;
}

// Placeholder: swipes the 3 nearest visible foes and briefly slows (roots) them, standing
// in for the DoT. TODO: swap the belt to a cat "swipe" moveset + real bleed DoT.
static void cast_cat_form(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t hits = witch_hit_onscreen(px, py, cat_dmg_rank[rank], 3u, 2u, WITCH_BOLT_OFF, out);
    sfx_fox_yip();
    push_short(hits ? "Cat Swipe!" : "Cat Swipe");
    out->consumed_turn = 1u;
}

// Placeholder: fires an ice volley at the 3 nearest foes and freezes (roots) them 1 turn,
// standing in for the slow orb that drifts to its target. TODO: turn-by-turn moving orb entity.
static void cast_icicle_orb(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t hits = witch_hit_onscreen(px, py, icicle_dmg_rank[rank], 3u, 1u, WITCH_BOLT_OFF, out);
    sfx_spell_zap();
    push_short(hits ? "Icicle Orb!" : "Icicle Orb");
    out->consumed_turn = 1u;
}

static void push_short(const char *s) { // tiny helper — log lines are short enough to inline at call sites
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

static void cast_bolt(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed, burst, dmg;
    if (!targeting_find_nearest_visible(px, py, WITCH_BOLT_RANGE_TILES, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return; // no consumed_turn — player retains action
    }
    sfx_spell_zap();
    for (burst = 0u; burst < WITCH_BOLT_BURSTS; burst++) {
        entity_sprites_run_projectile(px, py, tx, ty,
            (uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET), PAL_XP_UI);
    }
    sfx_lunge_hit();
    if (rank == 0u) {
        dmg = 2u; // generic scroll: flat weak bolt, no crit, no class scaling
    } else {
        dmg = (uint8_t)(((player_damage + 1u) >> 1) + 1u + bolt_bonus_dmg[rank]); // half damage, rounded up, +1 base
        dmg = combat_crit_roll(dmg);
    }
    killed = combat_damage_enemy(ei, dmg, 0u);
    out->consumed_turn = 1u;
    if (killed) {
        out->did_kill = 1u;
        out->kill_x = tx;
        out->kill_y = ty;
    }
}

static void cast_swamp_root(uint8_t rank, AbilityResult *out) {
    uint8_t ei;
    uint8_t cam_tx = (uint8_t)(camera_px >> 3);
    uint8_t cam_ty = (uint8_t)(camera_py >> 3);
    for (ei = 0u; ei < num_enemies; ei++) {
        if (!enemy_alive[ei] || enemy_hidden[ei]) continue; // phased-out Ghost takes no AoE
        if (enemy_x[ei] >= cam_tx && enemy_x[ei] < (uint8_t)(cam_tx + GRID_W)
                && enemy_y[ei] >= cam_ty && enemy_y[ei] < (uint8_t)(cam_ty + GRID_H))
            enemy_status[ei] = root_turns_rank[rank];
    }
    push_short("Swamp Root!");
    out->consumed_turn = 1u;
}

BANKREF(abilities_witch_new_run_init)
void abilities_witch_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_KEY, 0);
    inventory_add(ITEM_KIND_KEY, 0); // 2× BigHeal Potion
}

BANKREF(ability_witch_cast)
void ability_witch_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    switch (spell_idx) {
        case 0u: cast_bolt(rank, px, py, out);        break;
        case 1u: cast_swamp_root(rank, out);          break;
        case 2u: cast_toad_plague(rank, px, py, out); break;
        case 3u: cast_poison_wall(rank, px, py, out); break;
        case 4u: cast_cat_form(rank, px, py, out);    break;
        case 5u: cast_icicle_orb(rank, px, py, out);  break;
        default: break;
    }
}
