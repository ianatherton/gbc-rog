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

static uint8_t map_prev_j;

BANKREF(state_map_enter)
void state_map_enter(void) BANKED {
    BANK_DBG("MAP_enter");
    map_prev_j = joypad();
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    {
        uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
        uint8_t x;
        gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
        set_bkg_tiles(16u, 0u, 1u, 1u, &v);
        set_bkg_attribute_xy(16u, 0u, PAL_XP_UI);
        for (x = 1u;  x <= 4u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI);  // ITEM
        for (x = 6u;  x <= 9u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI);  // STAT
        for (x = 11u; x <= 15u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI);  // SPELL
        VBK_REG = VBK_TILES;
    }
    gotoxy(2, 6); printf("MAP");
    gotoxy(1, 12); printf("START resume");
}

BANKREF(state_map_tick)
void state_map_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~map_prev_j);
    if (e & J_START)  next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_INVENTORY;
    map_prev_j = j;
    wait_vbl_done();
}
