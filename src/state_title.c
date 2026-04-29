#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "ui.h"
#include "music.h"
#include <gb/gb.h>
#include <stdint.h>
#include <gbdk/platform.h>

BANKREF(state_title_enter)
void state_title_enter(void) BANKED {
    BANK_DBG("TI_enter");
    g_run_entropy += 1u + (uint16_t)DIV_REG;
    music_play_title();
    {
        uint16_t seed = title_screen(g_run_entropy);
        if (!seed) seed = (uint16_t)(g_run_entropy ^ 0xACE1u);
        if (!seed) seed = 0xACE1u;
        run_seed  = seed;
    }
    floor_num = 0;
    next_state = STATE_CHAR_CREATE;
}
