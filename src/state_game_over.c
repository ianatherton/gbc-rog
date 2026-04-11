#pragma bank 1

#include "debug_bank.h"
#include "game_state.h"
#include "ui.h"
#include <gb/gb.h>
#include <stdint.h>
#include <gbdk/platform.h>

BANKREF(state_game_over_enter)
void state_game_over_enter(void) BANKED {
    BANK_DBG("GO_enter");
    game_over_screen();
    next_state = STATE_TITLE;
}
