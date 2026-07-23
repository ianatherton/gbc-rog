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
#include "entity_sprites.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(combat_crit_roll)
BANKREF_EXTERN(entity_sprites_run_projectile)

#define WITCH_BOLT_RANGE_TILES 4u
#define WITCH_BOLT_BURSTS      3u

// Rank scaling (index = rank; [0] = generic-scroll strength for any class)
static const uint8_t bolt_bonus_dmg[4]  = {0u, 0u, 1u, 2u}; // added to the class formula (rank 1 = legacy numbers)
static const uint8_t root_turns_rank[4] = {6u, 8u, 12u, 16u};

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
        if (!enemy_alive[ei]) continue;
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
        case 0u: cast_bolt(rank, px, py, out); break;
        case 1u: cast_swamp_root(rank, out);   break;
        default: break; // spells 2..5 not designed yet
    }
}
