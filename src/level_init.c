#pragma bank 10 // was bank 2 — swapped with state_gameplay so gameplay kernel stays one ROM bank

#include "ability_dispatch.h"
#include "defs.h"
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
        floor_num++;
        entered_from_below = 0u; // descended via pit — land at stairs-up
    } else if (from_pit == 2u) {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin(1);
        floor_num--;
        entered_from_below = 1u; // ascended via stairs — land at pit
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
        deepest_floor = 0u;       // hub is the shallowest; floor 1+ are "deeper"
        entered_from_below = 0u;
        player_hp_max = PLAYER_HP_BASE_MAX;
        player_level = 1;
        player_damage = 1;
        player_xp = 0;
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
        if (player_class == 2u) inventory_add(ITEM_KIND_SCROLL_PORT6, 0); // WITCH starts with the Port: Flr6 scroll
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
    // Direction-independent revisit: any floor at or above the deepest reached
    // has been visited before, whether we arrived by descending or ascending.
    if (from_pit == 0u) {
        level_is_revisit = 0u; // fresh run, floor 1
    } else if (floor_num > deepest_floor) {
        level_is_revisit = 0u; // first time this deep
        deepest_floor = floor_num;
    } else {
        level_is_revisit = 1u; // been here before — restore permanence
    }
    ui_combat_log_clear();
}
