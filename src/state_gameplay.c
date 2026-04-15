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
#include "combat.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

static const char *const s_class_names[3] = { "KNIGHT", "ROGUE", "MAGE" };

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
    music_play_game();
    wait_vbl_done();
    lcd_clear_display();
    level_init_display(0);
    BANK_DBG("GP_gen");
    level_generate_and_spawn(&g_player_x, &g_player_y);
    BANK_DBG("GP_done");
}

BANKREF(state_gameplay_tick)
void state_gameplay_tick(void) BANKED {
    uint8_t j  = joypad();
    uint8_t nx = g_player_x, ny = g_player_y;

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

    if (lcd_gameplay_active && (j & J_SELECT)) {
        uint8_t edge_s = (uint8_t)(j & (uint8_t)~g_prev_j);
        if (!(g_prev_j & J_SELECT)) {
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
        if (edge_s & J_RIGHT && look_cx < MAP_W - 1u) look_cx++;
        if (edge_s & J_UP && look_cy) look_cy--;
        if (edge_s & J_DOWN && look_cy < MAP_H - 1u) look_cy++;
        {
            uint8_t ei = enemy_at(look_cx, look_cy);
            if (ei != ENEMY_DEAD) ui_panel_show_inspect(ei);
            else ui_panel_show_combat();
        }
        g_prev_j = j;
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
        return;
    }
    if (lcd_gameplay_active && (g_prev_j & J_SELECT) && !(j & J_SELECT)) {
        ui_panel_show_combat();
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }

    if (j & J_LEFT)  { nx = g_player_x > 0       ? (uint8_t)(g_player_x-1) : g_player_x; entity_sprites_set_player_facing(-1); }
    if (j & J_RIGHT) { nx = g_player_x < MAP_W-1 ? (uint8_t)(g_player_x+1) : g_player_x; entity_sprites_set_player_facing(1); }
    if (j & J_UP)    ny = g_player_y > 0         ? (uint8_t)(g_player_y-1) : g_player_y;
    if (j & J_DOWN)  ny = g_player_y < MAP_H-1   ? (uint8_t)(g_player_y+1) : g_player_y;

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

        if (ei != ENEMY_DEAD) {
            combat_idle_turns = 0;
            combat_player_attacks(ei, g_player_x, g_player_y, nx, ny);
            wait_vbl_done();
            draw_screen(g_player_x, g_player_y);

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
                if (combat_idle_turns < 255u) combat_idle_turns++;
                if (combat_idle_turns == 5u) {
                    ui_combat_log_clear();
                    ui_combat_log_push(s_class_names[(unsigned)player_class % 3u]);
                    { char lb[5]; lb[0]='L'; lb[1]='V';
                      lb[2]=(char)('0'+player_level%10u); lb[3]=0;
                      if (player_level>=10u) { lb[3]=lb[2]; lb[2]=(char)('0'+player_level/10u); lb[4]=0; }
                      ui_combat_log_push(lb); }
                }
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                g_player_x = nx;
                g_player_y = ny;
                lighting_reveal_radius(g_player_x, g_player_y, PLAYER_LIGHT_RADIUS);
                if (player_hp < player_hp_max) player_hp++;
                {
                    uint8_t target_cx = (g_player_x > GRID_W / 2) ? (uint8_t)(g_player_x - GRID_W / 2) : 0;
                    uint8_t target_cy = (g_player_y > GRID_H / 2) ? (uint8_t)(g_player_y - GRID_H / 2) : 0;
                    if (target_cx > (uint8_t)(MAP_W - GRID_W)) target_cx = (uint8_t)(MAP_W - GRID_W);
                    if (target_cy > (uint8_t)(MAP_H - GRID_H)) target_cy = (uint8_t)(MAP_H - GRID_H);
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
                if (result) combat_idle_turns = 0;
                wait_vbl_done();
                if (result) draw_screen(g_player_x, g_player_y);
                else        draw_enemy_cells(g_player_x, g_player_y);

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
    }

    if (enemy_anim_update()) {
        wait_vbl_done();
        draw_enemy_cells(g_player_x, g_player_y);
    }
    g_prev_j = j;
    wait_vbl_done();
}
