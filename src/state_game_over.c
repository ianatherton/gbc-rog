#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "render.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/platform.h>
#include <stdint.h>
#include <stdio.h>

BANKREF_EXTERN(load_palettes)

BANKREF(state_game_over_enter)
void state_game_over_enter(void) BANKED {
    uint8_t d, n, p;
    uint8_t prev_j = joypad();
    BANK_DBG("GO_enter");
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    load_palettes();
    gotoxy(5, 6);
    printf("GAME OVER");
    run_seed_to_triple(run_seed, &d, &n, &p);
    ui_game_over_put_seed_words(d, n, p);
    gotoxy(4, 13);
    printf("START=again");
    while (1) {
        uint8_t j = joypad();
        if ((j & J_START) && !(prev_j & J_START)) break;
        prev_j = j;
        wait_vbl_done();
    }
    next_state = STATE_TITLE;
}
