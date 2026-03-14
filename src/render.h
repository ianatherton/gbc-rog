#ifndef RENDER_H
#define RENDER_H

#include "defs.h"

void load_palettes(void);
void draw_screen(uint8_t px, uint8_t py);
void draw_cell(uint8_t mx, uint8_t my, uint8_t px, uint8_t py);
void draw_col_strip(uint8_t mx, uint8_t px, uint8_t py);
void draw_row_strip(uint8_t my, uint8_t px, uint8_t py);
void draw_enemy_cells(uint8_t px, uint8_t py);
void draw_bottom_ui(void);
void screen_shake(void);

#endif // RENDER_H
