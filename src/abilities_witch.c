#pragma bank 8

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "targeting.h"
#include "combat.h"
#include "globals.h"
#include "defs.h"
#include "ui.h"
#include "entity_sprites.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)

#define WITCH_BOLT_RANGE_TILES 4u
#define WITCH_BOLT_BURSTS      3u
#define WITCH_BOLT_COOLDOWN    2u

static void push_short(const char *s) { // tiny helper — log lines are short enough to inline at call sites
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

static void cast_bolt(uint8_t px, uint8_t py, AbilityResult *out) {
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
    dmg = (uint8_t)((player_damage + 1u) >> 1); // half damage, rounded up
    killed = combat_damage_enemy(ei, dmg, 0u);
    witch_shot_cooldown_turns = WITCH_BOLT_COOLDOWN;
    out->consumed_turn = 1u;
    if (killed) {
        out->did_kill = 1u;
        out->kill_x = tx;
        out->kill_y = ty;
    }
}

static void push_recharge(uint8_t turns) {
    char buf[20];
    const char *s = "Recharging: ";
    uint8_t i = 0u;
    while (*s) buf[i++] = *s++;
    buf[i++] = (char)('0' + (turns > 9u ? 9u : turns));
    s = " turns";
    while (*s) buf[i++] = *s++;
    buf[i] = 0;
    ui_combat_log_push(buf);
}

BANKREF(ability_witch_cast_belt)
void ability_witch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot; // pre-dispatch UX: B always tries witch bolt regardless of selected slot — matches legacy behavior until other slots are wired
    if (player_level < 1u) return;
    if (witch_shot_cooldown_turns > 0u) {
        push_recharge((uint8_t)(witch_shot_cooldown_turns - 1u));
        return;
    }
    cast_bolt(px, py, out);
}
