#ifndef RENDER_H
#define RENDER_H

#include "defs.h"

void load_palettes(void);   // upload 8 CGB background palette slots (wall slot from table[0])
void apply_wall_palette(void); // refresh PAL_WALL_BG from wall_palette_table[wall_palette_index]
void draw_screen(uint8_t px, uint8_t py); // full BG redraw + scroll registers
void draw_cell(uint8_t mx, uint8_t my, uint8_t px, uint8_t py); // single map cell if visible
void draw_col_strip(uint8_t mx, uint8_t px, uint8_t py); // one world column for horizontal scroll
void draw_row_strip(uint8_t my, uint8_t px, uint8_t py); // one world row for vertical scroll
void draw_enemy_cells(uint8_t px, uint8_t py); // animation-only partial update
void draw_ui_rows(void); // HUD + bottom UI after camera moves (delegates to ui.c)
void screen_shake(void); // temporary SCX/SCY offset wobble

#endif // RENDER_H
