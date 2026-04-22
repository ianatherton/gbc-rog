#pragma bank 2

#include "debug_bank.h"
#include "state_gameplay.h"
#include "globals.h"
#include "game_state.h"
#include "defs.h"
#include "map.h"
#include "enemy.h"
#include "render.h"
#include "entity_sprites.h"
#include "ui.h"
#include "lcd.h"
#include "camera.h"
#include "music.h"
#include "wall_palettes.h"
#include "class_palettes.h"
#include "combat.h"
#include "perf.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

static uint8_t abs_u8_diff(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static uint8_t witch_find_los_target(uint8_t px, uint8_t py, uint8_t *out_slot, uint8_t *out_tx, uint8_t *out_ty) {
    uint8_t best_dist = 255u;
    uint8_t i;
    for (i = 0u; i < num_enemies; i++) {
        uint8_t ex;
        uint8_t ey;
        uint8_t adx;
        uint8_t ady;
        uint8_t dist;
        int8_t stepx;
        int8_t stepy;
        uint8_t blocked = 0u;
        uint8_t s;
        uint8_t cx;
        uint8_t cy;
        if (!enemy_alive[i]) continue;
        ex = enemy_x[i];
        ey = enemy_y[i];
        adx = abs_u8_diff(ex, px);
        ady = abs_u8_diff(ey, py);
        if (!(adx == 0u || ady == 0u || adx == ady)) continue; // orthogonal or diagonal only
        dist = (adx > ady) ? adx : ady;
        if (dist == 0u || dist >= best_dist) continue;
        stepx = (ex > px) ? 1 : (ex < px ? -1 : 0);
        stepy = (ey > py) ? 1 : (ey < py ? -1 : 0);
        cx = px;
        cy = py;
        for (s = 1u; s < dist; s++) {
            cx = (uint8_t)((int16_t)cx + stepx);
            cy = (uint8_t)((int16_t)cy + stepy);
            if (!is_walkable(cx, cy)) {
                blocked = 1u;
                break;
            }
        }
        if (blocked) continue;
        best_dist = dist;
        *out_slot = i;
        *out_tx = ex;
        *out_ty = ey;
    }
    return (best_dist != 255u) ? 1u : 0u;
}

static void push_witch_no_los_line(void) {
    char buf[20];
    buf[0] = 'n';
    buf[1] = 'o';
    buf[2] = ' ';
    buf[3] = 'l';
    buf[4] = 'o';
    buf[5] = 's';
    buf[6] = 0;
    ui_combat_log_push(buf);
}

static uint8_t witch_cast_shot(uint8_t px, uint8_t py) {
    uint8_t burst_i;
    uint8_t ei;
    uint8_t tx;
    uint8_t ty;
    if (!witch_find_los_target(px, py, &ei, &tx, &ty)) {
        push_witch_no_los_line();
        return 0u;
    }
    sfx_spell_zap();
    for (burst_i = 0u; burst_i < 3u; burst_i++) {
        entity_sprites_run_projectile(px, py, tx, ty, (uint8_t)(TILE_WITCH_BOLT_VRAM - TILESET_VRAM_OFFSET), PAL_XP_UI);
    }
    sfx_lunge_hit();
    {
        uint8_t spell_damage = (uint8_t)((player_damage + 1u) >> 1);
        uint8_t killed = combat_damage_enemy(ei, spell_damage);
        if (killed) draw_cell(tx, ty);
    }
    witch_shot_cooldown_turns = 2u;
    return 1u;
}

static void tick_turn_cooldowns(void) {
    if (witch_shot_cooldown_turns > 0u) witch_shot_cooldown_turns--;
}

static void push_witch_recharge_line(uint8_t turns) {
    char buf[20];
    buf[0] = 'r';
    buf[1] = 'e';
    buf[2] = 'c';
    buf[3] = 'h';
    buf[4] = 'a';
    buf[5] = 'r';
    buf[6] = 'g';
    buf[7] = 'i';
    buf[8] = 'n';
    buf[9] = 'g';
    buf[10] = ':';
    buf[11] = ' ';
    buf[12] = (char)('0' + (turns > 9u ? 9u : turns));
    buf[13] = ' ';
    buf[14] = 't';
    buf[15] = 'u';
    buf[16] = 'r';
    buf[17] = 'n';
    buf[18] = 's';
    buf[19] = 0;
    ui_combat_log_push(buf);
}

static void push_selected_belt_description(void) {
    if (selected_belt_slot == 0u && player_class == 2u && player_level >= 1u) {
        char buf[20];
        buf[0] = 'f';
        buf[1] = 'e';
        buf[2] = 't';
        buf[3] = 'i';
        buf[4] = 'd';
        buf[5] = ' ';
        buf[6] = 'b';
        buf[7] = 'o';
        buf[8] = 'l';
        buf[9] = 't';
        buf[10] = 0;
        ui_combat_log_push(buf);
    }
}

BANKREF(state_gameplay_enter)
void state_gameplay_enter(void) BANKED {
    if (gameplay_soft_reenter) {
        gameplay_soft_reenter = 0u;
        g_prev_j = 0;
        lcd_gameplay_active = 0u;
        BANK_DBG("GP_soft");
        wait_vbl_done();
        return;
    }
    g_prev_j = 0;
    BANK_DBG("GP_enter");
    class_palettes_sprite_player_apply(); // OCP PAL_PLAYER matches player_class (title left gold on slot 2)
    perf_clear_all();
    music_play_game();
    wait_vbl_done();
    lcd_clear_display();
    load_palettes(); // restore BGP slots (PAL_UI etc.) after char create / loading — avoids blank or flat-white WIN
    level_init_display(0);
    BANK_DBG("GP_gen");
    level_generate_and_spawn(&g_player_x, &g_player_y);
    selected_belt_slot = 0u;
    witch_shot_cooldown_turns = 0u;
    BANK_DBG("GP_done");
}

BANKREF(state_gameplay_tick)
void state_gameplay_tick(void) BANKED {
    uint8_t j  = joypad();
    uint8_t nx = g_player_x, ny = g_player_y;

    if (player_hp == 0u) {
        pending_transition = TRANS_TO_GAME_OVER;
        next_state         = STATE_TRANSITION;
        g_prev_j           = j;
        wait_vbl_done();
        return;
    }

    if (!lcd_gameplay_active) {
        lcd_gameplay_active = 1u;
        window_ui_show();
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }

    if (lcd_gameplay_active && (j & J_START) && !(g_prev_j & J_START)) {
        next_state = STATE_STATS;
        g_prev_j = joypad();
        return;
    }

    if (lcd_gameplay_active && (j & J_SELECT) && (j & J_B) && !(g_prev_j & J_B)) {
        ui_panel_toggle_perf();
        g_prev_j = j;
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        return;
    }

    if (lcd_gameplay_active && (j & J_SELECT) && (j & (J_LEFT | J_RIGHT | J_UP | J_DOWN))) {
        uint8_t edge_s = (uint8_t)(j & (uint8_t)~g_prev_j);
        if (!(g_prev_j & J_SELECT)
                || (!(g_prev_j & (J_LEFT | J_RIGHT | J_UP | J_DOWN)) && (j & (J_LEFT | J_RIGHT | J_UP | J_DOWN)))) {
            look_cx = g_player_x;
            look_cy = g_player_y;
        }
        if (edge_s & J_A) {
#if GBC_ROG_DEBUG
            wall_tileset_index = (uint8_t)(wall_tileset_index + 16u);
            if (wall_tileset_index > TILE_WALL_LAST) wall_tileset_index = TILE_WALL_FIRST;
#endif
        }
        if (edge_s & J_LEFT && look_cx) look_cx--;
        if (edge_s & J_RIGHT && look_cx < active_map_w - 1u) look_cx++;
        if (edge_s & J_UP && look_cy) look_cy--;
        if (edge_s & J_DOWN && look_cy < active_map_h - 1u) look_cy++;
        {
            uint8_t ei = enemy_at(look_cx, look_cy);
            if (ei != ENEMY_DEAD) ui_panel_show_inspect(ei);
            else ui_panel_show_combat();
        }
        g_prev_j = j;
        wait_vbl_done();
#if GBC_ROG_DEBUG
        if (edge_s & J_A) draw_screen(g_player_x, g_player_y); // wall sheet cycle — BKG must repaint
        else
#endif
            draw_gameplay_overlays_profiled(g_player_x, g_player_y); // look cursor + panel only; dungeon unchanged
        return;
    }
    if (lcd_gameplay_active && (j & J_SELECT)) {
        uint8_t edge_sel = (uint8_t)(j & (uint8_t)~g_prev_j);
        if (edge_sel & J_SELECT) {
            selected_belt_slot = (uint8_t)((selected_belt_slot + 1u) % BELT_SLOT_COUNT);
            push_selected_belt_description();
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y); // belt row + selector sprite; BKG ring unchanged
        }
        g_prev_j = j;
        wait_vbl_done();
        return;
    }
    if (lcd_gameplay_active && (g_prev_j & J_SELECT) && !(j & J_SELECT)) {
        ui_panel_show_combat();
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y); // panel mode back to combat; BKG unchanged
    }

    if (lcd_gameplay_active && (j & J_B) && !(g_prev_j & J_B)) {
        if (player_class == 2u && player_level >= 1u && witch_shot_cooldown_turns == 0u) {
            uint8_t consumed_turn = witch_cast_shot(g_player_x, g_player_y);
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y);
            if (consumed_turn) {
                uint8_t old_ex[MAX_ENEMIES], old_ey[MAX_ENEMIES], old_ea[MAX_ENEMIES], k;
                uint8_t result;
                for (k = 0; k < num_enemies; k++) {
                    old_ex[k] = enemy_x[k];
                    old_ey[k] = enemy_y[k];
                    old_ea[k] = enemy_alive[k];
                }
                move_enemies(g_player_x, g_player_y);
                entity_sprites_run_enemy_glide(g_player_x, g_player_y, old_ex, old_ey, old_ea);
                result = resolve_enemy_hits_and_animate(g_player_x, g_player_y);
                if (!result) { wait_vbl_done(); draw_enemy_cells(g_player_x, g_player_y); }
                if (result == 2) {
                    pending_transition = TRANS_TO_GAME_OVER;
                    next_state         = STATE_TRANSITION;
                    g_prev_j           = j;
                    wait_vbl_done();
                    return;
                }
                tick_turn_cooldowns();
                if (ui_combat_log_tick_quiet_turn()) {
                    wait_vbl_done();
                    draw_gameplay_overlays_profiled(g_player_x, g_player_y);
                }
            }
            g_prev_j = j;
            wait_vbl_done();
            return;
        } else if (player_class == 2u && player_level >= 1u && witch_shot_cooldown_turns > 0u) {
            push_witch_recharge_line((uint8_t)(witch_shot_cooldown_turns - 1u));
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y);
            g_prev_j = j;
            wait_vbl_done();
            return;
        }
        g_prev_j = j;
        wait_vbl_done();
        return;
    }

    if (j & J_LEFT)  { nx = g_player_x > 0       ? (uint8_t)(g_player_x-1) : g_player_x; entity_sprites_set_player_facing(-1); }
    if (j & J_RIGHT) { nx = g_player_x < active_map_w-1 ? (uint8_t)(g_player_x+1) : g_player_x; entity_sprites_set_player_facing(1); }
    if (j & J_UP)    ny = g_player_y > 0         ? (uint8_t)(g_player_y-1) : g_player_y;
    if (j & J_DOWN)  ny = g_player_y < active_map_h-1   ? (uint8_t)(g_player_y+1) : g_player_y;

#if GBC_ROG_DEBUG
    if ((j & J_A) && !(g_prev_j & J_A)) {
        wall_palette_index = (uint8_t)((wall_palette_index + 1u) % NUM_WALL_PALETTES);
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }
#endif

    if (nx != g_player_x || ny != g_player_y) {
        uint8_t ei = enemy_at(nx, ny);
        uint8_t result = 0;
        uint8_t consumed_turn = 0u;

        if (ei != ENEMY_DEAD) {
            consumed_turn = 1u;
            {
                uint8_t killed = combat_player_attacks(ei, g_player_x, g_player_y, nx, ny);
                wait_vbl_done();
                if (killed) draw_cell(nx, ny); // corpse BG only; non-kill leaves terrain unchanged (enemy is sprite)
                draw_gameplay_overlays_profiled(g_player_x, g_player_y);
            }

            {
                uint8_t old_ex[MAX_ENEMIES], old_ey[MAX_ENEMIES], old_ea[MAX_ENEMIES], k;
                for (k = 0; k < num_enemies; k++) {
                    old_ex[k] = enemy_x[k];
                    old_ey[k] = enemy_y[k];
                    old_ea[k] = enemy_alive[k];
                }
                move_enemies(g_player_x, g_player_y);
                entity_sprites_run_enemy_glide(g_player_x, g_player_y, old_ex, old_ey, old_ea);
            }
            result = resolve_enemy_hits_and_animate(g_player_x, g_player_y);
            if (!result) { wait_vbl_done(); draw_enemy_cells(g_player_x, g_player_y); }
            if (result == 2) {
                pending_transition = TRANS_TO_GAME_OVER;
                next_state         = STATE_TRANSITION;
                g_prev_j           = j;
                wait_vbl_done();
                return;
            }
#if TURN_DELAY_MS > 0
            delay(TURN_DELAY_MS);
#endif
        } else {
            uint8_t t = tile_at(nx, ny);
            if (t == TILE_WALL) {
            } else if (t == TILE_PIT) {
                if (player_hp < player_hp_max) player_hp++;
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                pending_transition = TRANS_FLOOR_PIT;
                next_state         = STATE_TRANSITION;
                g_prev_j           = j;
                wait_vbl_done();
                return;
            } else {
                uint8_t opx = g_player_x, opy = g_player_y;
                consumed_turn = 1u;
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                g_player_x = nx;
                g_player_y = ny;
                lighting_reveal_radius(g_player_x, g_player_y,
                    (player_class == 1u) ? LIGHT_RADIUS_ROGUE
                    : (player_class == 2u || player_class == 3u) ? LIGHT_RADIUS_MAGE
                    : LIGHT_RADIUS_KNIGHT);
                if (player_hp < player_hp_max) player_hp++;
                {
                    uint8_t target_cx = (g_player_x > GRID_W / 2) ? (uint8_t)(g_player_x - GRID_W / 2) : 0;
                    uint8_t target_cy = (g_player_y > GRID_H / 2) ? (uint8_t)(g_player_y - GRID_H / 2) : 0;
                    if (target_cx > (uint8_t)(active_map_w - GRID_W)) target_cx = (uint8_t)(active_map_w - GRID_W);
                    if (target_cy > (uint8_t)(active_map_h - GRID_H)) target_cy = (uint8_t)(active_map_h - GRID_H);
                    camera_scroll_to(target_cx, target_cy, opx, opy, g_player_x, g_player_y);
                }
                {
                    uint8_t old_ex[MAX_ENEMIES], old_ey[MAX_ENEMIES], old_ea[MAX_ENEMIES], k;
                    for (k = 0; k < num_enemies; k++) {
                        old_ex[k] = enemy_x[k];
                        old_ey[k] = enemy_y[k];
                        old_ea[k] = enemy_alive[k];
                    }
                    move_enemies(g_player_x, g_player_y);
                    entity_sprites_run_enemy_glide(g_player_x, g_player_y, old_ex, old_ey, old_ea);
                }
                result = resolve_enemy_hits_and_animate(g_player_x, g_player_y);
                wait_vbl_done();
#if FEATURE_MAP_FOG
                if (lighting_dirty_overflow())
                    draw_screen(g_player_x, g_player_y);
                else {
                    uint8_t ni, dmx, dmy;
                    for (ni = 0u; ni < lighting_dirty_count(); ni++) {
                        lighting_dirty_tile(ni, &dmx, &dmy);
                        draw_cell(dmx, dmy);
                    }
                    draw_gameplay_overlays_profiled(g_player_x, g_player_y);
                }
                lighting_dirty_clear();
#else
                draw_gameplay_overlays_profiled(g_player_x, g_player_y);
#endif

                if (result == 2) {
                    pending_transition = TRANS_TO_GAME_OVER;
                    next_state         = STATE_TRANSITION;
                    g_prev_j           = j;
                    wait_vbl_done();
                    return;
                }
#if TURN_DELAY_MS > 0
                delay(TURN_DELAY_MS);
#endif
            }
        }
        if (consumed_turn && ui_combat_log_tick_quiet_turn()) {
            tick_turn_cooldowns();
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y); // reclaim panel text only
        } else if (consumed_turn) {
            tick_turn_cooldowns();
        }
    }

    if (enemy_anim_update()) {
        wait_vbl_done();
        draw_enemy_cells(g_player_x, g_player_y);
    }
    g_prev_j = j;
    wait_vbl_done();
}
