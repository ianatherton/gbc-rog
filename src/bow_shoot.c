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

#define BOW_RANGE_TILES 4u        // same reach as the witch bolt
#define BOW_SNIPER_RANGE_TILES 8u // while sniper_turns > 0 (Sniper Mode buff)
#define WAND_RANGE_TILES 4u       // wand matches the bow's base reach (no Sniper Mode — that stays bow-only)
#define WAND_BOLT_OFF ((uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET)) // M12 fetid-bolt art

static void push_short(const char *s) { // log lines are short — inline copy into a small buffer
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

/* Shared core for every "belt stack item that shoots the nearest visible enemy". The bow and the
   wand differ only in reach, the icon that pops out, the projectile tile+palette, whether the shot
   lands at full player damage, and which secondary stat adds its flat bonus. */
static void shoot_nearest(uint8_t range, uint8_t icon_kind, uint8_t tile_off,
                          uint8_t pal, uint8_t full_dmg, uint8_t flat_bonus,
                          AbilityResult *out) {
    uint8_t ei, tx, ty, too_far, killed, dmg;
    uint8_t px = g_player_x, py = g_player_y;
    if (!targeting_find_nearest_visible(px, py, range, &ei, &tx, &ty, &too_far)) {
        push_short(too_far ? "too far" : "no los");
        return; // no consumed_turn → charge not spent, player keeps the turn
    }
    entity_sprites_run_item_popout(icon_kind); // item icon holds beside the hero, then the shot flies
    sfx_spell_zap();
    entity_sprites_run_projectile(px, py, tx, ty, tile_off, pal);
    sfx_lunge_hit();
    /* Half damage rounded up (full while Sniper Mode is up), plus the weapon's secondary stat as a
       flat integer — the bow scales off CRIT%, the wand off MAGDEF%. uint16 intermediate:
       player_damage reaches 255 and the bonus 100, so the sum overflows uint8 before the crit roll
       ever sees it. */
    {
        uint16_t d = full_dmg ? (uint16_t)player_damage
                              : (uint16_t)((player_damage + 1u) >> 1);
        d += (uint16_t)flat_bonus;
        dmg = (d > 255u) ? 255u : (uint8_t)d;
    }
    dmg = combat_crit_roll(dmg); // crit doubles the whole sum and clamps at 255
    killed = combat_damage_enemy(ei, dmg, 0u);
    out->consumed_turn = 1u;
    if (killed) {
        out->did_kill = 1u;
        out->kill_x = tx;
        out->kill_y = ty;
    }
}

void bow_shoot_use(AbilityResult *out) BANKED {
    shoot_nearest(sniper_turns ? BOW_SNIPER_RANGE_TILES : BOW_RANGE_TILES,
                  ITEM_KIND_BOW,
                  (uint8_t)(TILE_ARROW_VRAM - TILESET_VRAM_OFFSET), // H12 arrow, bat ramp
                  PAL_ENEMY_BAT,
                  sniper_turns ? 1u : 0u,
                  player_crit_chance, out); // bow scales off CRIT%
}

void wand_shoot_use(AbilityResult *out) BANKED {
    shoot_nearest(WAND_RANGE_TILES, ITEM_KIND_WAND,
                  WAND_BOLT_OFF, PAL_XP_UI, // M12 fetid bolt, same tile+palette the witch spell uses
                  0u, player_magdef, out);  // wand scales off MAGDEF%
}
