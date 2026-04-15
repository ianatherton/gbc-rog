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
#define UI_WINDOW_TILE_ROWS  4u  // window tile rows: 3 chat + bottom HUD — flush to LCD bottom (144 scanlines)
#define UI_WINDOW_PIXEL_H    ((uint8_t)(UI_WINDOW_TILE_ROWS * 8u)) // 32 — must match tile rows × 8
#define GRID_H               ((uint8_t)((144u - UI_WINDOW_PIXEL_H) / 8u)) // 14 — dungeon fills lines 0..UI_WINDOW_Y_START-1

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
//   Lines  0–(UI_WINDOW_Y_START-1): dungeon — full-width BKG scroll; WY off-screen
//   Lines  UI_WINDOW_Y_START–143 : bottom band — window rows 0–2 = log/seed/inspect; row 3 = HUD
#define UI_ROW_TOP       0               // legacy alias (dungeon now starts at screen line 0)
#define DUNGEON_ROW(y)   (y)             // viewport row y → same screen tile row (no top HUD strip)
#define UI_ROW_BOTTOM_1  (GRID_H + 1)    // legacy BKG text row aliases (unused in WIN layout)
#define UI_ROW_BOTTOM_2  (GRID_H + 2)
#define UI_ROW           UI_ROW_TOP      // shorthand alias
#define UI_WINDOW_Y_START    ((uint8_t)(144u - UI_WINDOW_PIXEL_H)) // 112 — WIN rows 0–3 map to scanlines 112–143 (HUD on true bottom)
#define UI_WINDOW_WY_OFFSCREEN 144u      // WY > 143: suppress window without HIDE_WIN (CGB may ignore LCDC.5 0→1 same frame)
#define UI_PANEL_WIN_Y0   0u             // top line of bottom band — combat log 1 / seed / inspect name
#define UI_PANEL_WIN_Y1   1u
#define UI_PANEL_WIN_Y2   2u             // last text row above HUD
#define UI_HUD_WIN_Y      3u             // bottom window row — L:♥ XP% FLOOR (physical screen bottom)
#define UI_PANEL_COLS     20u

/* ── Level generation ────────────────────────────────────────────────────── */
#define WALK_STEPS 4000   // scaled up from 350 to match the larger 64×64 map
#define NUM_PITS     1    // single descent tile per floor

/* ── Enemy roster ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES    30
#define NUM_ENEMIES    30
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
#define SCROLL_SPEED 2 // px/frame; 2 = 4-frame glide per tile for smooth visible interpolation
#define TURN_DELAY_MS 0 // extra ms after each resolved turn; 0 = only VBlank/scroll pacing (see main.c guard)

/* ── Logical tile IDs (returned by tile_at() for render compatibility) ───── */
// These are NOT stored in memory — tile_at() reconstructs them on-the-fly
// from the two bitsets (floor_bits, pit_bits).
#define TILE_WALL  0
#define TILE_FLOOR 1
#define TILE_PIT   2

/* ── Tileset VRAM layout ─────────────────────────────────────────────────── */
#define TILESET_VRAM_OFFSET 128 // first tile index in VRAM reserved for this sheet (above font)
#define TILESET_NTILES_ROM  256 // tiles in generated tileset_tiles[] (16×16 sheet)
/* GB BG VRAM is only 256 tiles total; uploading past 255 wraps and overwrites low tiles (font). */
#define TILESET_NTILES_VRAM ((uint8_t)(256u - TILESET_VRAM_OFFSET)) // =128: VRAM slots [128..255] only

/* ── Tileset tile indices (relative to TILESET_VRAM_OFFSET) ─────────────── */
/* Sheet: 16×16 grid of 8×8 tiles = 256 tiles total (256×256px)             */
/* Organized by column. Index = (row-1)*16 + col (A=0, B=1, … P=15)        */
/* Add TILESET_VRAM_OFFSET (128) for actual VRAM index in set_bkg_tiles.    */

/* ── A col — test tile + wall variants ──────────────────────────────────── */
#define TILE_TEST            0   /* A1  - debug/test tile                   */
#define TILE_WALL_A         16   /* A2  - wall variant 1 (default)          */
#define TILE_WALL_B         32   /* A3  - wall variant 2                    */
#define TILE_WALL_C         48   /* A4  - wall variant 3                    */
#define TILE_WALL_D         64   /* A5  - wall variant 4                    */
#define TILE_WALL_E         80   /* A6  - wall variant 5                    */
#define TILE_WALL_F         96   /* A7  - wall variant 6                    */
#define TILE_WALL_G        112   /* A8  - wall variant 7                    */

/* wall_tileset_index should cycle TILE_WALL_A through TILE_WALL_G (7 variants) */
#define TILE_WALL_FIRST    TILE_WALL_A
#define TILE_WALL_LAST     TILE_WALL_G
#define TILE_WALL_COUNT    7

/* ── B col — player class sprites ───────────────────────────────────────── */
#define TILE_CLASS_KNIGHT    1   /* B1  */
#define TILE_CLASS_BERSERKER 17  /* B2  */
#define TILE_CLASS_WITCH     33  /* B3  */
#define TILE_CLASS_SCOUNDREL 49  /* B4  */

/* ── C col — lighting objects (torches, lanterns, etc.) ─────────────────── */
#define TILE_LIGHT_1         2   /* C1  */
#define TILE_LIGHT_2        18   /* C2  */
#define TILE_LIGHT_3        34   /* C3  */
#define TILE_LIGHT_4        50   /* C4  */

/* ── D col — decorative columns ─────────────────────────────────────────── */
/* Placed when a wall tile has no orthogonal wall neighbour, or randomly.   */
#define TILE_COLUMN_1        3   /* D1  */
#define TILE_COLUMN_2       19   /* D2  */
#define TILE_COLUMN_3       35   /* D3  */
#define TILE_COLUMN_4       51   /* D4  */

/* ── E col — ground / floor tiles ───────────────────────────────────────── */
#define TILE_GROUND_A        4   /* E1  */
#define TILE_GROUND_B       20   /* E2  */
#define TILE_GROUND_C       36   /* E3  */
#define TILE_GROUND_D       52   /* E4  — also title-menu fire particle glyph */
#define TILE_TITLE_FIRE     TILE_GROUND_D
#define TILE_GROUND_E       68   /* E5  */

/* ── F col — props ───────────────────────────────────────────────────────── */
#define TILE_CHEST           5   /* F1  */
#define TILE_BARREL         21   /* F2  */
#define TILE_MUSHROOM       37   /* F3  */
/* F4 (index 53) — unused, skip */

/* ── G col — doors + shrine states ──────────────────────────────────────── */
#define TILE_DOOR_OPEN       6   /* G1  */
#define TILE_DOOR_CLOSED    22   /* G2  */
#define TILE_SHRINE_ON_1    38   /* G3  - active shrine animation frame 1  */
#define TILE_SHRINE_ON_2    54   /* G4  - active shrine animation frame 2  */
#define TILE_SHRINE_OFF     70   /* G5  - inactive shrine                  */

/* ── H col — stairs + pit ────────────────────────────────────────────────── */
#define TILE_STAIRS_UP_1     7   /* H1  */
#define TILE_LADDER_DOWN    23   /* H2  */
#define TILE_STAIRS_UP_2    39   /* H3  */
#define TILE_PIT_TILE       55   /* H4  - visual for pit hazard             */

/* ── I col — items (10 slots) ───────────────────────────────────────────── */
#define TILE_ITEM_1          8   /* I1  */
#define TILE_ITEM_2         24   /* I2  */
#define TILE_ITEM_3         40   /* I3  */
#define TILE_ITEM_4         56   /* I4  */
#define TILE_ITEM_5         72   /* I5  */
#define TILE_ITEM_6         88   /* I6  */
#define TILE_ITEM_7        104   /* I7  */
#define TILE_ITEM_8        120   /* I8  */
#define TILE_ITEM_9        136   /* I9  */
#define TILE_ITEM_10       152   /* I10 */

/* ── J col — enemy sprites ───────────────────────────────────────────────── */
#define TILE_SPIDER_1        9   /* J1  - spider frame 1                   */
#define TILE_SPIDER_2       25   /* J2  - spider frame 2                   */
#define TILE_MONSTER_1      41   /* J3  */
#define TILE_MONSTER_2      57   /* J4  */
#define TILE_MONSTER_3      73   /* J5  */
#define TILE_LOADING_SKULL   105  /* J7  - skull / loading adorn (row7 col J) */

/* K col (offset 10) — empty, reserved */

/* ── L col — floor decorations ──────────────────────────────────────────── */
#define TILE_FLOOR_DECO_1   11   /* L1  */
#define TILE_FLOOR_DECO_2   27   /* L2  */
#define TILE_FLOOR_DECO_3   43   /* L3  */
#define TILE_FLOOR_DECO_4   59   /* L4  */
#define TILE_FLOOR_DECO_5   75   /* L5  */
#define TILE_FLOOR_DECO_6   91   /* L6  */
#define TILE_FLOOR_DECO_7  107   /* L7  */

/* ── M col — directional arrows ─────────────────────────────────────────── */
#define TILE_ARROW_NE       12   /* M1  - top-right diagonal               */
#define TILE_ARROW_NW       28   /* M2  - top-left diagonal                */
#define TILE_ARROW_SW       44   /* M3  - bottom-left diagonal             */
#define TILE_ARROW_SE       60   /* M4  - bottom-right diagonal            */
/* M5 (index 76) — unused */

/* ── N+O col — HUD / UI tiles ───────────────────────────────────────────── */
#define TILE_UI_FLOOR_L     13   /* N1  - left portion of "FLOOR" word     */
#define TILE_UI_FLOOR_R     14   /* O1  - right portion of "FLOOR" word    */
#define TILE_UI_HEART_FULL  29   /* N2  - full heart                       */
#define TILE_UI_HEART_HALF  30   /* O2  - half heart                       */

/* ── Player stats ────────────────────────────────────────────────────────── */
#define PLAYER_HP_BASE_MAX 10
#define PLAYER_LEVEL_XP_BASE 15u
#define PLAYER_LEVEL_XP_STEP 5u
#define LIFE_BAR_LEN   5

/* Sheet-relative tile for hero (swap for berserker / witch / scoundrel) */
#define PLAYER_TILE_OFFSET TILE_CLASS_KNIGHT

/* ── CGB palette slot assignments (0–7) ─────────────────────────────────── */
#define PAL_PILLAR_BG 1 // BKG only: column/pillar wall cells; OCP slot 1 is enemy snake (separate CRAM)
#define PAL_WALL_BG 3   // dungeon bulk walls; wall_palette_table[wall_palette_index]
#define PAL_PLAYER  2   // hero gold on **sprites** (OCP); index shared with PAL_FLOOR_BG (BKG CRAM is separate)
#define PAL_FLOOR_BG 2u // BKG only: TILE_GROUND_C/D scatter deco (dark grey on black); blank + stairs use slot 0
#define PAL_LADDER  4   // BKG pit/ladder; OCP4 = skeleton (violet)
#define PAL_ENEMY_SNAKE     1 // serpent + adder — green on BKG+sprites
#define PAL_ENEMY_SKELETON  4 // violet bone; same index as PAL_LADDER, sprite CRAM only
#define PAL_ENEMY_RAT       5 // red–rose; BKG5 = life bar
#define PAL_ENEMY_GOBLIN    6 // magenta–pink; BKG6 = window HUD text
#define PAL_ENEMY_BAT       7 // aqua–turquoise; BKG7 = XP HUD
#define PAL_UI      6   // white on black — BKG/window HUD text
#define PAL_LIFE_UI 5   // red on black   — BKG life bar fill
#define PAL_XP_UI   7   // gold/yellow — BKG XP digits
#define PAL_CORPSE  0   // corpse 'x' uses default grayscale ramp (freed slot 7 for PAL_XP_UI)

/* ── Player/floor stats: globals.h ────────────────────────────────────────── */
extern uint16_t camera_px;    // pixel x of viewport top-left (defined in camera.c)
extern uint16_t camera_py;    // pixel y of viewport top-left (defined in camera.c)
#define CAM_TX (camera_px >> 3)
#define CAM_TY (camera_py >> 3)
#define RING_BKG_VY_WORLD(my) ((uint8_t)((my) & 31u)) // ring Y for world row my — SCY applies from line 0 in gameplay

/* ── Tileset pixel data ──────────────────────────────────────────────────── */
extern const uint8_t tileset_tiles[];

#endif // DEFS_H

