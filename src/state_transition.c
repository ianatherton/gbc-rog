#pragma bank 1

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "map.h"
#include "music.h"
#include "render.h"

BANKREF_EXTERN(load_palettes)
#include <gb/gb.h>
#include <gbdk/platform.h>

BANKREF(state_transition_enter)

void state_transition_enter(void) BANKED {
    TransitionKind k = pending_transition;
    pending_transition = TRANS_NONE;

    switch (k) {
    case TRANS_FLOOR_PIT:
        BANK_DBG("TR_floor");
        music_play_game();
        wait_vbl_done();
        lcd_clear_display();
        load_palettes(); // BGP after floor wipe — same pitfall as gameplay enter
        level_init_display(1);
        level_generate_and_spawn(&g_player_x, &g_player_y);
        gameplay_soft_reenter = 1u;
        current_state = STATE_NONE; // bounce so next frame next!=current; gameplay enter always runs
        next_state    = STATE_GAMEPLAY;
        break;
    case TRANS_TO_GAME_OVER:
        BANK_DBG("TR_death");
        next_state = STATE_GAME_OVER;
        break;
    default:
        next_state = STATE_GAMEPLAY;
        break;
    }
}
