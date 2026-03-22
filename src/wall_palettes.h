#ifndef WALL_PALETTES_H
#define WALL_PALETTES_H

#include <gb/cgb.h>

#define NUM_WALL_PALETTES 40 // ROM table; hardware uses one BG slot, reloaded per index

extern const palette_color_t wall_palette_table[NUM_WALL_PALETTES][4];

#endif
