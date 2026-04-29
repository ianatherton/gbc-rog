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
#include "ability_dispatch.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

static void tick_turn_cooldowns(void) {
    if (witch_shot_cooldown_turns > 0u) witch_shot_cooldown_turns--;
}

static const char *belt_name_for(uint8_t slot) { // bank-2 table — strings live here, not HOME, to keep HOME footprint tight
    if (slot != 0u) return 0;
    if (player_class == 0u && player_level >= 1u) return "holy fire shield";
    if (player_class == 2u && player_level >= 1u) return "fetid bolt";
    return 0;
}

static void push_selected_belt_description(void) {
    const char *name = belt_name_for(selected_belt_slot);
    char buf[20];
    uint8_t i = 0u;
    if (!name) return;
    while (name[i] && i < 19u) { buf[i] = name[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
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
        AbilityResult ar;
        ability_dispatch_cast_belt(selected_belt_slot, g_player_x, g_player_y, &ar);
        if (ar.did_kill) {
            wait_vbl_done();
            draw_cell(ar.kill_x, ar.kill_y); // banked ability never touches BKG; render in gameplay bank
        }
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        if (ar.consumed_turn) {
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
