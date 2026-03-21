#ifndef DEFS_H
#define DEFS_H

/* ── System headers ─────────────────────────────────────────────────────── */
#include <gb/gb.h>          // core GBDK: DISPLAY_ON/OFF, joypad(), wait_vbl_done(), delay()
#include <gb/cgb.h>         // Color Game Boy extras: set_bkg_palette(), RGB() macro
#include <gb/hardware.h>    // direct hardware registers: DIV_REG, SCX_REG, SCY_REG
#include <gbdk/console.h>   // text output: gotoxy(), setchar(), printf(), set_bkg_attribute_xy()
#include <gbdk/font.h>      // font helpers: font_init(), font_load(), font_color()
#include <rand.h>           // random numbers: initrand() to seed, rand() to get a value
#include <stdint.h>         // fixed-width types: uint8_t (0-255), uint16_t (0-65535)
#include <stdio.h>          // printf()

/* ── Viewport dimensions (what the player sees) ─────────────────────────── */
#define GRID_W 20   // visible columns
#define GRID_H 15   // visible dungeon rows (rows 1-15 on screen)

/* ── Actual map dimensions ───────────────────────────────────────────────── */
// ⚠ MEMORY NOTE: GBC has 32 KB WRAM total.
//   The bitsets below cost MAP_W×MAP_H / 8 bytes each.
//   64×64 → 512 bytes per bitset ✓  128×128 → 2 048 bytes per bitset ✓
//   200×200 would need bank-switched WRAM (advanced topic).
#define MAP_W 64
#define MAP_H 64
// Total tile count as uint16_t to avoid 8-bit overflow in expressions.
#define MAP_TILES ((uint16_t)MAP_W * MAP_H)   // 4 096 for 64×64

/* ── Bitset storage size ─────────────────────────────────────────────────── */
// We store one BIT per tile instead of one BYTE.
// That is 8× more compact: 4 096 tiles → 512 bytes instead of 4 096.
// Two separate bitsets: one for "is floor/walkable", one for "is pit".
#define BITSET_BYTES ((MAP_W * MAP_H + 7) / 8)   // = 512 bytes for 64×64

/* ── Bitset access macros ────────────────────────────────────────────────── */
// Given a flat tile index I (= y*MAP_W + x):
//   byte slot  = I / 8  = I >> 3
//   bit within = I % 8  = I & 7
// TILE_IDX builds the flat index; the BIT_* macros read/write single bits.
#define TILE_IDX(x, y)      ((uint16_t)(y) * MAP_W + (x))
#define BIT_GET(arr, idx)   (((arr)[(idx) >> 3] >> ((idx) & 7)) & 1u)
#define BIT_SET(arr, idx)    ((arr)[(idx) >> 3] |=  (1u << ((idx) & 7)))
#define BIT_CLR(arr, idx)    ((arr)[(idx) >> 3] &= ~(1u << ((idx) & 7)))

/* ── Navigation graph constants ──────────────────────────────────────────── */
// A nav-graph node sits at a "junction" floor tile — any tile that is NOT a
// plain straight corridor (i.e. has something other than exactly two
// directly-opposite walkable neighbours).
//
// Nodes are connected by tracing axis-aligned corridors between them.
// Enemies do BFS on this small graph instead of searching every tile.
//
// MAX_NAV_NODES caps memory use.  48 nodes × 6 bytes = 288 bytes.
#define MAX_NAV_NODES  48    // hard cap on junction nodes per level
#define NAV_NO_LINK   255    // sentinel: adjacency slot is empty / node not found
#define NAV_MIN_SPACE   4    // minimum Manhattan distance between two nodes
#define NAV_MAX_TRACE  24    // max tiles traced in one direction when connecting

// The navigation node struct.
// x, y  — position on the map.
// adj[] — indices (into nav_nodes[]) of connected nodes in the four
//          cardinal directions: 0=up 1=down 2=left 3=right.
//          NAV_NO_LINK means no connection in that direction.
typedef struct {
    uint8_t x, y;      // map position of this node
    uint8_t adj[4];    // links: adj[0]=up adj[1]=down adj[2]=left adj[3]=right
} NavNode;

/* ── Screen layout ───────────────────────────────────────────────────────── */
//   Row  0          : top HUD  (floor number + life bar)
//   Rows 1 – GRID_H : dungeon viewport  (15 rows)
//   Row  GRID_H + 1 : bottom UI row 1   (stats)
//   Row  GRID_H + 2 : bottom UI row 2   (item hotbar)
//   Total: 1 + 15 + 2 = 18 rows  ✓ fills the GBC screen exactly.
#define UI_ROW_TOP       0               // top HUD row
#define DUNGEON_ROW(y)   ((y) + 1)       // viewport row y → screen row y+1
#define UI_ROW_BOTTOM_1  (GRID_H + 1)    // bottom UI row 1 = screen row 16
#define UI_ROW_BOTTOM_2  (GRID_H + 2)    // bottom UI row 2 = screen row 17
#define UI_ROW           UI_ROW_TOP      // shorthand alias used in render.c

/* ── Level generation ────────────────────────────────────────────────────── */
#define WALK_STEPS 4000   // scaled up from 350 to match the larger 64×64 map
#define NUM_PITS    40    // scaled up from 6

/* ── Enemy roster ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES    20
#define NUM_ENEMIES    12
#define ENEMY_DEAD    255

/* ── Enemy movement styles ───────────────────────────────────────────────── */
#define MOVE_CHASE   0   // always step toward the player
#define MOVE_RANDOM  1   // random walkable neighbour each turn
#define MOVE_WANDER  2   // 50% chase, 50% random

/* ── Enemy type IDs ──────────────────────────────────────────────────────── */
#define ENEMY_SERPENT   0
#define ENEMY_ADDER     1
#define ENEMY_RAT       2
#define ENEMY_BAT       3
#define ENEMY_SKELETON  4
#define ENEMY_GOBLIN    5
#define NUM_ENEMY_TYPES 6

/* ── Animation ───────────────────────────────────────────────────────────── */
// DIV_REG runs at 16384 Hz; 68266 ticks ≈ 4.16s between flips (≈250 frames at 60fps)
#define ENEMY_ANIM_DIV_TICKS 18266UL

/* ── Corpses ─────────────────────────────────────────────────────────────── */
#define MAX_CORPSES MAX_ENEMIES

/* ── Timing ──────────────────────────────────────────────────────────────── */
#define TURN_DELAY_MS 60

/* ── Logical tile IDs (returned by tile_at() for render compatibility) ───── */
// These are NOT stored in memory — tile_at() reconstructs them on-the-fly
// from the two bitsets (floor_bits, pit_bits).
#define TILE_WALL  0
#define TILE_FLOOR 1
#define TILE_PIT   2

/* ── Tileset VRAM layout ─────────────────────────────────────────────────── */
#define TILESET_VRAM_OFFSET 128
#define TILESET_NTILES       26
#define TILE_WALL_1    1
#define TILE_MUSH_1    2
#define TILE_TORCH_1   3
#define TILE_TORCH_2   4
#define TILE_SKULL_1   5
#define TILE_CLASS_1   6
#define TILE_GROUND_1 16
#define TILE_WALL_2    9
#define TILE_MUSH_2   18
#define TILE_PILLAR_1 19
#define TILE_CHEST_1  20
#define TILE_BARREL_1 21
#define TILE_CLASS_2  22
#define TILE_ENEMY_1  23
#define TILE_ENEMY_2  24
#define TILE_ENEMY_3  25
#define TILE_DOOR_1   33
#define TILE_PILLAR_2 35
#define TILE_CLASS_3  38

/* ── Player spawn ────────────────────────────────────────────────────────── */
#define START_X (MAP_W / 2)
#define START_Y (MAP_H / 2)

/* ── Player stats ────────────────────────────────────────────────────────── */
#define PLAYER_HP_MAX 10
#define LIFE_BAR_LEN   5

/* ── CGB palette slot assignments (0–7) ─────────────────────────────────── */
#define PAL_UI      6   // white on black — HUD text
#define PAL_LIFE_UI 5   // red on black   — life bar fill
#define PAL_CORPSE  7   // dark green     — corpse 'x'

/* ── Globals defined in main.c ──────────────────────────────────────────── */
extern uint8_t  player_hp;    // current hit points
extern uint8_t  floor_num;    // current floor number (1-based)
extern uint16_t run_seed;    // immutable run seed — never changes mid-run; floor seed derived from this
extern uint16_t camera_px;    // pixel x of viewport top-left
extern uint16_t camera_py;    // pixel y of viewport top-left
#define CAM_TX (camera_px >> 3)
#define CAM_TY (camera_py >> 3)

/* ── Tileset pixel data ──────────────────────────────────────────────────── */
extern const uint8_t tileset_tiles[];

#endif // DEFS_H

