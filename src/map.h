#ifndef GAME_MAP_H
#define GAME_MAP_H

#include "defs.h"

/* ── Tile storage ────────────────────────────────────────────────────────── */
// Two bitsets replace the old single byte-per-tile map[] array.
//
//   floor_bits: bit=1 means the tile is open (floor OR pit).
//               bit=0 means it is a wall.
//
//   pit_bits:   bit=1 means the tile is specifically a pit.
//               Always a SUBSET of floor_bits (a pit is also open).
//
// Together they reconstruct the original three tile types:
//   !floor             → TILE_WALL
//    floor &&  pit     → TILE_PIT
//    floor && !pit     → TILE_FLOOR
//
// Memory: 2 × 512 = 1 024 bytes instead of the old 4 096 bytes.
extern uint8_t floor_bits[BITSET_BYTES];
extern uint8_t pit_bits[BITSET_BYTES];

/* ── Navigation graph ────────────────────────────────────────────────────── */
extern NavNode  nav_nodes[MAX_NAV_NODES];
extern uint8_t  num_nav_nodes;

/* ── Wall appearance (debug-cyclable) ────────────────────────────────────── */
extern uint8_t wall_tileset_index;
extern uint8_t wall_palette_index;

/* ── Tile accessors ──────────────────────────────────────────────────────── */
uint8_t tile_at(uint8_t x, uint8_t y);      // returns TILE_WALL / TILE_FLOOR / TILE_PIT
void    set_floor(uint8_t x, uint8_t y);    // marks tile as open floor
void    set_pit(uint8_t x, uint8_t y);      // marks tile as pit (also sets floor bit)
uint8_t is_walkable(uint8_t x, uint8_t y);  // 1 for floor or pit, 0 for wall

/* ── Rendering helpers ───────────────────────────────────────────────────── */
char    tile_char(uint8_t t);        // '#', '.', '0'
uint8_t tile_vram_index(uint8_t t);  // VRAM slot for custom graphic, or 0
uint8_t tile_palette(uint8_t t);     // CGB palette index for this tile type

/* ── Dungeon generation ──────────────────────────────────────────────────── */
// Clears both bitsets, carves floor via drunkard's walk, scatters pits,
// then calls build_nav_graph() automatically.
void generate_level(void);

/* ── Navigation graph queries ────────────────────────────────────────────── */
uint8_t nearest_nav_node(uint8_t x, uint8_t y);   // closest node by Manhattan distance
uint8_t nav_next_step(uint8_t from, uint8_t to);  // first-hop BFS from node to node

#endif // GAME_MAP_H

