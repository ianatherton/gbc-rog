#ifndef UI_H
#define UI_H

#include "defs.h"

uint16_t title_screen(uint16_t entropy_hint);
void     game_over_screen(void);
/** Draw the three seed words for the given seed at ring positions (hvx, b1vy) and (hvx, b2vy). */
void     ui_draw_seed_words(uint16_t seed, uint8_t hvx, uint8_t b1vy, uint8_t b2vy);

#endif // UI_H

