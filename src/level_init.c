#pragma bank 10 // was bank 2 — swapped with state_gameplay so gameplay kernel stays one ROM bank

#include "ability_dispatch.h"
#include "defs.h"
#include "dungeon.h"
#include "equipment.h" // items_equip_apply — witch starts with her hat worn
#include "globals.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include <gbdk/platform.h>

BANKREF(level_init_display)

void level_init_display(uint8_t from_pit) BANKED {
    if (from_pit == 1u) {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin(0);
        if (floor_num >= GUARD_FLOOR_BASE) { // guardroom pit drops into the dungeon's first counted floor
            uint8_t d = (uint8_t)(floor_num - GUARD_FLOOR_BASE);
            floor_num = DUNGEON_BASE_FLOOR(d);
        } else {
            floor_num++;
        }
        entered_from_below = 0u; // descended via pit — land at stairs-up
    } else if (from_pit == 2u) {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin(1);
        if (floor_num >= TOWN_FLOOR_BASE) { // town door exits to the hub, beside the town (0x80 flags a town id)
            hub_landing_dungeon = (uint8_t)(0x80u | (uint8_t)(floor_num - TOWN_FLOOR_BASE));
            floor_num = 0u;
            entered_from_below = 0u;
        } else if (floor_num >= GUARD_FLOOR_BASE) { // guardroom stairs exit to the hub, beside the entrance used
            hub_landing_dungeon = (uint8_t)(floor_num - GUARD_FLOOR_BASE);
            floor_num = 0u;
            entered_from_below = 0u; // hub has no pit — spawn at the (overridden) spawn point
        } else {
            uint8_t lo = FLOOR_LOCAL(floor_num);
            if (lo == 1u) { // first counted floor climbs back to its guardroom
                uint8_t d = FLOOR_DUNGEON_ID(floor_num);
                floor_num = DUNGEON_GUARD_FLOOR(d);
            } else {
                floor_num--;
            }
            entered_from_below = 1u; // ascended via stairs — land at pit
        }
    } else if (from_pit == 4u) { // boss-floor exit portal — dungeon complete, return to hub
        uint8_t d = FLOOR_DUNGEON_ID(floor_num);
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin(1);
        dungeon_complete_mask |= (uint16_t)((uint16_t)1u << d);
        hub_landing_dungeon = d;
        floor_num = 0u;
        entered_from_below = 0u;
    } else if (from_pit == 3u) {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin(0);
        floor_num = pending_port_floor; // Witch Port scroll warps straight to this floor
        entered_from_below = 0u;        // land at the stairs-up like a descent
    } else {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        floor_num = 0;            // new run starts on the overworld hub (floor 0)
        entered_from_below = 0u;
        dungeon_complete_mask = 0u;
        hub_landing_dungeon   = DUNGEON_NONE;
        floor_kind            = FLOORKIND_HUB;
        {
            uint8_t vi;
            for (vi = 0u; vi < 7u; vi++) floor_visited[vi] = 0u;
        }
        player_hp_max = PLAYER_HP_BASE_MAX;
        player_level = 1;
        player_damage = 1;
        player_xp = 0;
        player_stat_points = 0;
        if      (player_class == 1u) player_crit_chance = 15u; // SCOUNDREL
        else if (player_class == 2u) player_crit_chance = 10u; // WITCH
        else if (player_class == 3u) player_crit_chance = 20u; // ZERKER
        else                         player_crit_chance = 10u; // KNIGHT (0)
        if      (player_class == 1u) { player_armor =  0u; player_magdef =  5u; player_dodge = 15u; } // SCOUNDREL — evasive
        else if (player_class == 2u) { player_armor =  0u; player_magdef = 15u; player_dodge =  5u; } // WITCH — warded
        else if (player_class == 3u) { player_armor = 10u; player_magdef =  0u; player_dodge =  5u; } // ZERKER — reckless
        else                          { player_armor = 15u; player_magdef =  5u; player_dodge =  0u; } // KNIGHT — armored
        player_hp = player_hp_max;
        inventory_clear_all(); // fresh run wipes any items from a previous attempt
        {   // trade state — see the GSINIT note in globals.c: these have no initializers
            uint8_t t;
            player_tokens = 0u;
            pending_talk_npc = 255u;
            for (t = 0u; t < TOWN_COUNT; t++) town_shop_sold[t] = 0u;
        }
        if (player_class == 2u) { // WITCH starts with the Port: Boss scroll + her hat (worn)
            uint8_t s;
            inventory_add(ITEM_KIND_SCROLL_PORT6, 0);
            inventory_add(ITEM_KIND_WITCH_HAT, 0);
            for (s = 0u; s < INVENTORY_MAX_SLOTS; s++) { // equip it — same steps as the inventory toggle
                if (inventory_kind[s] == ITEM_KIND_WITCH_HAT) {
                    inventory_equipped[s] = 1u;
                    items_equip_apply(ITEM_KIND_WITCH_HAT, s, 1u);
                    break;
                }
            }
        }
        {
            uint8_t fi;
            for (fi = 0u; fi < MAX_FLOORS; fi++) {
                floor_items_picked[fi] = 0u;
            }
            for (fi = 0u; fi < (uint8_t)(MAX_FLOORS * 3u); fi++) floor_enemy_dead[fi] = 0u;
        }
        ability_dispatch_new_run_init();
        wait_vbl_done();
        ui_loading_screen_begin(0);
    }
    // Direction-independent revisit: each floor tracks its own visited bit —
    // with 9 independent dungeon branches a "deepest floor" watermark is meaningless.
    level_is_revisit = BIT_GET(floor_visited, floor_num);
    BIT_SET(floor_visited, floor_num);
    ui_combat_log_clear();
}
