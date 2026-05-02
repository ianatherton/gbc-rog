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
#define UI_WINDOW_TILE_ROWS  5u  // window tile rows: belt + 3 chat + HUD — flush to LCD bottom (144 scanlines)
#define UI_WINDOW_PIXEL_H    ((uint8_t)(UI_WINDOW_TILE_ROWS * 8u)) // 40 — must match tile rows × 8
#define GRID_H               ((uint8_t)((144u - UI_WINDOW_PIXEL_H) / 8u)) // 13 — dungeon fills lines 0..UI_WINDOW_Y_START-1

/* ── Actual map dimensions ───────────────────────────────────────────────── */
// ⚠ MEMORY NOTE: GBC has 32 KB WRAM total.
//   The bitsets below cost MAP_W×MAP_H / 8 bytes each.
//   64×64 → 512 bytes per bitset ✓  96×96 → 1 152 bytes per bitset ✓
//   200×200 would need bank-switched WRAM (advanced topic).
#define MAP_W 96
#define MAP_H 96
// Total tile count as uint16_t to avoid 8-bit overflow in expressions.
#define MAP_TILES ((uint16_t)MAP_W * MAP_H)   // 9 216 for 96×96

/* ── Bitset storage size ─────────────────────────────────────────────────── */
// We store one BIT per tile instead of one BYTE.
// That is 8× more compact: 4 096 tiles → 512 bytes instead of 4 096.
// Two separate bitsets: one for "is floor/walkable", one for "is pit".
#define BITSET_BYTES ((MAP_W * MAP_H + 7) / 8)   // = 1 152 bytes for 96×96

/* ── Bitset access macros ────────────────────────────────────────────────── */
// Given a flat tile index I (= y*MAP_W + x):
//   byte slot  = I / 8  = I >> 3
//   bit within = I % 8  = I & 7
// TILE_IDX builds the flat index; the BIT_* macros read/write single bits.
#define TILE_IDX(x, y)      ((uint16_t)(y) * MAP_W + (x))
#define BIT_GET(arr, idx)   (((arr)[(idx) >> 3] >> ((idx) & 7)) & 1u)
#define BIT_SET(arr, idx)    ((arr)[(idx) >> 3] |=  (1u << ((idx) & 7)))
#define BIT_CLR(arr, idx)    ((arr)[(idx) >> 3] &= ~(1u << ((idx) & 7)))

#define FEATURE_MAP_FOG 1u // 1 enables explored-bit fog gate in renderer + reveal updates on movement
#define LIGHT_RADIUS_LADDER_DOWN 1u
#define LIGHT_RADIUS_STAIRS_UP 2u
#define LIGHT_RADIUS_BRAZIER 3u
#define LIGHT_RADIUS_TORCH 2u
#define LIGHT_RADIUS_KNIGHT 4u
#define LIGHT_RADIUS_ROGUE 2u
#define LIGHT_RADIUS_MAGE 3u
#define MAX_BRAZIERS 20u // upper bound for floor brazier placements before depth reduction

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
//   Lines  UI_WINDOW_Y_START–143 : bottom band — row 0 = belt stub; rows 1–3 = log/seed/inspect; row 4 = HUD
#define UI_ROW_TOP       0               // legacy alias (dungeon now starts at screen line 0)
#define DUNGEON_ROW(y)   (y)             // viewport row y → same screen tile row (no top HUD strip)
#define UI_ROW_BOTTOM_1  (GRID_H + 1)    // legacy BKG text row aliases (unused in WIN layout)
#define UI_ROW_BOTTOM_2  (GRID_H + 2)
#define UI_ROW           UI_ROW_TOP      // shorthand alias
#define UI_WINDOW_Y_START    ((uint8_t)(144u - UI_WINDOW_PIXEL_H)) // 104 — WIN rows 0–4 map to scanlines 104–143 (HUD on true bottom)
#define UI_WINDOW_WY_OFFSCREEN 144u      // WY > 143: suppress window without HIDE_WIN (CGB may ignore LCDC.5 0→1 same frame)
#define UI_BELT_WIN_Y     0u             // first window row — screen tile row GRID_H (belt stub)
#define UI_PANEL_WIN_Y0   1u             // combat log / seed / inspect — below belt
#define UI_PANEL_WIN_Y1   2u
#define UI_PANEL_WIN_Y2   3u             // last text row above HUD
#define UI_HUD_WIN_Y      4u             // bottom window row — L:♥ XP% FLOOR (physical screen bottom)
#define BELT_SLOT_COUNT   4u             // spell quick slots (left half of belt row)
#define BELT_ITEM_SLOT_COUNT 4u          // item quick slots (right half; mirrors inventory_kind[0..3])
#define BELT_TOTAL_SLOTS  ((uint8_t)(BELT_SLOT_COUNT + BELT_ITEM_SLOT_COUNT)) // SELECT cycles 0..7
#define UI_PANEL_COLS     20u
#define UI_CHAT_RECLAIM_AFTER_TURNS 8u // no new log lines for this many player turns → clear log, show idle class row until next push

/* ── Level generation ────────────────────────────────────────────────────── */
#define WALK_STEPS 4000   // scaled up from 350 to match the larger 64×64 map
#define NUM_PITS     1    // single descent tile per floor

/* ── Enemy roster ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES    28 // one fewer live slots; OAM layout reserves aura + player before enemy run
#define NUM_ENEMIES    28
#define ENEMY_DEAD    255

/* OAM draw order: lower index = in front (hardware). Aura must be < player or the 8×8 hero covers it completely.
   Flight FX (witch bolt, shield fireball) borrow SP_PLAYER_AURA_OAM during entity_sprites_run_projectile so bolts sit above the hero. */
#define SP_PLAYER_AURA_OAM    0u // M15/M16 gold flicker — slot also drives bolt/fireball FX (same index = above hero)
#define SP_PLAYER             1u // hero body
#define SP_ENEMY_BASE         2u // enemies use OAM [SP_ENEMY_BASE .. SP_ENEMY_BASE + num_enemies - 1]
#define MAX_ALLIES            4u // parallel ally slots — OAM SP_ALLY_BASE .. SP_ALLY_BASE+MAX_ALLIES-1 (above enemy run)
#define ALLY_TYPE_NONE        0u
#define ALLY_TYPE_FOX         1u // Scoundrel Call Fox — further types share ally_* arrays + per-type tick/OAM in ally layer
#define SP_ALLY_BASE          30u // first ally sprite; must stay below fixed UI sprites (e.g. SP_BELT_SELECTOR 35)
#define SP_BUFF_ICON         39u // top-right HUD slot for active player buffs (knight shield, etc.) — survives hide sweep

/* ── Enemy movement styles ───────────────────────────────────────────────── */
#define MOVE_CHASE   0   // always step toward the player
#define MOVE_RANDOM  1   // random walkable neighbour each turn
#define MOVE_WANDER  2   // 50% chase, 50% random
#define MOVE_BLINK   3   // teleport to a tile adjacent to player; def->param caps Chebyshev jump range (0 = default 3)
#define ENEMY_SLEEP_OFFSCREEN 1u // 1: skip AI updates for distant unrevealed enemies to stabilize crowded-floor turn cost
#define ENEMY_WAKE_MANHATTAN  12u // offscreen enemies inside this player distance still simulate so near-edge threats stay responsive

/* ── Enemy type IDs ──────────────────────────────────────────────────────── */
#define ENEMY_SERPENT   0
#define ENEMY_ADDER     1
#define ENEMY_RAT       2
#define ENEMY_BAT       3
#define ENEMY_SKELETON  4
#define ENEMY_GOBLIN    5
#define NUM_ENEMY_TYPES 6

/* ── Animation ───────────────────────────────────────────────────────────── */
// DIV_REG runs at 16384 Hz; 1638 ticks ≈ 0.10s between frame flips
#define ENEMY_ANIM_DIV_TICKS 1638UL

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

/* Char-create class emblem: 4 scratch VRAM tiles (normally ROM 124–127); restored before gameplay */
#define CLASS_EMBLEM_VRAM_START       252u
#define CLASS_EMBLEM_VRAM_ROM_RESTORE 124u

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
#define TILE_CLASS_BERSERKER 17  /* B2 — Zerker in-game */
#define TILE_CLASS_WITCH     33  /* B3  */
#define TILE_CLASS_SCOUNDREL 49  /* B4 — Scoundrel / rogue */

/* Class emblems on sheet: each is 2×2 (row 15–16 1-based). Knight = A15 B15 / A16 B16 → VRAM order TL,TR,BL,BR */
/* TL index below is A15 for Knight; Scoundrel C15; Witch E15; Zerker G15 (16-wide sheet: BR = TL+17).          */
#define TILE_EMBLEM_KNIGHT_TL     224u
#define TILE_EMBLEM_SCOUNDREL_TL  226u
#define TILE_EMBLEM_WITCH_TL      228u
#define TILE_EMBLEM_ZERKER_TL     230u

#define PLAYER_CLASS_COUNT 4u

/* Char-menu emblem BKG only: slots 4–7; gameplay load_palettes restores ladder/HUD/XP on those indices */
#define PAL_CLASS_EMBLEM_KNIGHT     4u
#define PAL_CLASS_EMBLEM_SCOUNDREL  5u
#define PAL_CLASS_EMBLEM_WITCH      6u
#define PAL_CLASS_EMBLEM_ZERKER     7u

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
#define TILE_COLUMN_5       67   /* D5  */
#define TILE_COLUMN_6       83   /* D6  */
#define TILE_COLUMN_7       99   /* D7  */

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
#define TILE_BAT_1          25   /* J2  - bat frame 1 (only used by Bat archetype) */
#define TILE_MONSTER_1      41   /* J3  */
#define TILE_MONSTER_2      57   /* J4  */
#define TILE_MONSTER_3      73   /* J5  */
#define TILE_LOADING_SKULL   105  /* J7  - skull / loading adorn (row7 col J) */
#define TILE_FOX_J9          137u /* J9  - sheet/ROM index (past first VRAM pack); copied to TILE_FOX_J9_VRAM at boot */
#define TILE_FOX_J9_VRAM     246u // OBJ + belt UI — same pattern as TILE_KNIGHT_SHIELD_VRAM for sheet tiles >127

/* K col (offset 10) — K1 reserved unused on map; VRAM tile patched from M14 at boot (see main.c) */
#define TILE_BAT_2          26   /* K2  - bat frame 2 (only used by Bat archetype) */

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
#define TILE_ARROW_LADDER   76   /* M5  - ladder marker sprite             */
#define TILE_SHEET_M12     188u  /* M12 - witch bolt art in source sheet   */
#define TILE_POOF_CLOUD    108u  /* M7  - enemy death puff (sprite; OCP0 grey/white ramp) */
#define TILE_PLAYER_AURA_ROM_A 236u /* M15 — copied to TILE_PLAYER_AURA_VRAM_* for OBJ */
#define TILE_PLAYER_AURA_ROM_B 252u /* M16 */
#define TILE_ZERKER_WHIRLWIND_VRAM 247u // copied from TILE_ITEM_10 (I10) at boot for Zerker Whirlwind belt icon
#define TILE_PLAYER_AURA_VRAM_A 248u // below CLASS_EMBLEM_VRAM_START 252 — gameplay aura only
#define TILE_PLAYER_AURA_VRAM_B 249u
#define TILE_WITCH_BOLT_VRAM 250u // copied from TILE_SHEET_M12 at boot for UI icon + projectile sprite
#define TILE_KNIGHT_SHIELD_VRAM 251u // copied from TILE_ITEM_9 (I9) at boot for knight shield UI icon + corner buff sprite
#define TILE_SHEET_M14     220u  /* M14 — empty belt slot (ROM); (14-1)*16+12, past VRAM 0..127 pack */

/* ── N+O col — HUD / UI tiles ───────────────────────────────────────────── */
#define TILE_UI_FLOOR_L     13   /* N1  - left portion of "FLOOR" word     */
#define TILE_UI_FLOOR_R     14   /* O1  - right portion of "FLOOR" word    */
#define TILE_UI_HEART_FULL  29   /* N2  - full heart                       */
#define TILE_UI_HEART_HALF  30   /* O2  - half heart                       */
#define TILE_UI_SPELL_L     61u  /* N4  - "SPELL" label left                 */
#define TILE_UI_SPELL_R     62u  /* O4  - "SPELL" label right               */
#define TILE_UI_ITEM_L      93u  /* N6  - "ITEM" label left                 */
#define TILE_UI_ITEM_R      94u  /* O6  - "ITEM" label right                */
#define TILE_UI_SLOT_EMPTY  10u  /* K1 VRAM index; bitmap replaced with TILE_SHEET_M14 at boot */

/* ── Player stats ────────────────────────────────────────────────────────── */
#define PLAYER_HP_BASE_MAX 10
#define PLAYER_LEVEL_XP_BASE 15u
#define PLAYER_LEVEL_XP_STEP 5u
#define LIFE_BAR_LEN   5

/* Sheet-relative tile for hero — see entity_sprites player_tile_offset_for_class */
#define PLAYER_TILE_OFFSET TILE_CLASS_KNIGHT

/* ── CGB palette slot assignments (0–7) ─────────────────────────────────── */
#define PAL_PILLAR_BG 1 // BKG only: column/pillar wall cells; OCP slot 1 is enemy snake (separate CRAM)
#define PAL_WALL_BG 3   // dungeon bulk walls; wall_palette_table[wall_palette_index]
#define PAL_PLAYER  2u  // hero OCP slot (same index as PAL_FLOOR_BG on BKG only — separate CRAM); class ramp uploaded in class_palettes_sprite_player_apply
#define PAL_PLAYER_KNIGHT     PAL_PLAYER // OAM uses PAL_PLAYER; knight ramp in class_palettes.c
#define PAL_PLAYER_SCOUNDREL  PAL_PLAYER
#define PAL_PLAYER_WITCH      PAL_PLAYER
#define PAL_PLAYER_ZERKER     PAL_PLAYER
#define PAL_FLOOR_BG 2u // BKG only: TILE_GROUND_C/D scatter deco (dark grey on black); blank + stairs use slot 0
#define PAL_LADDER  4   // BKG pit/ladder only (blinked in render.c); OCP4 = skeleton (violet)
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

