#pragma bank 2 // with map/render/draw — level_init and map_gen (generate_level) moved to bank 10

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
#include "biome.h"
#include "ally.h"
#include "items.h"
#include "story_ui.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

BANKREF_EXTERN(ally_fox_turn_tick)
BANKREF_EXTERN(story_ui_run_before_first_floor)
BANKREF_EXTERN(ally_fox_run_glide)
BANKREF_EXTERN(entity_sprites_run_enemy_glide)
BANKREF_EXTERN(entity_sprites_run_enemy_glide_finish)
BANKREF_EXTERN(entity_sprites_enemy_glide_begin)
BANKREF_EXTERN(entity_sprites_ally_glide_begin)
BANKREF_EXTERN(entity_sprites_set_player_facing)

static uint8_t turn_snap_ex[MAX_ENEMIES], turn_snap_ey[MAX_ENEMIES], turn_snap_ea[MAX_ENEMIES]; // enemy pos before AI — file static so SDCC does not stack three 28-byte arrays in one tick()

static void tick_turn_cooldowns(void) {
    if (witch_shot_cooldown_turns > 0u) witch_shot_cooldown_turns--;
    if (zerker_whirlwind_cooldown_turns > 0u) zerker_whirlwind_cooldown_turns--;
    if (book_heal_cooldown_turns > 0u) book_heal_cooldown_turns--;
}

static void gameplay_allies_turn_and_glide(uint8_t px, uint8_t py) {
    uint8_t snap_x[MAX_ALLIES], snap_y[MAX_ALLIES], snap_a[MAX_ALLIES];
    uint8_t i;
    uint8_t fk = 0u;
    for (i = 0u; i < MAX_ALLIES; i++) {
        snap_x[i] = ally_x[i];
        snap_y[i] = ally_y[i];
        snap_a[i] = ally_active[i];
    }
    for (i = 0u; i < MAX_ALLIES; i++) {
        if (!snap_a[i]) continue;
        switch (ally_type[i]) {
        case ALLY_TYPE_FOX:
            fk |= ally_fox_turn_tick(i, px, py);
            break;
        default:
            break;
        }
    }
    if (fk) { wait_vbl_done(); draw_enemy_cells(px, py); draw_corpse_cells(); }
    for (i = 0u; i < MAX_ALLIES; i++) {
        if (!ally_active[i] || !snap_a[i]) continue;
        if (snap_x[i] != ally_x[i] || snap_y[i] != ally_y[i]) {
            switch (ally_type[i]) {
            case ALLY_TYPE_FOX:
                ally_fox_run_glide(i, snap_x[i], snap_y[i]);
                break;
            default:
                break;
            }
        }
    }
}

// Strings live in HOME (ability_dispatch.c) — always mapped, safe to return from bank 2.
static const char *belt_name_for(uint8_t slot) {
    if (slot == 1u && player_class == 2u && player_level >= 3u) return ability_name_swamp_root;
    if (slot != 0u) return 0;
    if (player_class == 0u) return ability_name_holy_fire_shield;
    if (player_class == 1u) return ability_name_call_fox;
    if (player_class == 2u) return ability_name_fetid_bolt;
    if (player_class == 3u) return ability_name_whirlwind;
    return 0;
}

static uint8_t select_hold_ticks;

static uint8_t belt_slot_nonempty(uint8_t slot) {
    if (slot < BELT_SLOT_COUNT) return belt_name_for(slot) != 0;
    {
        uint8_t kind = inventory_kind[slot - BELT_SLOT_COUNT];
        return kind != ITEM_KIND_NONE;
    }
}

static void belt_select_advance_skip_empty(void) {
    uint8_t i;
    for (i = 0u; i < BELT_TOTAL_SLOTS; i++) {
        selected_belt_slot = (uint8_t)((selected_belt_slot + 1u) % BELT_TOTAL_SLOTS);
        if (belt_slot_nonempty(selected_belt_slot)) break;
    }
}

static void push_selected_belt_description(void) {
    char buf[20];
    uint8_t i = 0u;
    if (selected_belt_slot < BELT_SLOT_COUNT) {
        const char *name = belt_name_for(selected_belt_slot);
        if (!name) return;
        while (name[i] && i < 19u) { buf[i] = name[i]; i++; }
        buf[i] = 0;
        ui_combat_log_push(buf);
    } else {
        uint8_t belt_idx = selected_belt_slot - BELT_SLOT_COUNT;
        uint8_t kind = inventory_kind[belt_idx];
        if (kind == ITEM_KIND_NONE) return;
        items_kind_display_name_copy(kind, inventory_mod_level[belt_idx], buf, sizeof buf);
        if (items_kind_category(kind) == ITEM_CAT_CONSUMABLE) {
            uint8_t p = 0u, split, cnt = inventory_count[selected_belt_slot - BELT_SLOT_COUNT];
            while (buf[p]) p++;
            split = (uint8_t)(p + 1u); // "x" starts after the space
            buf[p++] = ' '; buf[p++] = 'x';
            if (cnt >= 100u) { buf[p++] = (char)('0' + cnt / 100u); cnt = (uint8_t)(cnt % 100u); }
            if (cnt >= 10u)  { buf[p++] = (char)('0' + cnt / 10u);  cnt = (uint8_t)(cnt % 10u);  }
            buf[p++] = (char)('0' + cnt);
            buf[p] = 0;
            ui_combat_log_push_gold_suffix(buf, split);
        } else {
            ui_combat_log_push(buf);
        }
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
    wait_vbl_done();
    lcd_clear_display();
    load_palettes(); // restore BGP slots (PAL_UI etc.) after char create / loading — avoids blank or flat-white WIN
    story_ui_run_before_first_floor(); // bank 14 — intro crawl before descending loading screen
    level_init_display(0); // sets floor_num = 1 before BGM pick
    // music_begin_floor_bgm(); // TEMP: music only on story screen
    BANK_DBG("GP_gen");
    level_generate_and_spawn(&g_player_x, &g_player_y);
    selected_belt_slot = 0u;
    player_hp_prev = player_hp;
    witch_shot_cooldown_turns = 0u;
    zerker_whirlwind_cooldown_turns = 0u;
    book_heal_cooldown_turns = 0u;
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
        apply_field_palette(); // restore slot 0 field color (a menu may have blanked it to black)
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }

    if (lcd_gameplay_active && (j & J_START) && !(g_prev_j & J_START)) {
        next_state = STATE_INVENTORY;
        g_prev_j = j;
        return;
    }

    if (lcd_gameplay_active && (j & J_SELECT) && (j & J_B) && !(g_prev_j & J_B)) {
        ui_panel_toggle_perf();
        g_prev_j = j;
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        return;
    }

    if (lcd_gameplay_active && floor_num != 0u && (j & J_SELECT) && (j & (J_LEFT | J_RIGHT | J_UP | J_DOWN))) { // no look/inspect on the hub
        uint8_t edge_s = (uint8_t)(j & (uint8_t)~g_prev_j);
        if (!(g_prev_j & J_SELECT)
                || (!(g_prev_j & (J_LEFT | J_RIGHT | J_UP | J_DOWN)) && (j & (J_LEFT | J_RIGHT | J_UP | J_DOWN)))) {
            look_cx = g_player_x;
            look_cy = g_player_y;
        }
#if GBC_ROG_DEBUG
        if (edge_s & J_A) {
            wall_tileset_index = (uint8_t)(wall_tileset_index + 16u);
            if (wall_tileset_index > TILE_WALL_LAST) wall_tileset_index = TILE_WALL_FIRST;
        }
#endif
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
#define SELECT_HOLD_RESET_FRAMES 30u // ~1 s at 60 Hz VBL
    if (lcd_gameplay_active && floor_num != 0u && (j & J_SELECT)) { // belt is hidden on the hub — no cycling/use there
        uint8_t edge_sel = (uint8_t)(j & (uint8_t)~g_prev_j);
        if (edge_sel & J_SELECT) {
            belt_select_advance_skip_empty();
            push_selected_belt_description();
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y); // belt row + selector sprite; BKG ring unchanged
        } else if (++select_hold_ticks == SELECT_HOLD_RESET_FRAMES) {
            selected_belt_slot = 0u;
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        }
        g_prev_j = j;
        wait_vbl_done();
        return;
    }
    if (lcd_gameplay_active && floor_num != 0u && (g_prev_j & J_SELECT) && !(j & J_SELECT)) {
        select_hold_ticks = 0u;
        ui_panel_show_combat();
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y); // panel mode back to combat; BKG unchanged
    }

    if (lcd_gameplay_active && floor_num != 0u && (j & J_B) && !(g_prev_j & J_B)) { // no spell/item use on the hub (avoid accidental belt use)
        AbilityResult ar;
        if (selected_belt_slot < BELT_SLOT_COUNT)
            ability_dispatch_cast_belt(selected_belt_slot, g_player_x, g_player_y, &ar);
        else
            items_use_belt((uint8_t)(selected_belt_slot - BELT_SLOT_COUNT), &ar);
        if (pending_transition != TRANS_NONE) { // Port scroll: leave this floor now, before any enemy turn
            next_state = STATE_TRANSITION;
            g_prev_j   = j;
            wait_vbl_done();
            return;
        }
        if (ar.did_kill) {
            wait_vbl_done();
            draw_enemy_cells(g_player_x, g_player_y);
            draw_corpse_cells();
        }
#if FEATURE_MAP_FOG
        if (ar.lighting_refresh) {
            uint8_t ni, dmx, dmy;
            wait_vbl_done();
            lighting_reveal_radius(g_player_x, g_player_y, player_light_radius());
            if (lighting_dirty_overflow())
                draw_screen(g_player_x, g_player_y);
            else {
                for (ni = 0u; ni < lighting_dirty_count(); ni++) {
                    lighting_dirty_tile(ni, &dmx, &dmy);
                    draw_cell(dmx, dmy);
                }
            }
            lighting_dirty_clear();
        }
#endif
        wait_vbl_done();
        draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        if (ar.consumed_turn) {
            uint8_t k;
            uint8_t result;
            gameplay_allies_turn_and_glide(g_player_x, g_player_y);
            for (k = 0; k < num_enemies; k++) {
                turn_snap_ex[k] = enemy_x[k];
                turn_snap_ey[k] = enemy_y[k];
                turn_snap_ea[k] = enemy_alive[k];
            }
            move_enemies(g_player_x, g_player_y);
            entity_sprites_run_enemy_glide(g_player_x, g_player_y, turn_snap_ex, turn_snap_ey, turn_snap_ea);
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
            ui_combat_log_tick_quiet_turn();
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y);
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
                                    killed = 1u;
                                }
                                hits++;
                            }
                            break;
                        }
                    }
                }
                /* Mace stun: if mace equipped and the struck enemy survived, chance to stun it.
                   (Axe cleave above never targets ei, so enemy_alive[ei] alone reflects whether
                   the primary attack killed it — the shared `killed` flag may have been set by
                   a cleave kill on a different enemy.) */
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
                wait_vbl_done();
                if (killed) draw_cell(nx, ny); // corpse BG only; non-kill leaves terrain unchanged (enemy is sprite)
                draw_gameplay_overlays_profiled(g_player_x, g_player_y);
            }

            {
                uint8_t k;
                gameplay_allies_turn_and_glide(g_player_x, g_player_y);
                for (k = 0; k < num_enemies; k++) {
                    turn_snap_ex[k] = enemy_x[k];
                    turn_snap_ey[k] = enemy_y[k];
                    turn_snap_ea[k] = enemy_alive[k];
                }
                move_enemies(g_player_x, g_player_y);
                entity_sprites_run_enemy_glide(g_player_x, g_player_y, turn_snap_ex, turn_snap_ey, turn_snap_ea);
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
            } else if (t == TILE_PIT && !(boss_alive)) {
                if (floor_num >= MAX_FLOORS) {
                    pending_transition = TRANS_TO_GAME_OVER;
                    next_state = STATE_TRANSITION;
                    wait_vbl_done();
                    return;
                }
                if (player_hp < player_hp_max) player_hp++;
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                pending_transition = TRANS_FLOOR_PIT;
                next_state         = STATE_TRANSITION;
                g_prev_j           = j;
                wait_vbl_done();
                return;
            } else if (nx == player_spawn_x && ny == player_spawn_y
                       && floor_num > 0u
                       && !((floor_biome == BIOME_BOSS || floor_biome == BIOME_MINIBOSS) && boss_alive)) {
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                pending_transition = TRANS_FLOOR_UP;
                next_state         = STATE_TRANSITION;
                g_prev_j           = j;
                wait_vbl_done();
                return;
            } else if (floor_biome == BIOME_OVERWORLD && overworld_trigger_at(nx, ny) == OW_FEAT_ENTRANCE) {
                // Part D: stepping onto an overworld cave-mouth enters the dungeon — reuses the hub
                // ladder's descent path (floor 0 -> 1). Returning via floor-1 stairs lands at the hub
                // pit as usual. Towns/waypoints/boss-door/encounters route here later via their own dest.
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
                if (floor_biome == BIOME_OVERWORLD) { // signpost on the hub → print its label to the chat box
                    uint8_t sa = overworld_signpost_aux_at(nx, ny);
                    if (sa != 255u) overworld_signpost_read(sa);
                }
                consumed_turn = 1u;
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                g_player_x = nx;
                g_player_y = ny;
                lighting_reveal_radius(g_player_x, g_player_y, player_light_radius());
                {
                    uint8_t gi = ground_item_index_at(g_player_x, g_player_y);
                    if (gi != 255u) pending_pickup_slot = gi; // queue modal — fires after enemy turn settles below
                }
                if (player_hp < player_hp_max) player_hp++;
                {
                    uint8_t ally_snap_x[MAX_ALLIES], ally_snap_y[MAX_ALLIES], ally_snap_a[MAX_ALLIES];
                    uint8_t ally_fk;
                    uint8_t k;
                    // Enemy AI runs before scroll so glide offsets animate concurrently with the camera pan
                    for (k = 0; k < num_enemies; k++) {
                        turn_snap_ex[k] = enemy_x[k];
                        turn_snap_ey[k] = enemy_y[k];
                        turn_snap_ea[k] = enemy_alive[k];
                    }
                    move_enemies(g_player_x, g_player_y);
                    entity_sprites_enemy_glide_begin(turn_snap_ex, turn_snap_ey, turn_snap_ea);
                    // Fox AI; glide offsets loaded so both fox and enemies slide during the camera pan
                    ally_fk = ally_walk_tick_and_snap(g_player_x, g_player_y, ally_snap_x, ally_snap_y, ally_snap_a);
                    entity_sprites_ally_glide_begin(ally_snap_x, ally_snap_y, ally_snap_a);
                    {
                        uint8_t target_cx = (g_player_x > GRID_W / 2) ? (uint8_t)(g_player_x - GRID_W / 2) : 0;
                        uint8_t target_cy = (g_player_y > GRID_H / 2) ? (uint8_t)(g_player_y - GRID_H / 2) : 0;
                        if (target_cx > (uint8_t)(active_map_w - GRID_W)) target_cx = (uint8_t)(active_map_w - GRID_W);
                        if (target_cy > (uint8_t)(active_map_h - GRID_H)) target_cy = (uint8_t)(active_map_h - GRID_H);
                        camera_scroll_to(target_cx, target_cy, opx, opy, g_player_x, g_player_y);
                    }
                    if (ally_fk) draw_corpse_cells();
                    // Finish any glide not yet resolved during scroll (blink teleports, deaths, etc.)
                    entity_sprites_run_enemy_glide_finish(turn_snap_ea);
                }
                result = resolve_enemy_hits_and_animate(g_player_x, g_player_y);
                tick_turn_cooldowns();
                consumed_turn = 0u; // ticked here; skip shared block below
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
        if (consumed_turn) {
            tick_turn_cooldowns();
            ui_combat_log_tick_quiet_turn();
            wait_vbl_done();
            draw_gameplay_overlays_profiled(g_player_x, g_player_y);
        }
    }

    if (enemy_anim_update()) {
        wait_vbl_done();
        draw_enemy_cells(g_player_x, g_player_y);
    }
    if (pending_pickup_slot != 255u) next_state = STATE_PICKUP; // turn fully resolved — open modal next frame
    g_prev_j = j;
    wait_vbl_done();
    if (floor_biome == BIOME_OVERWORLD) water_anim_tick(); // fresh in VBlank: one 16-byte VRAM write drifts all sea
    else if (floor_biome == BIOME_BOSS2) sphinx_anim_tick(); // fresh in VBlank: re-upload sphinx body/wing frames
}
