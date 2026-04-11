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

static uint8_t ab_prev_j;

BANKREF(state_ability_enter)
void state_ability_enter(void) BANKED {
    BANK_DBG("AB_enter");
    ab_prev_j = 0;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    gotoxy(2, 6); printf("ABILITIES");
    gotoxy(2, 9); printf("(none yet)");
    gotoxy(1, 12); printf("B back");
}

BANKREF(state_ability_tick)
void state_ability_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~ab_prev_j);
    if (e & J_B) next_state = STATE_GAMEPLAY;
    ab_prev_j = j;
    wait_vbl_done();
}
