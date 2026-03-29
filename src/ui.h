#ifndef UI_H
#define UI_H

#include "defs.h"

uint16_t title_screen(uint16_t entropy_hint); // returns run seed from START or word picker
void     game_over_screen(void);              // blocks until START
void     ui_draw_top_hud(void);               // L: hearts HP% XP FLOOR (window row 0)
void     ui_draw_bottom_rows(void);           // seed mnemonic lines + palette fill
void     ui_draw_seed_words(uint16_t seed, uint8_t hvx, uint8_t b1vy, uint8_t b2vy); // bottom UI text from seed
void     window_ui_show(void);                // WX/WY + enable window (gameplay)
void     window_ui_hide(void);              // hide window (title / game over)
void     ui_combat_log_clear(void);           // empty log (new floor / run)
void     ui_combat_log_push(const char *s);  // ring buffer; bottom rows show last lines in dungeon

#endif // UI_H

