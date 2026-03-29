#include "lcd.h"
#include "defs.h" // camera_px, camera_py — pulls gb.h for fill_* / move_sprite / LCDC_REG
#include "ui.h"   // ui_loading_vblank — no cycle: ui.h does not include lcd.h

uint8_t  lcd_gameplay_active   = 0u;
volatile int8_t lcd_shake_x    = 0;
volatile int8_t lcd_shake_y    = 0;

static void lcd_vbl_handler(void) { // VBL: HUD band setup for lines 0–7
    SCX_REG = 0u;
    SCY_REG = 0u;
    LYC_REG = 8u;
    if (lcd_gameplay_active) SHOW_WIN; // window row 0 = HUD overlay on BKG
    ui_loading_vblank(); // cheap OAM bob; no-op unless ui_loading_screen_begin is active
}

static void lcd_stat_handler(void) { // fires at LYC (line 8 or UI_WINDOW_Y_START)
    if (LY_REG < 16u) { // line-8 event: switch from HUD band to dungeon viewport
        if (lcd_gameplay_active) {
            int16_t tx = (int16_t)(uint8_t)(camera_px & 0xFFu) + (int16_t)lcd_shake_x;
            int16_t ty = (int16_t)(uint8_t)(camera_py & 0xFFu) + (int16_t)lcd_shake_y;
            SCX_REG = (uint8_t)tx;
            SCY_REG = (uint8_t)ty;
            HIDE_WIN; // window off for dungeon area
            LYC_REG = UI_WINDOW_Y_START; // chain to bottom-UI scanline
        } else {
            SCX_REG = 0u;
            SCY_REG = 0u;
        }
    } else { // UI_WINDOW_Y_START event: re-enable window for bottom seed panel
        SHOW_WIN; // window internal line counter resumes at row 1 (after 8 HUD lines)
    }
}

void lcd_init_raster(void) {
    CRITICAL {
        STAT_REG = STATF_LYC;
        LYC_REG  = 8u;
        add_VBL(lcd_vbl_handler);
        add_LCD(lcd_stat_handler);
    }
}

void lcd_clear_display(void) { // caller must wait_vbl_done() first so LCDC is not toggled mid-scanline
    uint8_t i;
    LCDC_REG &= ~LCDCF_ON; // VRAM free of LCD timing; ~2.4k tile/attr writes + OAM well under one VBlank at double speed
    fill_bkg_rect(0, 0, 32, 32, 0);
    fill_win_rect(0, 0, 32, 32, 0);
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(0, 0, 32, 32, 0);
    fill_win_rect(0, 0, 32, 32, 0);
    VBK_REG = VBK_TILES;
    for (i = 0; i < 40; i++) move_sprite(i, 0, 0);
    LCDC_REG |= LCDCF_ON;
}
