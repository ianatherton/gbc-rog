#ifndef RENDER_H
#define RENDER_H

#include "defs.h"

void load_palettes(void) BANKED;   // upload 8 CGB background palette slots (wall slot from table[0])
void render_sprite_palette_player_default(void) NONBANKED; // OCP PAL_PLAYER — class ramp from class_palettes (after hurt flash)
void render_sprite_palette_player_hurt(void) NONBANKED;    // OCP PAL_PLAYER — saturated red tint
void apply_wall_palette(void); // PAL_WALL_BG + PAL_PILLAR_BG from wall_palette_index / pillar_palette_index
void draw_screen(uint8_t px, uint8_t py); // full BG redraw + sprite refresh
void draw_cell(uint8_t mx, uint8_t my); // single map cell if visible (terrain only)
void draw_col_strip(uint8_t mx); // one world column for horizontal scroll
void draw_row_strip(uint8_t my); // one world row for vertical scroll
void draw_enemy_cells(uint8_t px, uint8_t py); // idle enemy glyph flip: OAM only (no BG/WIN redraw)
void draw_ui_rows(void); // window text panel + bottom HUD after camera moves (delegates to ui.c)

#endif // RENDER_H
