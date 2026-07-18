#ifndef LCD_H
#define LCD_H

#include <stdint.h>

void lcd_init_raster(void); // VBL + LYC: dungeon full height → bottom window (WY=UI_WINDOW_Y_START)
void lcd_clear_display(void); // call only after wait_vbl_done(): LCD off → wipe maps+OAM → on
void lcd_hp_panic_flash_trigger(void); // one brief red wash on BKG palette 0 (CGB) — call when HP% crosses down through 30%
void lcd_note_bkg0(const uint16_t *pal4); // record the live BKG palette 0 (4 colors) so the panic flash restores the right ramp — call after every gameplay set_bkg_palette(0, ...)
void lcd_suspend(void);
void lcd_resume(void);

extern uint8_t  lcd_gameplay_active; // 0 title/gameover: scroll 0 + legacy LYC chain; 1 dungeon: camera + bottom WIN
extern volatile int8_t lcd_shake_x;  // added to game SCX in line-8 ISR only
extern volatile int8_t lcd_shake_y;  // added to game SCY in line-8 ISR only

#endif
