#ifndef GAME_MAP_H
#define GAME_MAP_H

#include "defs.h"

extern uint8_t floor_bits[BITSET_BYTES]; // 1 = carved open (floor or pit)
extern uint8_t pit_bits[BITSET_BYTES];   // 1 = pit (subset of floor_bits)

extern NavNode  nav_nodes[MAX_NAV_NODES]; // junction graph for chase AI
extern uint8_t  num_nav_nodes;            // populated after generate_level

extern uint8_t wall_tileset_index; // which wall tile in VRAM band (debug)
extern uint8_t wall_palette_index; // wall_palette_table index; applied to PAL_WALL_BG slot

uint8_t tile_at(uint8_t x, uint8_t y); // TILE_WALL / TILE_FLOOR / TILE_PIT from bitsets
void    set_floor(uint8_t x, uint8_t y); // carve floor-only
void    set_pit(uint8_t x, uint8_t y); // carve pit (sets both bits)
uint8_t is_walkable(uint8_t x, uint8_t y); // floor_bits only

char    tile_char(uint8_t t); // ASCII when not using custom tile
uint8_t tile_vram_index(uint8_t t); // non-0 → set_bkg_tiles index
uint8_t tile_palette(uint8_t t); // CGB palette for terrain

void generate_level(void); // drunkard walk + pits + build_nav_graph

uint8_t nearest_nav_node(uint8_t x, uint8_t y); // for mapping entity tiles to graph
uint8_t nav_next_step(uint8_t from, uint8_t to); // BFS first hop on nav graph

#endif // GAME_MAP_H

