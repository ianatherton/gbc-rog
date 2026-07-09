#pragma bank 16

#include "bow_shoot.h"
#include "globals.h"
#include "enemy.h"
#include "combat.h"
#include "defs.h"
#include "targeting.h"
#include "entity_sprites.h"
#include "items.h"
#include "ui.h"
#include "music.h"

BANKREF_EXTERN(combat_damage_enemy)
BANKREF_EXTERN(combat_crit_roll)
BANKREF_EXTERN(entity_sprites_run_projectile)
BANKREF_EXTERN(entity_sprites_run_item_popout)

#define BOW_RANGE_TILES 4u // same reach as the witch bolt

static void push_short(const char *s) { // log lines are short — inline copy into a small buffer
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

void bow_shoot_use(AbilityResult *out) BANKED {
    uint8_t ei, tx, ty, too_far, killed, dmg;
    uint8_t px = g_player_x, py = g_player_y;
    if (!targeting_find_nearest_visible(px, py, BOW_RANGE_TILES, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return; // no consumed_turn → arrow not spent, player keeps the turn
    }
    entity_sprites_run_item_popout(ITEM_KIND_BOW); // bow icon holds beside the hero, then the arrow flies
    sfx_spell_zap();
    entity_sprites_run_projectile(px, py, tx, ty,
        (uint8_t)(TILE_ARROW_VRAM - TILESET_VRAM_OFFSET), PAL_ENEMY_BAT); // H12 arrow, bat ramp
    sfx_lunge_hit();
    dmg = (uint8_t)((player_damage + 1u) >> 1); // half damage, rounded up — mirrors the witch bolt
    dmg = combat_crit_roll(dmg);
    killed = combat_damage_enemy(ei, dmg, 0u);
    out->consumed_turn = 1u;
    if (killed) {
        out->did_kill = 1u;
        out->kill_x = tx;
        out->kill_y = ty;
    }
}
