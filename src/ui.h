#ifndef UI_H
#define UI_H

#include "defs.h"

uint16_t title_screen(uint16_t entropy_hint); // returns run seed from START or word picker
void     game_over_screen(void);              // blocks until START
void     ui_draw_top_hud(void);               // floor + life bar into ring row above dungeon band
void     ui_draw_bottom_rows(void);           // seed mnemonic lines + palette fill
void     ui_draw_seed_words(uint16_t seed, uint8_t hvx, uint8_t b1vy, uint8_t b2vy); // bottom UI text from seed

#endif // UI_H

