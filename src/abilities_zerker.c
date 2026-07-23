#pragma bank 9

#include "abilities_class.h"
#include "ability_dispatch.h"
#include "combat.h"
#include "enemy.h"
#include "items.h"
#include "globals.h"
#include "ui.h"
#include "music.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(combat_crit_roll)

static void push_short(const char *s) {
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

static void cast_whirlwind(uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t ei;
    uint8_t hits = 0u;
    uint8_t dmg;
    if (rank == 0u) dmg = player_damage; // generic scroll: 1x base, no crit — rank scaling is in the cooldown curve
    else dmg = combat_crit_roll((player_damage > 127u) ? 255u : (uint8_t)(player_damage << 1)); // 2x base; crit doubles again

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
        killed = combat_damage_enemy(ei, dmg, 0u);
        if (killed) {
            out->did_kill = 1u;
            out->kill_x = ex;
            out->kill_y = ey;
        }
    }

    sfx_whirlwind_cast();
    out->consumed_turn = 1u;
    if (hits) push_short("Whirlwind");
    else      push_short("Whirlwind Miss");
}

BANKREF(abilities_zerker_new_run_init)
void abilities_zerker_new_run_init(void) BANKED {
    inventory_add(ITEM_KIND_AXE, 0);
}

BANKREF(ability_zerker_cast)
void ability_zerker_cast(uint8_t spell_idx, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) BANKED {
    switch (spell_idx) {
        case 0u: cast_whirlwind(rank, px, py, out); break;
        default: break; // spells 1..5 not designed yet
    }
}
