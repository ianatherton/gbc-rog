#pragma bank 9

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "combat.h"
#include "enemy.h"
#include "globals.h"
#include "ui.h"
#include "music.h"
#include <gbdk/platform.h>

#define ZERKER_WHIRLWIND_COOLDOWN 6u

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
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

static void cast_whirlwind(uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei;
    uint8_t hits = 0u;
    uint8_t dmg = (player_damage > 127u) ? 255u : (uint8_t)(player_damage << 1); // clamp 2x damage into uint8

    for (ei = 0u; ei < num_enemies; ei++) {
        uint8_t ex;
        uint8_t ey;
        uint8_t dx;
        uint8_t dy;
        uint8_t killed;
        if (!enemy_alive[ei]) continue;
        ex = enemy_x[ei];
        ey = enemy_y[ei];
        dx = (ex > px) ? (uint8_t)(ex - px) : (uint8_t)(px - ex);
        dy = (ey > py) ? (uint8_t)(ey - py) : (uint8_t)(py - ey);
        if (dx > 1u || dy > 1u || (dx == 0u && dy == 0u)) continue; // adjacent 8 tiles only
        hits = 1u;
        killed = combat_damage_enemy(ei, dmg);
        if (killed) {
            out->did_kill = 1u;
            out->kill_x = ex;
            out->kill_y = ey;
        }
    }

    sfx_whirlwind_cast();
    zerker_whirlwind_cooldown_turns = ZERKER_WHIRLWIND_COOLDOWN;
    out->consumed_turn = 1u;
    if (hits) push_short("Whirlwind");
    else      push_short("Whirlwind Miss");
}

BANKREF(ability_zerker_cast_belt)
void ability_zerker_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    (void)belt_slot; // pre-dispatch UX: B always tries the class primary ability from slot 0
    if (player_level < 1u) return;
    if (zerker_whirlwind_cooldown_turns > 0u) {
        push_recharge((uint8_t)(zerker_whirlwind_cooldown_turns - 1u));
        return;
    }
    cast_whirlwind(px, py, out);
}
