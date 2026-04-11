#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

static uint8_t stats_prev_j;

BANKREF(state_stats_enter)
void state_stats_enter(void) BANKED {
    BANK_DBG("ST_enter");
    stats_prev_j = 0;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("ST_draw");
    gotoxy(1, 2); printf("STATS");
    gotoxy(1, 4); printf("HP %u/%u", (unsigned)player_hp, (unsigned)player_hp_max);
    gotoxy(1, 5); printf("LV %u DMG %u", (unsigned)player_level, (unsigned)player_damage);
    gotoxy(1, 8); printf("SEL inventory");
    gotoxy(1, 9); printf("A abilities");
    gotoxy(1, 10); printf("B resume");
}

BANKREF(state_stats_tick)
void state_stats_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~stats_prev_j);
    if (e & J_B) next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_INVENTORY;
    if (e & J_A) next_state = STATE_ABILITY;
    stats_prev_j = j;
    wait_vbl_done();
}
