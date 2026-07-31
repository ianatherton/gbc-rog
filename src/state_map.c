#pragma bank 3

#include "debug_bank.h"
#include "dungeon.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

static uint8_t map_prev_j;

// Recall is a safe-travel convenience, not an escape hatch: boss and miniboss fights can't be
// skipped, and blocking '?' encounters means we never have to reason about floor 49's entry-time
// persistence wipe (level_init_display case 3) respawning an encounter's enemies and chest.
static uint8_t recall_blocked_here(void) {
    return (uint8_t)(floor_kind == FLOORKIND_MINIBOSS
                     || floor_kind == FLOORKIND_BOSS
                     || floor_kind == FLOORKIND_ENCOUNTER);
}

// Town id to recall into from wherever the player is standing.
// Off the hub this is pure arithmetic: entrances are placed 3 per region in region order
// (place_overworld_features), so dungeon d rings town d/3 — the same grouping DUNGEON_MONSTER_LEVEL
// encodes. On the hub ow_features[] is live (it's only populated on floor 0), so scan it for the
// nearest town door by Chebyshev distance, counting placement ordinal exactly like
// overworld_town_id_at does.
static uint8_t recall_town_id(void) {
    if (floor_num != 0u) {
        uint8_t d = FLOOR_DUNGEON_ID(floor_num);
        if (d == DUNGEON_NONE) return 0u; // town interiors don't recall out; belt-and-braces
        return (uint8_t)(d / 3u);
    }
    {
        uint8_t i, ord = 0u, best = 0u, best_d = 255u;
        for (i = 0u; i < ow_feature_count; i++) {
            uint8_t tx, ty, dx, dy, dist;
            if (ow_features[i].type != OW_FEAT_TOWN) continue;
            tx = (uint8_t)(ow_features[i].x + ow_prefab_defs[OW_FEAT_TOWN].ent_dx);
            ty = (uint8_t)(ow_features[i].y + ow_prefab_defs[OW_FEAT_TOWN].ent_dy);
            dx = (g_player_x > tx) ? (uint8_t)(g_player_x - tx) : (uint8_t)(tx - g_player_x);
            dy = (g_player_y > ty) ? (uint8_t)(g_player_y - ty) : (uint8_t)(ty - g_player_y);
            dist = (dx > dy) ? dx : dy; // Chebyshev — 8-way movement, same metric as the road builder
            if (dist < best_d) { best_d = dist; best = ord; }
            ord++;
        }
        return best;
    }
}

BANKREF(state_map_enter)
void state_map_enter(void) BANKED {
    BANK_DBG("MAP_enter");
    map_prev_j = joypad();
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    {
        uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
        uint8_t x;
        gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
        set_bkg_tiles(16u, 0u, 1u, 1u, &v);
        set_bkg_attribute_xy(16u, 0u, PAL_XP_UI_BG);
        for (x = 1u;  x <= 4u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG);  // ITEM
        for (x = 6u;  x <= 9u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG);  // STAT
        for (x = 11u; x <= 15u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG);  // SPELL
        VBK_REG = VBK_TILES;
        if (player_stat_points) { // unspent level-up points reminder
            gotoxy(10, 0); putchar('+');
            set_bkg_attribute_xy(10u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
        if (player_spell_points) { // unspent spell points; col 16 holds this tab's arrow → use col 5
            gotoxy(5, 0); putchar('+');
            set_bkg_attribute_xy(5u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
    }
    ui_map_put_seed_line(0u, 1u); // full seed name under the tab bar
    gotoxy(2, 6); printf("MAP");
    // Recall prompt lives at the bottom so the minimap can own rows 2..11 later.
    if (recall_blocked_here()) {
        gotoxy(1, 10); printf("NO RECALL HERE");
    } else if (recall_anchor_floor != RECALL_NO_ANCHOR) {
        gotoxy(1, 10); printf("A return to mark");
    } else if (floor_kind != FLOORKIND_TOWN) {
        gotoxy(1, 10); printf("A recall to town");
    }
    gotoxy(1, 12); printf("START resume");
}

BANKREF(state_map_tick)
void state_map_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~map_prev_j);
    if (e & J_START)  next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_INVENTORY;
    if ((e & J_A) && !recall_blocked_here()) {
        // Both legs are the Witch Port scroll's path: arm pending_port_floor + TRANS_FLOOR_PORT and
        // hand off to STATE_TRANSITION, which ports, regenerates and soft-re-enters gameplay itself.
        if (recall_anchor_floor != RECALL_NO_ANCHOR) {         // return leg — map.c consumes the anchor
            pending_port_floor    = recall_anchor_floor;
            recall_return_pending = 1u;
            pending_transition    = TRANS_FLOOR_PORT;
            next_state            = STATE_TRANSITION;
        } else if (floor_kind != FLOORKIND_TOWN) {             // outbound leg — mark, then go to town
            recall_anchor_floor = floor_num;
            recall_anchor_x     = g_player_x;
            recall_anchor_y     = g_player_y;
            pending_port_floor  = (uint8_t)(TOWN_FLOOR_BASE + recall_town_id());
            pending_transition  = TRANS_FLOOR_PORT;
            next_state          = STATE_TRANSITION;
        }
    }
    map_prev_j = j;
    wait_vbl_done();
}
