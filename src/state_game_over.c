#pragma bank 1

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

static void go_put_word5(uint8_t x, uint8_t y, const char *s) { // BKG text like ui put_word5
    uint8_t i;
    for (i = 0; i < 5u; i++) {
        gotoxy((uint8_t)(x + i), y);
        setchar(s[i] ? s[i] : ' ');
    }
}

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
    go_put_word5(0u, 9u, seed_words_desc[d]);
    go_put_word5(6u, 9u, seed_words_noun[n]);
    go_put_word5(0u, 10u, seed_words_place[p]);
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
