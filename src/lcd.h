#ifndef LCD_H
#define LCD_H

#include <stdint.h>

void lcd_init_raster(void); // VBL + LYC chain: HUD (WY=0) → dungeon (WY off-screen) → bottom panel (WY=96)
void lcd_clear_display(void); // call only after wait_vbl_done(): LCD off → wipe maps+OAM → on

extern uint8_t  lcd_gameplay_active; // 0 title/gameover: full-frame scroll 0; 1 dungeon: line-8 camera
extern volatile int8_t lcd_shake_x;  // added to game SCX in line-8 ISR only
extern volatile int8_t lcd_shake_y;  // added to game SCY in line-8 ISR only

#endif
