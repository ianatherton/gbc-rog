#pragma bank 19 // was bank 2 — combat is per-turn, not per-frame; freed ~2.5 KB for the gameplay kernel (docs/BANKS.md)

#include "combat.h"
#include "globals.h"
#include "defs.h"
#include "enemy.h"
#include "items.h"
#include "ui.h"
#include "render.h"
#include "entity_sprites.h"
#include "music.h"
#include "map.h"
#include "camera.h"
#include "perf.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

BANKREF_EXTERN(entity_sprites_level_up_fx_trigger)
BANKREF_EXTERN(entity_sprites_enemy_hit_flash_clear)
BANKREF_EXTERN(entity_sprites_enemy_poof_begin)
BANKREF_EXTERN(entity_sprites_run_player_lunge)
BANKREF_EXTERN(entity_sprites_run_enemy_lunges_batch)
BANKREF_EXTERN(entity_sprites_player_hurt_flash)
BANKREF_EXTERN(entity_sprites_run_projectile)
BANKREF_EXTERN(enemy_try_drop_item)
BANKREF_EXTERN(enemy_slime_split)
BANKREF_EXTERN(enemy_slime_big_death_spawn)
BANKREF_EXTERN(enemy_gorgon_summon)
BANKREF_EXTERN(draw_boss_reveal_cells_far)

static void grant_xp_from_kill(uint8_t enemy_damage) {
    uint16_t next_level_xp;
    uint8_t  did_level = 0;
    player_xp = (uint16_t)(player_xp + enemy_damage);
    while (1) {
        next_level_xp = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
        if (player_xp < next_level_xp) break;
        player_xp = (uint16_t)(player_xp - next_level_xp);
        if (player_level < 255u) player_level++;
        if (player_damage < 255u) player_damage++;
        if (player_hp_max <= 245u) player_hp_max = (uint8_t)(player_hp_max + 10u);
        else player_hp_max = 255u;
        player_hp = player_hp_max;
        ui_push_level_up_line(player_level); // bank 5 — far call
        did_level = 1;
        entity_sprites_level_up_fx_trigger(); // L10 smile on aura slot — HOME
    }
    if (did_level) music_play_levelup_jingle();
}

static uint8_t s_last_was_crit = 0u;

BANKREF(combat_damage_enemy)
uint8_t combat_damage_enemy(uint8_t ei, uint8_t damage, uint8_t from_shield_burn) BANKED {
    if (enemy_hp[ei] > damage) {
        enemy_hp[ei] = (uint8_t)(enemy_hp[ei] - damage);
        if (from_shield_burn) ui_push_combat_log_shield_burn(enemy_type[ei], damage, enemy_hp[ei]);
        else                  ui_push_combat_log(enemy_type[ei], damage, enemy_hp[ei], s_last_was_crit);
        return 0u;
    } else {
        uint8_t kill_xp;
        uint8_t dx;
        uint8_t dy;
        if (from_shield_burn) ui_push_combat_log_shield_burn(enemy_type[ei], damage, 0); // killing burn — no separate DIES line
        else                  ui_push_combat_log(enemy_type[ei], damage, 0, s_last_was_crit);
        kill_xp = enemy_effective_damage(enemy_type[ei]);
        ui_push_xp_gain_line(kill_xp);
        dx = enemy_x[ei];
        dy = enemy_y[ei];
        {
            uint8_t dropped = enemy_try_drop_item(dx, dy);
            if (!dropped && num_corpses < MAX_CORPSES && !map_tile_blocks_gravestone(dx, dy)) {
                corpse_x[num_corpses] = dx;
                corpse_y[num_corpses] = dy;
                corpse_tile[num_corpses] = corpse_deco_random();
                corpse_place_slot(num_corpses, dx, dy);
                num_corpses++;
            }
        }
        enemy_clear_slot(dx, dy);
        if (enemy_type[ei] == ENEMY_GORGON || enemy_type[ei] == ENEMY_SLIME_BIG || enemy_type[ei] == ENEMY_SPHINX) enemy_clear_slot((uint8_t)(dx+1u), dy);
        enemy_alive[ei] = 0u;
        if (enemy_persistent[ei]) // transient summons/splits don't leave permanent gravestones
            floor_enemy_dead[(floor_num - 1u) * 3u + (ei >> 3u)] |= (uint8_t)(1u << (ei & 7u));
        entity_sprites_enemy_hit_flash_clear(ei);
        entity_sprites_enemy_poof_begin(ei);
        if (dead_enemy_pool_count < MAX_ENEMIES)
            dead_enemy_pool[dead_enemy_pool_count++] = ei;
        if (enemy_type[ei] == ENEMY_GORGON || enemy_type[ei] == ENEMY_SLIME_BIG || enemy_type[ei] == ENEMY_SPHINX) {
            boss_alive = 0u;
            draw_boss_reveal_cells_far(); // reveal stairs + pit now that boss/miniboss is dead
        }
        if (enemy_type[ei] == ENEMY_SLIME_BIG) enemy_slime_big_death_spawn(dx, dy); // guaranteed pop, any kill method
        grant_xp_from_kill(kill_xp);
#if GBC_ROG_DEBUG
        {
            uint8_t _rem = 0, _ei;
            for (_ei = 0u; _ei < num_enemies; _ei++)
                if (enemy_alive[_ei]) _rem++;
            {
                char _buf[UI_MSG_LINE];
                uint8_t _p = 0;
                const char *_s = "Foes left: ";
                while (*_s) _buf[_p++] = *_s++;
                if (_rem >= 10u) _buf[_p++] = (char)('0' + _rem / 10u);
                _buf[_p++] = (char)('0' + _rem % 10u);
                _buf[_p] = 0;
                ui_combat_log_push_pal(_buf, PAL_UI);
            }
        }
#endif
        return 1u;
    }
}

BANKREF(combat_crit_roll)
uint8_t combat_crit_roll(uint8_t base_damage) BANKED {
    if (player_crit_chance && (uint8_t)(DIV_REG % 100u) < player_crit_chance) {
        s_last_was_crit = 1u;
        return (base_damage > 127u) ? 255u : (uint8_t)(base_damage << 1u);
    }
    s_last_was_crit = 0u;
    return base_damage;
}

BANKREF(combat_player_attacks)
uint8_t combat_player_attacks(uint8_t ei, uint8_t px, uint8_t py, uint8_t nx, uint8_t ny) BANKED {
    int8_t adx = (nx > px) ? 1 : (nx < px ? -1 : 0);
    int8_t ady = (ny > py) ? 1 : (ny < py ? -1 : 0);
    if (enemy_type[ei] == ENEMY_SPHINX && g_sphinx_mode != SPHINX_GROUNDED) {
        entity_sprites_run_player_lunge(px, py, adx, ady, ENTITY_LUNGE_HIT_FLASH_NONE); // lunge whiffs — flyer is out of melee reach, no hit flash/sfx
        sfx_dodge_woosh();
        { // copy the literal into RAM first — a bank-19 ROM literal garbles across the banked ui call
          char msg[18]; const char *s = "SPHINX IS FLYING!"; uint8_t k = 0u;
          while ((msg[k] = s[k]) != 0) k++;
          ui_combat_log_push(msg); }
        return 0u; // not killed; ranged attacks (range >= 2) can still hit it
    }
    entity_sprites_run_player_lunge(px, py, adx, ady, ei); // plays sfx_lunge_hit at the contact frame
    {
        uint8_t killed = combat_damage_enemy(ei, combat_crit_roll(player_damage), 0u);
        if (killed) enemy_slime_split(enemy_type[ei], enemy_x[ei], enemy_y[ei], px, py);
        return killed;
    }
}

BANKREF(combat_player_melee_extras)
uint8_t combat_player_melee_extras(uint8_t ei) BANKED {
    uint8_t cleave_killed = 0u;
    /* Axe cleave: if axe equipped, hit up to 2 more adjacent enemies */
    {
        uint8_t ck;
        for (ck = 0u; ck < INVENTORY_MAX_SLOTS; ck++) {
            if (inventory_kind[ck] == ITEM_KIND_AXE && inventory_equipped[ck]) {
                uint8_t ci, hits = 0u;
                for (ci = 0u; ci < num_enemies && hits < 2u; ci++) {
                    uint8_t dx, dy;
                    if (!enemy_alive[ci] || ci == ei) continue;
                    dx = (enemy_x[ci] > g_player_x) ? (uint8_t)(enemy_x[ci] - g_player_x) : (uint8_t)(g_player_x - enemy_x[ci]);
                    dy = (enemy_y[ci] > g_player_y) ? (uint8_t)(enemy_y[ci] - g_player_y) : (uint8_t)(g_player_y - enemy_y[ci]);
                    if (dx > 1u || dy > 1u) continue;
                    if (combat_damage_enemy(ci, combat_crit_roll(player_damage), 0u)) {
                        enemy_try_drop_item(enemy_x[ci], enemy_y[ci]);
                        cleave_killed = 1u;
                    }
                    hits++;
                }
                break;
            }
        }
    }
    /* Mace stun: if mace equipped and the struck enemy survived, chance to stun it.
       (Axe cleave above never targets ei, so enemy_alive[ei] alone reflects whether
       the primary attack killed it — cleave kills land on other enemies.) */
    if (enemy_alive[ei]) {
        uint8_t ck;
        for (ck = 0u; ck < INVENTORY_MAX_SLOTS; ck++) {
            if (inventory_kind[ck] == ITEM_KIND_MACE && inventory_equipped[ck]) {
                if ((uint8_t)(DIV_REG % 100u) < MACE_STUN_CHANCE_PCT)
                    enemy_stun[ei] = MACE_STUN_TURNS;
                break;
            }
        }
    }
    return cleave_killed;
}

BANKREF(resolve_enemy_hits_and_animate)
uint8_t resolve_enemy_hits_and_animate(uint8_t px, uint8_t py) BANKED {
    uint8_t perf_stamp = perf_stamp_now();
    uint8_t a;
    uint8_t any_landed = 0u;
    if (sphinx_fire_pending) { // flying Sphinx pelts the player with the stun-glyph bolt (slot 0 = boss)
        sphinx_fire_pending = 0u;
        player_hp_prev = player_hp;
        entity_sprites_run_projectile(enemy_x[0], enemy_y[0], px, py,
            (uint8_t)(TILE_STUN_ICON_VRAM - TILESET_VRAM_OFFSET), PAL_WALL_BG);
        if (!enemy_resolve_hit(0)) { entity_sprites_player_hurt_flash(); camera_shake(); } // else dodged
        wait_vbl_done();
        draw_gameplay_overlays_profiled_far(px, py);
        if (player_hp == 0u) return 2u;
    }
    if (!enemy_attack_count) return 0;
    player_hp_prev = player_hp;
    for (a = 0; a < enemy_attack_count; a++) {
        if (!enemy_resolve_hit(enemy_attack_slots[a])) any_landed = 1u; // returns 1 on dodge
        enemy_gorgon_summon(enemy_attack_slots[a]); // no-op for non-Gorgon slots
    }
    wait_vbl_done();
    draw_gameplay_overlays_profiled_far(px, py); // HP/log in WIN; lunges are sprites — BKG unchanged here
    entity_sprites_run_enemy_lunges_batch(px, py, enemy_attack_slots, enemy_attack_count); // enemy still lunges even on a dodge
    if (any_landed) {
        sfx_lunge_hit();
        entity_sprites_player_hurt_flash();
        camera_shake();
    } else {
        sfx_dodge_woosh(); // every strike dodged — woosh, no flash/shake/crunch
    }
    if (knight_shield_active && player_hp != 0u) { // reply: 1 dmg/level + fireball — only adjacent strikers reach here, so no range gate needed
        uint8_t reflect_dmg = player_level ? player_level : 1u;
        uint8_t shield_killed = 0u;
        for (a = 0; a < enemy_attack_count; a++) {
            uint8_t ei = enemy_attack_slots[a];
            if (!enemy_alive[ei]) continue; // could've been killed earlier this frame
            entity_sprites_run_projectile(px, py, enemy_x[ei], enemy_y[ei],
                (uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET), PAL_XP_UI);
            if (combat_damage_enemy(ei, reflect_dmg, 1u)) shield_killed = 1u; // log + xp + corpse; "burned" line
        }
        wait_vbl_done();
        draw_gameplay_overlays_profiled_far(px, py);
        if (shield_killed) draw_corpse_cells_far();
    }
    perf_record(PERF_HIT_RESOLVE, perf_stamp_elapsed(&perf_stamp));
    return (player_hp == 0) ? 2u : 1u;
}
