#pragma bank 2

#include "combat.h"
#include "globals.h"
#include "defs.h"
#include "enemy.h"
#include "ui.h"
#include "render.h"
#include "entity_sprites.h"
#include "music.h"
#include "camera.h"
#include <gb/gb.h>

#define MSG_LINE 20u // matches ui combat log row cap

static void push_xp_gain_line(uint8_t amt) {
    char buf[MSG_LINE];
    uint8_t p = 0;
    uint16_t need = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
    uint16_t pct16 = need ? (((uint16_t)amt * 100u) / need) : 0u;
    uint8_t pct = (pct16 > 100u) ? 100u : (uint8_t)pct16;
    buf[p++] = '+';
    if (amt >= 100u) {
        buf[p++] = (char)('0' + amt / 100u);
        buf[p++] = (char)('0' + (amt % 100u) / 10u);
        buf[p++] = (char)('0' + amt % 10u);
    } else if (amt >= 10u) {
        buf[p++] = (char)('0' + amt / 10u);
        buf[p++] = (char)('0' + amt % 10u);
    } else {
        buf[p++] = (char)('0' + amt);
    }
    buf[p++] = ' ';
    buf[p++] = 'X';
    buf[p++] = 'P';
    buf[p++] = ' ';
    buf[p++] = '(';
    if (pct >= 100u) {
        buf[p++] = '1';
        buf[p++] = '0';
        buf[p++] = '0';
    } else if (pct >= 10u) {
        buf[p++] = (char)('0' + pct / 10u);
        buf[p++] = (char)('0' + pct % 10u);
    } else {
        buf[p++] = (char)('0' + pct);
    }
    buf[p++] = '%';
    buf[p++] = ')';
    buf[p] = 0;
    ui_combat_log_push_pal(buf, PAL_XP_UI);
}

static void push_level_up_line(uint8_t new_level) {
    char buf[MSG_LINE];
    uint8_t i = 0;
    const char *s = "You level up! (";
    while (*s) buf[i++] = *s++;
    if (new_level >= 100u) {
        buf[i++] = (char)('0' + new_level / 100u);
        buf[i++] = (char)('0' + (new_level % 100u) / 10u);
        buf[i++] = (char)('0' + new_level % 10u);
    } else if (new_level >= 10u) {
        buf[i++] = (char)('0' + new_level / 10u);
        buf[i++] = (char)('0' + new_level % 10u);
    } else {
        buf[i++] = (char)('0' + new_level);
    }
    buf[i++] = ')';
    buf[i] = 0;
    ui_combat_log_push(buf);
}

void push_combat_log(uint8_t type_idx, uint8_t dmg, uint8_t hp_remaining_for_pct) {
    char logbuf[MSG_LINE];
    const char *name = enemy_type_short_name(type_idx);
    uint8_t p = 0, d = dmg, mhp, pct;
    while (*name && p < 9u) logbuf[p++] = *name++;
    logbuf[p++] = ' ';
    if (d) {
        logbuf[p++] = '-';
        if (d >= 100u) {
            logbuf[p++] = (char)('0' + d / 100u);
            d %= 100u;
        }
        if (d >= 10u) {
            logbuf[p++] = (char)('0' + d / 10u);
            d %= 10u;
        }
        logbuf[p++] = (char)('0' + d);
        mhp = enemy_effective_max_hp(type_idx);
        pct = 0;
        if (mhp) pct = (uint8_t)(((uint16_t)hp_remaining_for_pct * 100u) / (uint16_t)mhp);
        if (pct > 99u) pct = 99u;
        logbuf[p++] = ' ';
        if (pct >= 10u) {
            logbuf[p++] = (char)('0' + pct / 10u);
            logbuf[p++] = (char)('0' + pct % 10u);
        } else {
            logbuf[p++] = (char)('0' + pct);
        }
        logbuf[p++] = '%';
    } else {
        logbuf[p++] = 'D';
        logbuf[p++] = 'I';
        logbuf[p++] = 'E';
        logbuf[p++] = 'S';
    }
    logbuf[p] = 0;
    ui_combat_log_push(logbuf);
}

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
        push_level_up_line(player_level);
        did_level = 1;
    }
    if (did_level) music_play_levelup_jingle();
}

uint8_t resolve_enemy_hits_and_animate(uint8_t px, uint8_t py) {
    uint8_t a;
    if (!enemy_attack_count) return 0;
    for (a = 0; a < enemy_attack_count; a++)
        enemy_resolve_hit(enemy_attack_slots[a]);
    wait_vbl_done();
    draw_screen(px, py);
    sfx_lunge_hit();
    entity_sprites_run_enemy_lunges_batch(px, py, enemy_attack_slots, enemy_attack_count);
    entity_sprites_player_hurt_flash();
    camera_shake();
    return (player_hp == 0) ? 2u : 1u;
}

void combat_player_attacks(uint8_t ei, uint8_t px, uint8_t py, uint8_t nx, uint8_t ny) {
    int8_t adx = (nx > px) ? 1 : (nx < px ? -1 : 0);
    int8_t ady = (ny > py) ? 1 : (ny < py ? -1 : 0);
    entity_sprites_run_player_lunge(px, py, adx, ady, ei);
    sfx_lunge_hit();
    if (enemy_hp[ei] > player_damage) {
        enemy_hp[ei] = (uint8_t)(enemy_hp[ei] - player_damage);
        push_combat_log(enemy_type[ei], player_damage, enemy_hp[ei]);
    } else {
        push_combat_log(enemy_type[ei], 0, 0);
        uint8_t kill_xp = enemy_effective_damage(enemy_type[ei]);
        push_xp_gain_line(kill_xp);
        uint8_t dx = enemy_x[ei], dy = enemy_y[ei];
        if (num_corpses < MAX_CORPSES) {
            corpse_x[num_corpses] = dx;
            corpse_y[num_corpses] = dy;
            corpse_tile[num_corpses] = corpse_deco_random();
            BIT_SET(corpse_occ, TILE_IDX(dx, dy));
            num_corpses++;
        }
        enemy_clear_slot(dx, dy);
        enemy_alive[ei] = 0u;
        entity_sprites_enemy_hit_flash_clear(ei);
        entity_sprites_enemy_poof_begin(ei);
        if (dead_enemy_pool_count < MAX_ENEMIES)
            dead_enemy_pool[dead_enemy_pool_count++] = ei;
        grant_xp_from_kill(kill_xp);
    }
}
