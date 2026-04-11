#pragma bank 1

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <stdint.h>
#include <stdio.h>
#include <gbdk/platform.h>

BANKREF(state_char_create_enter)
void state_char_create_enter(void) BANKED {
    uint8_t prev_j = 0, sel = 0;
    BANK_DBG("CC_enter");
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    for (;;) {
        gotoxy(3, 4); printf("CHOOSE CLASS");
        gotoxy(2, 7); printf(sel == 0 ? ">" : " ");
        gotoxy(4, 7); printf("KNIGHT");
        gotoxy(2, 9); printf(sel == 1 ? ">" : " ");
        gotoxy(4, 9); printf("ROGUE");
        gotoxy(2, 11); printf(sel == 2 ? ">" : " ");
        gotoxy(4, 11); printf("MAGE");
        gotoxy(2, 14); printf("A=confirm");
        {
            uint8_t j = joypad();
            uint8_t e = (uint8_t)(j & (uint8_t)~prev_j);
            if (e & J_UP)   sel = (uint8_t)((sel + 2u) % 3u);
            if (e & J_DOWN) sel = (uint8_t)((sel + 1u) % 3u);
            if (e & J_A) {
                player_class = sel;
                break;
            }
            prev_j = j;
        }
        wait_vbl_done();
    }
    g_prev_j = 0;
    next_state = STATE_GAMEPLAY;
}
