#ifndef GAME_MAP_H
#define GAME_MAP_H

#include "defs.h"
#include <gbdk/platform.h>

extern uint8_t floor_bits[BITSET_BYTES]; // 1 = carved open (floor or pit)
extern uint8_t pit_bits[BITSET_BYTES];   // 1 = pit (subset of floor_bits)
extern uint8_t explored_bits[BITSET_BYTES]; // 1 = tile was revealed to player (fog-of-war scaffold)

extern NavNode  nav_nodes[MAX_NAV_NODES]; // junction graph for chase AI
extern uint8_t  num_nav_nodes;            // populated after generate_level

extern uint8_t wall_tileset_index; // which wall tile in VRAM band (debug)
extern uint8_t wall_palette_index; // wall_palette_table index → PAL_WALL_BG
extern uint8_t pillar_palette_index; // wall_palette_table index → PAL_PILLAR_BG (column tiles)
extern uint8_t floor_column_off; // D1..D4 sheet offset; pillars use this, bulk uses wall_tileset_index
extern uint8_t active_map_w; // runtime floor bounds within MAP_W storage
extern uint8_t active_map_h;
extern uint8_t player_spawn_x;    // set per floor in generate_level — seed-based, walkable
extern uint8_t player_spawn_y;
extern uint8_t brazier_count; // number of active floor light sources
extern uint8_t brazier_x[MAX_BRAZIERS];
extern uint8_t brazier_y[MAX_BRAZIERS];
extern uint8_t brazier_type[MAX_BRAZIERS]; // 0=brazier C3/C4, 1=torch C1/C2

uint8_t tile_at(uint8_t x, uint8_t y) BANKED; // TILE_WALL / TILE_FLOOR / TILE_PIT from bitsets
void    set_floor(uint8_t x, uint8_t y); // carve floor-only
void    set_pit(uint8_t x, uint8_t y); // carve pit (sets both bits)
uint8_t is_walkable(uint8_t x, uint8_t y) BANKED; // floor_bits only — BANKED: callers outside bank 2 (e.g. scoundrel_fox)

char    tile_char(uint8_t t); // ASCII when not using custom tile
uint8_t tile_vram_index(uint8_t t); // non-0 → set_bkg_tiles index
uint8_t tile_palette(uint8_t t); // CGB palette for terrain

void generate_level(uint16_t floor_seed); // drunkard from spawn + pits + build_nav_graph

void level_init_display(uint8_t from_pit) BANKED; // bank 2 with gameplay; far-call from other banks
void level_generate_and_spawn(uint8_t *px, uint8_t *py) BANKED; // bank 2; far-call from state_transition (bank 1)

void    floor_ground_init(uint16_t floor_seed); // per floor: visual variant seed for floor deco selection
uint8_t floor_tile_sheet_offset(uint8_t x, uint8_t y); // sheet offset for TILE_FLOOR cell; 255 = blank tile
uint8_t floor_tile_palette_xy(uint8_t x, uint8_t y); // CGB attr: stairs/blank ->0; floor deco -> PAL_FLOOR_BG; brazier/torch -> PAL_LADDER
uint8_t wall_ortho_wall_count_xy(uint8_t x, uint8_t y); // count orthogonal wall neighbors from floor_bits only
void lighting_reset(void); // clear revealed bits for new floor
void lighting_reveal_radius(uint8_t cx, uint8_t cy, uint8_t radius); // reveal a square around center
uint8_t lighting_is_revealed(uint8_t x, uint8_t y); // fog gate helper for renderer
uint8_t lighting_dirty_count(void); // tiles newly revealed in last lighting_reveal_radius (fog only)
void lighting_dirty_tile(uint8_t i, uint8_t *x, uint8_t *y); // i < lighting_dirty_count()
uint8_t lighting_dirty_overflow(void); // 1 if last reveal exceeded buffer — use full draw_screen
void lighting_dirty_clear(void); // after painting dirty cells (optional hygiene between reveals)

uint8_t nearest_nav_node(uint8_t x, uint8_t y); // for mapping entity tiles to graph
uint8_t nav_next_step(uint8_t from, uint8_t to); // BFS first hop on nav graph
uint8_t map_pit_position(uint8_t *x, uint8_t *y); // 1 when floor has a down-ladder pit coordinate

uint8_t ground_item_index_at(uint8_t x, uint8_t y); // ground_item_* slot at (x,y), 255 = none
void    ground_item_kill(uint8_t slot); // remove a ground item (pickup or discard)

#endif // GAME_MAP_H

