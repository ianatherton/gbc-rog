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
#include "dungeon.h"
#include "ally.h"
#include "items.h"
#include "story_ui.h"
#include "auto_explore.h"
#include "gameplay_cold.h"
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

// Zone-confirm latch (CONFIRM_* in defs.h): walking onto a transition tile is a real step (full
// turn — enemies act, camera scrolls, lighting updates, same as any other move); once landed, this
// arms + prints a prompt instead of zoning immediately. The next A rising-edge fires the stored
// transition without moving the player further. Any move to a different target cell cancels
// silently.
static uint8_t confirm_kind, confirm_aux, confirm_tx, confirm_ty;

static void confirm_arm(uint8_t kind, uint8_t aux, uint8_t tx, uint8_t ty) {
    if (confirm_kind == kind && confirm_tx == tx && confirm_ty == ty) return; // already armed — don't respam the prompt
    confirm_kind = kind; confirm_aux = aux; confirm_tx = tx; confirm_ty = ty;
    ui_confirm_prompt_push(kind, aux);
    wait_vbl_done();
    draw_gameplay_overlays_profiled(g_player_x, g_player_y); // render the prompt line now — arming consumes no turn, so nothing else redraws the HUD
}

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

static uint8_t select_hold_ticks;
// belt_select_advance_skip_empty / push_selected_belt_description evicted to gameplay_cold.c
// (bank 30) — SELECT-edge-only, moved for bank-2 space relief.

BANKREF(state_gameplay_enter)
void state_gameplay_enter(void) BANKED {
    confirm_kind = CONFIRM_NONE;
    auto_explore_active = 0u;
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
        g_prev_j = j; // buttons still held from the modal's exit press are stale — require release before they edge again (B belt-use / A auto-explore)
        window_ui_show();
        apply_field_palette(); // restore slot 0 field color (a menu may have blanked it to black)
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }

    if (auto_explore_active) j = auto_explore_step(j); // synthesized dpad bit (or 0) — drives the normal walk/bump path below

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

    if (confirm_kind != CONFIRM_NONE && confirm_kind != CONFIRM_SEALED
            && (j & J_A) && !(g_prev_j & J_A)) { // fire the armed zone transition
        uint8_t ck = confirm_kind;
        confirm_kind = CONFIRM_NONE;
        wait_vbl_done();
        draw_cell(g_player_x, g_player_y);
        if (ck == CONFIRM_ENTRANCE) {
            pending_port_floor = DUNGEON_GUARD_FLOOR(confirm_aux);
            pending_transition = TRANS_FLOOR_PORT;
        } else if (ck == CONFIRM_TOWN) {
            pending_port_floor = (uint8_t)(TOWN_FLOOR_BASE + confirm_aux);
            pending_transition = TRANS_FLOOR_PORT;
        } else if (ck == CONFIRM_UP) {
            pending_transition = TRANS_FLOOR_UP;
        } else if (ck == CONFIRM_BOSS_EXIT) {
            pending_transition = TRANS_DUNGEON_EXIT;
        } else { // CONFIRM_PIT
            pending_transition = TRANS_FLOOR_PIT;
        }
        next_state = STATE_TRANSITION;
        g_prev_j   = j;
        wait_vbl_done();
        return;
    }

    if (!auto_explore_active && (j & J_A) && !(g_prev_j & J_A))
        auto_explore_try_start(); // no confirm armed (consumed above) — A starts auto-explore; falls through, J_A moves nothing

    if (j & J_LEFT)  { nx = g_player_x > 0       ? (uint8_t)(g_player_x-1) : g_player_x; entity_sprites_set_player_facing(-1); }
    if (j & J_RIGHT) { nx = g_player_x < active_map_w-1 ? (uint8_t)(g_player_x+1) : g_player_x; entity_sprites_set_player_facing(1); }
    if (j & J_UP)    ny = g_player_y > 0         ? (uint8_t)(g_player_y-1) : g_player_y;
    if (j & J_DOWN)  ny = g_player_y < active_map_h-1   ? (uint8_t)(g_player_y+1) : g_player_y;

#if GBC_ROG_DEBUG
    if ((j & J_B) && (j & J_A) && !(g_prev_j & J_A)) { // B+A: plain A now starts auto-explore
        wall_palette_index = (uint8_t)((wall_palette_index + 1u) % NUM_WALL_PALETTES);
        wait_vbl_done();
        draw_screen(g_player_x, g_player_y);
    }
#endif

    if (nx != g_player_x || ny != g_player_y) {
        uint8_t ei = enemy_at(nx, ny);
        uint8_t result = 0;
        uint8_t consumed_turn = 0u;

        if (confirm_kind != CONFIRM_NONE && (nx != confirm_tx || ny != confirm_ty))
            confirm_kind = CONFIRM_NONE; // walked/attacked elsewhere — silent cancel

        if (ei != ENEMY_DEAD) {
            consumed_turn = 1u;
            {
                uint8_t killed = combat_player_attacks(ei, g_player_x, g_player_y, nx, ny);
                if (combat_player_melee_extras(ei)) killed = 1u; // axe cleave + mace stun (bank 19)
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
            if (t == TILE_WALL && floor_kind == FLOORKIND_TOWN && town_barrel_try_break(nx, ny)) {
                // 1-hit break: loot roll + poof already ran; no player movement. Overlay/HUD refresh
                // comes from the shared consumed_turn tail below — no separate draw call needed here.
                consumed_turn = 1u;
                wait_vbl_done();
                draw_cell(nx, ny); // barrel gone — cell now shows plain grass
            } else if (t == TILE_WALL || (floor_kind == FLOORKIND_TOWN && town_npc_blocks(nx, ny))) { // a villager's tile blocks like a wall
            } else {
                // Figure out whether (nx,ny) is a transition tile before walking onto it — the
                // walk always happens (full turn, enemies act); the confirm prompt arms only
                // after landing, once g_player_x/y actually sit on the tile.
                uint8_t want_kind = CONFIRM_NONE, want_aux = 0u;
                if (t == TILE_PIT && !(boss_alive)) {
                    // Boss floor renders its pit as the exit portal once the boss is dead (boss_alive
                    // gates it above).
                    want_kind = (floor_kind == FLOORKIND_BOSS) ? CONFIRM_BOSS_EXIT : CONFIRM_PIT;
                } else if (((nx == player_spawn_x && ny == player_spawn_y)
                            || (floor_kind == FLOORKIND_TOWN && town_exit_at(nx, ny))) // any of the 4 town road mouths
                           && floor_num > 0u
                           && !boss_alive) { // boss_alive is only ever set on boss/miniboss floors
                    want_kind = CONFIRM_UP;
                } else if (floor_biome == BIOME_OVERWORLD && overworld_trigger_at(nx, ny) == OW_FEAT_ENTRANCE) {
                    // Overworld cave-mouth: entry into that dungeon's guardroom via the port path.
                    // Each of the 9 entrances is its own dungeon (dungeon.h floor scheme).
                    uint8_t did = overworld_entrance_id_at(nx, ny);
                    if (did < DUNGEON_COUNT && (dungeon_complete_mask & (uint16_t)((uint16_t)1u << did))) {
                        want_kind = CONFIRM_SEALED; // message only (deduped); A does nothing
                    } else if (did < DUNGEON_COUNT) {
                        want_kind = CONFIRM_ENTRANCE; want_aux = did;
                    }
                } else if (floor_biome == BIOME_OVERWORLD && overworld_trigger_at(nx, ny) == OW_FEAT_TOWN) {
                    uint8_t tid = overworld_town_id_at(nx, ny);
                    if (tid < TOWN_COUNT) { want_kind = CONFIRM_TOWN; want_aux = tid; } // A → TOWN_FLOOR_BASE+tid via the port path
                }

                uint8_t opx = g_player_x, opy = g_player_y;
                if (floor_biome == BIOME_OVERWORLD || floor_biome == BIOME_TOWN)
                    overworld_step_feature(nx, ny); // signpost label / town fountain heal
                consumed_turn = 1u;
                wait_vbl_done();
                draw_cell(g_player_x, g_player_y);
                g_player_x = nx;
                g_player_y = ny;
                lighting_reveal_radius(g_player_x, g_player_y, player_light_radius());
                uint8_t roof_changed = 0u; // town: stepping through a building door lifts/re-covers its roof
                if (floor_kind == FLOORKIND_TOWN)
                    roof_changed = town_roof_update(g_player_x, g_player_y);
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
                    if (floor_kind == FLOORKIND_TOWN) town_npcs_tick(g_player_x, g_player_y); // lazy villager wander; no glide
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
                if (lighting_dirty_overflow() || roof_changed)
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
                if (roof_changed) draw_screen(g_player_x, g_player_y);
                else              draw_gameplay_overlays_profiled(g_player_x, g_player_y);
#endif

                if (result == 2) {
                    pending_transition = TRANS_TO_GAME_OVER;
                    next_state         = STATE_TRANSITION;
                    g_prev_j           = j;
                    wait_vbl_done();
                    return;
                }
                if (want_kind != CONFIRM_NONE) confirm_arm(want_kind, want_aux, g_player_x, g_player_y);
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
    if (pending_pickup_slot != 255u) { // turn fully resolved — auto-take while exploring, else open modal next frame
        if (auto_explore_active) auto_explore_take_pending();
        else next_state = STATE_PICKUP;
    }
    g_prev_j = j;
    wait_vbl_done();
    if (floor_biome == BIOME_OVERWORLD) water_anim_tick(); // fresh in VBlank: one 16-byte VRAM write drifts all sea
    else if (floor_kind == FLOORKIND_BOSS && floor_boss_type == ENEMY_SPHINX) sphinx_anim_tick(); // fresh in VBlank: re-upload sphinx body/wing frames
}
