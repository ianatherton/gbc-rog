#include "lcd.h"
#include "defs.h" // camera_px, camera_py — pulls gb.h for fill_* / move_sprite / LCDC_REG
#include "globals.h" // inv_desc_scx
#include "ui.h"   // ui_loading_vblank — no cycle: ui.h does not include lcd.h
#include "entity_sprites.h" // entity_sprites_vbl_tick

// Inventory desc-row sub-pixel scroll: SCX is overridden only for these scanlines.
// Fire one scanline EARLY so the ISR's SCX write lands after mode 3 of the
// early scanline (already committed), taking clean effect for the target row.
#define INV_DESC_SCAN_START 119u // one before tile row 15 (pixel row 120)
#define INV_DESC_SCAN_END   127u // one before tile row 16 (pixel row 128)

BANKREF_EXTERN(entity_sprites_vbl_tick)

uint8_t  lcd_gameplay_active;   // BSS 0 — title/state screens drive transitions to gameplay
volatile int8_t lcd_shake_x;
volatile int8_t lcd_shake_y;

static uint8_t lcd_hp_panic_flash_ttl; // VBlanks left: BKG pal 0 red tint then restore
static const palette_color_t lcd_pal_hp_panic_flash[] = {
    RGB(22, 0, 0), RGB(28, 2, 2), RGB(31, 8, 6), RGB(31, 16, 12),
};
// Restored ramp = whatever the gameplay palette loaders last pushed into slot 0 (dungeon default /
// hub green field / town field) — a fixed ramp here painted the dungeon black over the hub's green.
static palette_color_t lcd_pal_bkg0_restore[] = {
    RGB(0, 0, 0), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
};

void lcd_note_bkg0(const uint16_t *pal4) { // caller's pointer may be banked ROM — copy while it's mapped
    lcd_pal_bkg0_restore[0] = pal4[0]; lcd_pal_bkg0_restore[1] = pal4[1];
    lcd_pal_bkg0_restore[2] = pal4[2]; lcd_pal_bkg0_restore[3] = pal4[3];
}

void lcd_hp_panic_flash_trigger(void) {
    if (lcd_gameplay_active) lcd_hp_panic_flash_ttl = 3u; // ~3 frames @60Hz — one visible red pulse on pal-0 tiles
}

static void lcd_vbl_handler(void) { // VBL: gameplay = dungeon scroll whole frame until LYC; title = fixed scroll
    if (lcd_gameplay_active) {
        int16_t tx = (int16_t)(uint8_t)(camera_px & 0xFFu) + (int16_t)lcd_shake_x;
        int16_t ty = (int16_t)(uint8_t)(camera_py & 0xFFu) + (int16_t)lcd_shake_y;
        SCX_REG = (uint8_t)tx;
        SCY_REG = (uint8_t)ty;
        LYC_REG = UI_WINDOW_Y_START;
        SHOW_WIN; // keep LCDC.5 on — toggling mid-frame unreliable on CGB (pan docs)
        WY_REG = UI_WINDOW_WY_OFFSCREEN; // hide WIN until bottom band (belt + 3 text rows + HUD)
        if (lcd_hp_panic_flash_ttl > 0u) {
            set_bkg_palette(0u, 1u, lcd_pal_hp_panic_flash);
            lcd_hp_panic_flash_ttl--;
            if (lcd_hp_panic_flash_ttl == 0u) set_bkg_palette(0u, 1u, lcd_pal_bkg0_restore);
        }
    } else {
        lcd_hp_panic_flash_ttl = 0u;
        SCX_REG = 0u;
        SCY_REG = 0u;
        LYC_REG = 8u;
    }
    ui_loading_vblank(); // cheap OAM bob; no-op unless ui_loading_screen_begin is active
    entity_sprites_vbl_tick(); // stable 60Hz timers for sprite-only effects
}

static void lcd_stat_handler(void) { // gameplay: LYC = UI_WINDOW_Y_START → show bottom window; title: line-8 chain
    if (lcd_gameplay_active) {
        WY_REG = UI_WINDOW_Y_START;
        return;
    }
    if (LY_REG < 16u) {                          // LY == 8 (VBL set LYC=8)
        SCX_REG = 0u;
        SCY_REG = 0u;
        LYC_REG = UI_WINDOW_Y_START;
    } else if (LY_REG < INV_DESC_SCAN_START) {   // LY == UI_WINDOW_Y_START (104)
        WY_REG = UI_WINDOW_Y_START;
        if (inv_desc_scx) LYC_REG = INV_DESC_SCAN_START; // extend chain for desc row
    } else if (LY_REG < INV_DESC_SCAN_END) {     // LY == 120: start of desc tile row
        SCX_REG = inv_desc_scx;
        LYC_REG = INV_DESC_SCAN_END;
    } else {                                      // LY == 128: end of desc tile row
        SCX_REG = 0u;
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

void lcd_suspend(void) {
    CRITICAL {
        remove_VBL(lcd_vbl_handler);
        remove_LCD(lcd_stat_handler);
        STAT_REG = 0;
    }
}

void lcd_resume(void) {
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
