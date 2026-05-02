#pragma bank 2

#include "defs.h"
#include "globals.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include <gbdk/platform.h>

BANKREF(level_init_display)

void level_init_display(uint8_t from_pit) BANKED {
    if (from_pit) {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        wait_vbl_done();
        lcd_clear_display();
        ui_loading_screen_begin();
        floor_num++;
    } else {
        lcd_gameplay_active = 0u;
        window_ui_hide();
        floor_num = 1;
        player_hp_max = PLAYER_HP_BASE_MAX;
        player_level = 1;
        player_damage = 1;
        player_xp = 0;
        player_hp = player_hp_max;
        inventory_clear_all(); // fresh run wipes any items from a previous attempt
        wait_vbl_done();
        ui_loading_screen_begin();
    }
    ui_combat_log_clear();
}
