#include "combat.h"
#include "globals.h"
#include "defs.h"
#include "enemy.h"
#include "ui.h"
#include "render.h"
#include "entity_sprites.h"
#include "music.h"
#include "camera.h"
#include "perf.h"
#include <gb/gb.h>

void grant_xp_from_kill(uint8_t enemy_damage) {
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
        ui_push_level_up_line(player_level); // bank 5 — keeps HOME thin
        did_level = 1;
    }
    if (did_level) music_play_levelup_jingle();
}

uint8_t resolve_enemy_hits_and_animate(uint8_t px, uint8_t py) {
    uint8_t perf_stamp = perf_stamp_now();
    uint8_t a;
    if (!enemy_attack_count) return 0;
    for (a = 0; a < enemy_attack_count; a++)
        enemy_resolve_hit(enemy_attack_slots[a]);
    wait_vbl_done();
    draw_gameplay_overlays_profiled(px, py); // HP/log in WIN; lunges are sprites — BKG unchanged here
    sfx_lunge_hit();
    entity_sprites_run_enemy_lunges_batch(px, py, enemy_attack_slots, enemy_attack_count);
    entity_sprites_player_hurt_flash();
    camera_shake();
    if (knight_shield_active && player_hp != 0u) { // reply: 1 dmg/level + fireball — only adjacent strikers reach here, so no range gate needed
        uint8_t reflect_dmg = player_level ? player_level : 1u;
        for (a = 0; a < enemy_attack_count; a++) {
            uint8_t ei = enemy_attack_slots[a];
            if (!enemy_alive[ei]) continue; // could've been killed earlier this frame
            entity_sprites_run_projectile(px, py, enemy_x[ei], enemy_y[ei],
                (uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET), PAL_XP_UI);
            (void)combat_damage_enemy(ei, reflect_dmg, 1u); // log + xp + corpse; "burned" line
        }
        wait_vbl_done();
        draw_gameplay_overlays_profiled(px, py);
    }
    perf_record(PERF_HIT_RESOLVE, perf_stamp_elapsed(&perf_stamp));
    return (player_hp == 0) ? 2u : 1u;
}

uint8_t combat_damage_enemy(uint8_t ei, uint8_t damage, uint8_t from_shield_burn) {
    if (enemy_hp[ei] > damage) {
        enemy_hp[ei] = (uint8_t)(enemy_hp[ei] - damage);
        if (from_shield_burn) ui_push_combat_log_shield_burn(enemy_type[ei], damage, enemy_hp[ei]);
        else                  ui_push_combat_log(enemy_type[ei], damage, enemy_hp[ei]);
        return 0u;
    } else {
        uint8_t kill_xp;
        uint8_t dx;
        uint8_t dy;
        if (from_shield_burn) ui_push_combat_log_shield_burn(enemy_type[ei], damage, 0); // killing burn — no separate DIES line
        else                  ui_push_combat_log(enemy_type[ei], 0, 0);
        kill_xp = enemy_effective_damage(enemy_type[ei]);
        ui_push_xp_gain_line(kill_xp);
        dx = enemy_x[ei];
        dy = enemy_y[ei];
        if (num_corpses < MAX_CORPSES) {
            corpse_x[num_corpses] = dx;
            corpse_y[num_corpses] = dy;
            corpse_tile[num_corpses] = corpse_deco_random();
            corpse_place_slot(num_corpses, dx, dy);
            num_corpses++;
        }
        enemy_clear_slot(dx, dy);
        enemy_alive[ei] = 0u;
        entity_sprites_enemy_hit_flash_clear(ei);
        entity_sprites_enemy_poof_begin(ei);
        if (dead_enemy_pool_count < MAX_ENEMIES)
            dead_enemy_pool[dead_enemy_pool_count++] = ei;
        grant_xp_from_kill(kill_xp);
        return 1u;
    }
}

uint8_t combat_player_attacks(uint8_t ei, uint8_t px, uint8_t py, uint8_t nx, uint8_t ny) {
    int8_t adx = (nx > px) ? 1 : (nx < px ? -1 : 0);
    int8_t ady = (ny > py) ? 1 : (ny < py ? -1 : 0);
    entity_sprites_run_player_lunge(px, py, adx, ady, ei);
    sfx_lunge_hit();
    return combat_damage_enemy(ei, player_damage, 0u);
}
