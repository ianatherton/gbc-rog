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
    stats_prev_j = joypad();
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("ST_draw");
    {
        uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
        uint8_t x;
        gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
        set_bkg_tiles(5u, 0u, 1u, 1u, &v);
        set_bkg_attribute_xy(5u, 0u, PAL_XP_UI);
        for (x = 1u;  x <= 4u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI); // ITEM
        for (x = 11u; x <= 15u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI); // SPELL
        for (x = 17u; x <= 19u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI); // MAP
        VBK_REG = VBK_TILES;
    }
    gotoxy(1, 2); printf("STATS");
    gotoxy(1, 4); printf("HP %u/%u", (unsigned)player_hp, (unsigned)player_hp_max);
    gotoxy(1, 5); printf("LV %u DMG %u", (unsigned)player_level, (unsigned)player_damage);
    gotoxy(1, 10); printf("START resume");
}

BANKREF(state_stats_tick)
void state_stats_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~stats_prev_j);
    if (e & J_START)  next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_ABILITY;
    stats_prev_j = j;
    wait_vbl_done();
}
