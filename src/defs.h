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
#define CANDLE_LIGHT_BONUS 3u // per use; stacks; cleared on new floor
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
#define MINIBOSS_FLOOR_NUM 3u // floor 3 always generates the miniboss biome
#define BOSS_FLOOR_NUM      5u // floor 5 always generates the boss biome
#define MAX_FLOORS     50u // hard cap; floor MAX_FLOORS pit ends the run

/* ── Enemy roster ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES    24 // OAM layout: 24 body slots + 4 skeleton-head slots fit before ally base
#define NUM_ENEMIES    24
#define ENEMY_DEAD    255

/* OAM draw order: lower index = in front (hardware). Aura must be < player or the 8×8 hero covers it completely.
   Flight FX (witch bolt, shield fireball) borrow SP_PLAYER_AURA_OAM during entity_sprites_run_projectile so bolts sit above the hero.
   Big Skell heads use SP_BIG_SKELL_HEAD_BASE..+MAX_BIG_SKELL_HEADS-1 (27..30) — managed by entity_sprites, excluded from hide sweep. */
#define SP_PLAYER_AURA_OAM    0u // M15/M16 gold flicker — slot also drives bolt/fireball FX (same index = above hero)
#define SP_PLAYER             1u // hero body
#define SP_ENEMY_BASE         3u // enemies use OAM [SP_ENEMY_BASE .. SP_ENEMY_BASE + MAX_ENEMIES - 1] = 3..26
#define SP_BIG_SKELL_HEAD_BASE 27u // big skell head overlays (up to MAX_BIG_SKELL_HEADS concurrent visible heads)
#define MAX_BIG_SKELL_HEADS    4u // head slots 27..30; must fit before SP_ALLY_BASE (31)
#define MAX_ALLIES            4u // parallel ally slots — OAM SP_ALLY_BASE .. SP_ALLY_BASE+MAX_ALLIES-1 (above enemy run)
#define ALLY_TYPE_NONE        0u
#define ALLY_TYPE_FOX         1u // Scoundrel Call Fox — further types share ally_* arrays + per-type tick/OAM in ally layer
#define SP_ALLY_BASE          31u // first ally sprite; must stay below fixed UI sprites (e.g. SP_BELT_SELECTOR 35)
#define SP_BUFF_ICON         39u // top-right HUD slot for active player buffs (knight shield, etc.) — survives hide sweep

/* ── Enemy movement styles ───────────────────────────────────────────────── */
#define MOVE_CHASE   0   // always step toward the player
#define MOVE_RANDOM  1   // random walkable neighbour each turn
#define MOVE_WANDER  2   // 50% chase, 50% random
#define MOVE_BLINK   3   // teleport to a tile adjacent to player; def->param caps Chebyshev jump range (0 = default 3)
#define ENEMY_SLEEP_OFFSCREEN 1u // 1: skip AI updates for distant unrevealed enemies to stabilize crowded-floor turn cost
#define ENEMY_WAKE_MANHATTAN  12u // offscreen enemies inside this player distance still simulate so near-edge threats stay responsive

/* ── Enemy type IDs ──────────────────────────────────────────────────────── */
#define ENEMY_SNAKE     0
#define ENEMY_SLIME     1
#define ENEMY_RAT       2
#define ENEMY_BAT       3
#define ENEMY_BIG_SKELL 4
#define ENEMY_IMP       5
#define ENEMY_SKELETON  6
#define ENEMY_GORGON    7
#define ENEMY_SLIME_BIG 8 // 2x-scaled Slime miniboss; visual-only footprint, reuses Slime AI + melee-split behavior
#define NUM_ENEMY_TYPES 9

/* ── Animation ───────────────────────────────────────────────────────────── */
// DIV_REG runs at 16384 Hz; 1638 ticks ≈ 0.10s between frame flips
#define ENEMY_ANIM_DIV_TICKS 1638UL

/* ── Corpses ─────────────────────────────────────────────────────────────── */
#define MAX_CORPSES MAX_ENEMIES

/* ── Timing ──────────────────────────────────────────────────────────────── */
#define SCROLL_SPEED 1 // px/frame; 2 = 4-frame glide per tile (smooth); 4 was snappy but visible jump
#define ENEMY_GLIDE_SPEED 1u // px/frame for enemy slide
#define ALLY_GLIDE_SPEED  1u // px/frame for fox slide; offset capped to 8px in glide_begin so always converges in 8 scroll frames
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
#define TILE_B6              81  /* B6 — unused placeholder (ladder/fence); reused as torch-frame decor on the title screen */

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
#define TILE_LIGHT_5        66   /* C5  — dead variant, never placed; VRAM 194 borrowed by TILE_SLIMEBIG_TL */
#define TILE_LIGHT_6        82   /* C6  */

/* ── D col — decorative columns ─────────────────────────────────────────── */
/* Placed when a wall tile has no orthogonal wall neighbour, or randomly.   */
#define TILE_COLUMN_1        3   /* D1  */
#define TILE_COLUMN_2       19   /* D2  */
#define TILE_COLUMN_3       35   /* D3  */
#define TILE_COLUMN_4       51   /* D4  */
#define TILE_COLUMN_5       67   /* D5  — dead variant, never placed; VRAM 195 borrowed by TILE_SLIMEBIG_TR */
#define TILE_COLUMN_6       83   /* D6  */
#define TILE_COLUMN_7       99   /* D7  */

/* ── E col — ground / floor tiles ───────────────────────────────────────── */
#define TILE_GROUND_A        4   /* E1  */
#define TILE_GROUND_B       20   /* E2  */
#define TILE_GROUND_C       36   /* E3  */
#define TILE_GROUND_D       52   /* E4  — also title-menu fire particle glyph */
#define TILE_TITLE_FIRE     TILE_GROUND_D
#define TILE_GROUND_E       68   /* E5  — dead variant, never placed; VRAM 196 borrowed by TILE_SLIMEBIG_BL */

/* ── F col — props ───────────────────────────────────────────────────────── */
#define TILE_CHEST           5   /* F1  */
#define TILE_BARREL         21   /* F2  */
#define TILE_MUSHROOM       37   /* F3  */
/* F4 (index 53) — unused, skip (was a stale c10 home; the title logo restore stomped VRAM 181). */
/* Overworld terrain art (hub only). Both ROM sources live past the first-128 VRAM upload, so they
   are boot-copied into title-safe VRAM slots — the title logo (title_logo.c) patches+restores
   128..181, so the wall/water slots must sit ≥182 and outside that table or they get blanked. */
#define TILE_C10                 146u /* C10 sheet source = (10-1)*16 + 2 — overworld wall (pine tree) art */
#define TILE_F10                 149u /* F10 sheet source = (10-1)*16 + 5 — overworld water (border) art */
#define TILE_OVERWORLD_WALL_OFF   85u /* borrows unused F6 VRAM slot (213); renderer adds TILESET_VRAM_OFFSET */
#define TILE_OVERWORLD_WATER_OFF  86u /* borrows unused G6 VRAM slot (214) */
#define TILE_OVERWORLD_WALL_VRAM  ((uint8_t)(TILESET_VRAM_OFFSET + TILE_OVERWORLD_WALL_OFF))  /* =213 tree */
#define TILE_OVERWORLD_WATER_VRAM ((uint8_t)(TILESET_VRAM_OFFSET + TILE_OVERWORLD_WATER_OFF)) /* =214 water */
#define OVERWORLD_BORDER_BAND      2u /* hub: outermost N tiles are the blue water border */

/* ── G col — doors + shrine states ──────────────────────────────────────── */
#define TILE_DOOR_OPEN       6   /* G1  */
#define TILE_DOOR_CLOSED    22   /* G2  */
#define TILE_SHRINE_ON_1    38   /* G3  - active shrine animation frame 1  */
#define TILE_SHRINE_ON_2    54   /* G4  - active shrine animation frame 2  */
#define TILE_SHRINE_OFF     70   /* G5  - inactive shrine; dead, never placed; VRAM 198 borrowed by TILE_SLIMEBIG_BR */

/* ── H col — stairs + pit ────────────────────────────────────────────────── */
#define TILE_STAIRS_UP_1     7   /* H1  */
#define TILE_LADDER_DOWN    23   /* H2  */
#define TILE_STAIRS_UP_2    39   /* H3  */
#define TILE_PIT_TILE       55   /* H4  - visual for pit hazard             */
#define TILE_H11           167u  /* H11 - book item; ROM row 11 → needs boot copy */
#define TILE_BOOK_H11_VRAM 199u  /* borrows H5 VRAM slot (ROM tile 71, not placed by map) */
#define TILE_BOOK_BELT_OFF ((uint8_t)(TILE_BOOK_H11_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(BOOK_*)
#define TILE_SHEET_H12     183u  /* H12 - arrow projectile art; ROM (12-1)*16+7, sheet >127 → boot copy */
#define TILE_ARROW_VRAM    219u  /* arrow projectile sprite — borrows unused L6 (FLOOR_DECO_6) VRAM slot */

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
#define TILE_ITEM_11       168   /* I11 — scroll; ROM past first VRAM pack → TILE_SCROLL_I11_VRAM */
#define TILE_ITEM_12       184   /* I12 — BigHeal potion; ROM past first VRAM pack → TILE_BIGHEAL_I12_VRAM */

#define TILE_SCROLL_I11_VRAM     245u // belt/pickup UI — copied from TILE_ITEM_11 at boot (sheet >127)
#define TILE_SCROLL_BELT_OFF ((uint8_t)(TILE_SCROLL_I11_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(SCROLL)
#define TILE_BIGHEAL_I12_VRAM    243u // belt/pickup UI — copied from TILE_ITEM_12 at boot
#define TILE_BIGHEAL_BELT_OFF ((uint8_t)(TILE_BIGHEAL_I12_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(KEY)
#define TILE_SHEET_I15           232u // I15 — bow & arrow item art; ROM (15-1)*16+8, sheet >127 → boot copy
#define TILE_BOW_VRAM            235u // belt/pickup UI — copied from TILE_SHEET_I15 at boot; borrows unused L7 (FLOOR_DECO_7) slot
#define TILE_BOW_BELT_OFF ((uint8_t)(TILE_BOW_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(BOW)
#define TILE_AXE_BELT_OFF    ((uint8_t)(TILE_ZERKER_WHIRLWIND_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(AXE)   — shares VRAM slot with whirlwind icon
#define TILE_SHIELD_BELT_OFF ((uint8_t)(TILE_KNIGHT_SHIELD_VRAM    - TILESET_VRAM_OFFSET)) // items_kind_tile(SHIELD) — shares VRAM slot with knight shield icon

/* ── J col — enemy sprites ───────────────────────────────────────────────── */
#define TILE_SPIDER_1        9   /* J1  - spider frame 1 (unused after snake remap) */
#define TILE_BAT_1          25   /* J2  - bat frame 1 (only used by Bat archetype) */
#define TILE_MONSTER_1      41   /* J3  - skeleton frame 1                 */
#define TILE_MONSTER_2      57   /* J4  - skeleton frame 2 / imp (flip-anim) */
#define TILE_MONSTER_3      73   /* J5  - (= TILE_SNAKE_1)                 */
#define TILE_SNAKE_1        73   /* J5  - snake frame 1                    */
#define TILE_LOADING_SKULL   105  /* J7  - skull / loading adorn (row7 col J) */
#define TILE_BIG_SKELL_HEAD   105u /* J7  - big skell head overlay (= TILE_LOADING_SKULL) */

/* J8 big skell body — ROM slot 121 maps to VRAM 249 = TILE_PLAYER_AURA_VRAM_B (patched at boot),
   so J8 is boot-patched to a borrowed VRAM slot instead (same pattern as slime/rat). */
#define TILE_BIG_SKELL_BODY_ROM  121u /* J8  — big skell body, ROM source         */
#define TILE_BIG_SKELL_BODY_VRAM 234u /* borrows unused K7 VRAM slot (sheet 106) */
#define TILE_BIG_SKELL_BODY      106u /* = TILE_BIG_SKELL_BODY_VRAM - TILESET_VRAM_OFFSET; use in EnemyDef.tile */

/* J10/K10 small skeleton — ROM past first VRAM pack; boot-patched to borrowed N7/O7 slots */
#define TILE_SKEL_ROM_1   153u  /* J10 — small skel frame 1, ROM source        */
#define TILE_SKEL_ROM_2   154u  /* K10 — small skel frame 2, ROM source        */
#define TILE_SKEL_1_VRAM  237u  /* borrows unused N7 VRAM slot (sheet 109)     */
#define TILE_SKEL_2_VRAM  238u  /* borrows unused O7 VRAM slot (sheet 110)     */
#define TILE_SKEL_1_OFF   109u  /* = TILE_SKEL_1_VRAM - TILESET_VRAM_OFFSET; use in EnemyDef.tile */
#define TILE_SKEL_2_OFF   110u  /* = TILE_SKEL_2_VRAM - TILESET_VRAM_OFFSET */
#define TILE_FOX_J9          137u /* J9  - sheet/ROM index (past first VRAM pack); copied to TILE_FOX_J9_VRAM at boot */
#define TILE_FOX_J9_VRAM     246u // OBJ + belt UI — same pattern as TILE_KNIGHT_SHIELD_VRAM for sheet tiles >127

/* K col (offset 10) — K1 reserved unused on map; VRAM tile patched from M14 at boot (see main.c) */
#define TILE_BAT_2          26   /* K2  - bat frame 2 (only used by Bat archetype) */
#define TILE_SNAKE_2        74   /* K5  - snake frame 2                    */

/* J11/K11 slime tiles — ROM row 11, past first VRAM pack; boot-patched to borrowed J6/K6 slots */
#define TILE_SLIME_ROM_1   169u  /* J11 — slime frame 1, ROM source        */
#define TILE_SLIME_ROM_2   170u  /* K11 — slime frame 2, ROM source        */
#define TILE_SLIME_1_VRAM  217u  /* borrows unused J6 VRAM slot (sheet 89) */
#define TILE_SLIME_2_VRAM  218u  /* borrows unused K6 VRAM slot (sheet 90) */
#define TILE_SLIME_1_OFF    89u  /* = TILE_SLIME_1_VRAM - TILESET_VRAM_OFFSET; use in EnemyDef.tile */
#define TILE_SLIME_2_OFF    90u  /* = TILE_SLIME_2_VRAM - TILESET_VRAM_OFFSET */

/* J16 rat tile — ROM row 16, past first VRAM pack; boot-patched to borrowed P7 slot */
#define TILE_RAT_ROM       249u  /* J16 — rat, ROM source                  */
#define TILE_RAT_VRAM      239u  /* borrows unused P7 VRAM slot (sheet 111) */
#define TILE_RAT_OFF       111u  /* = TILE_RAT_VRAM - TILESET_VRAM_OFFSET; use in EnemyDef.tile */

/* N/O rows 10-12 — Gorgon boss sprite (2×3 tiles). ROM indices past first VRAM pack.
   Boot-patched to borrowed row-7 slots (B7/C7/E7/F7/G7/H7) whose original ROM content
   is never placed as a BG tile. Head row (N10/O10) flips horizontally for animation.
   Until tile art exists, EnemyDef uses BIG_SKELL placeholder tiles. */
#define TILE_GORGON_HEAD_L_ROM  157u  /* N10 — head left                    */
#define TILE_GORGON_HEAD_R_ROM  158u  /* O10 — head right                   */
#define TILE_GORGON_BODY_L_ROM  173u  /* N11 — body left                    */
#define TILE_GORGON_BODY_R_ROM  174u  /* O11 — body right                   */
#define TILE_GORGON_FEET_L_ROM  189u  /* N12 — feet left (collision tile)   */
#define TILE_GORGON_FEET_R_ROM  190u  /* O12 — feet right                   */
#define TILE_GORGON_HEAD_L_VRAM 225u  /* borrows B7=ROM97  (unused)         */
#define TILE_GORGON_HEAD_R_VRAM 226u  /* borrows C7=ROM98  (unused)         */
#define TILE_GORGON_BODY_L_VRAM 228u  /* borrows E7=ROM100 (unused)         */
#define TILE_GORGON_BODY_R_VRAM 229u  /* borrows F7=ROM101 (unused)         */
#define TILE_GORGON_FEET_L_VRAM 230u  /* borrows G7=ROM102 (unused)         */
#define TILE_GORGON_FEET_R_VRAM 231u  /* borrows H7=ROM103 (unused)         */
#define TILE_GORGON_HEAD_L_OFF   97u  /* = VRAM - TILESET_VRAM_OFFSET       */
#define TILE_GORGON_HEAD_R_OFF   98u
#define TILE_GORGON_BODY_L_OFF  100u
#define TILE_GORGON_BODY_R_OFF  101u
#define TILE_GORGON_FEET_L_OFF  102u
#define TILE_GORGON_FEET_R_OFF  103u

/* A9-D9 — 2x-scaled Slime miniboss (2×2 tiles, nearest-neighbor upscale of TILE_SLIME_ROM_1).
   ROM indices past first VRAM pack, never auto-loaded. Boot-patched to borrowed dead-variant
   slots — C5/D5/E5/G5 are the Nth drawn variant of a themed column (lights/columns/ground/
   shrine) that the game logic never wires up, confirmed by zero references anywhere outside
   their own #define (same precedent as TILE_FLOOR_DECO_6/7/8 already borrowed for arrow/bow/
   knight-shield). Visual-only scale-up: the enemy still occupies a single logical tile.
   NOTE: an earlier version of this borrowed A7/D7/I7/J7, which turned out to be live content
   (TILE_WALL_F, TILE_COLUMN_7, TILE_ITEM_7, TILE_LOADING_SKULL/TILE_BIG_SKELL_HEAD) — do not
   reuse those for anything. */
#define TILE_SLIMEBIG_TL_ROM  128u  /* A9 */
#define TILE_SLIMEBIG_TR_ROM  129u  /* B9 */
#define TILE_SLIMEBIG_BL_ROM  130u  /* C9 */
#define TILE_SLIMEBIG_BR_ROM  131u  /* D9 */
#define TILE_SLIMEBIG_TL_VRAM 194u  /* borrows C5=ROM66 (TILE_LIGHT_5, dead variant) */
#define TILE_SLIMEBIG_TR_VRAM 195u  /* borrows D5=ROM67 (TILE_COLUMN_5, dead variant) */
#define TILE_SLIMEBIG_BL_VRAM 196u  /* borrows E5=ROM68 (TILE_GROUND_E, dead variant) */
#define TILE_SLIMEBIG_BR_VRAM 198u  /* borrows G5=ROM70 (TILE_SHRINE_OFF, dead variant) */
#define TILE_SLIMEBIG_TL_OFF   66u  /* = VRAM - TILESET_VRAM_OFFSET */
#define TILE_SLIMEBIG_TR_OFF   67u
#define TILE_SLIMEBIG_BL_OFF   68u
#define TILE_SLIMEBIG_BR_OFF   70u

/* ── L col — floor decorations ──────────────────────────────────────────── */
#define TILE_FLOOR_DECO_1   11   /* L1  */
#define TILE_FLOOR_DECO_2   27   /* L2  */
#define TILE_FLOOR_DECO_3   43   /* L3  */
#define TILE_FLOOR_DECO_4   59   /* L4  */
#define TILE_FLOOR_DECO_5   75   /* L5  */
#define TILE_FLOOR_DECO_6   91   /* L6  */
#define TILE_FLOOR_DECO_7  107   /* L7  */
#define TILE_FLOOR_DECO_8  123   /* L8  */
/* L6/L7/L8's own VRAM slots (TILESET_VRAM_OFFSET+91/107/123 = 219/235/251) are permanently borrowed at boot
   for the arrow/bow/knight-shield icons (see TILE_ARROW_VRAM / TILE_BOW_VRAM / TILE_KNIGHT_SHIELD_VRAM) — reading
   those slots back does NOT show the original L6/L7/L8 art. To actually display it (e.g. title-screen FX), read
   the ROM tiles fresh via tileset_read_tiles() into dedicated scratch VRAM, then restore before leaving that screen. */
#define TILE_TITLE_FIREANIM_VRAM_1 182u // title-only scratch (1 of 3) — borrows G4 shrine-anim-2 slot
#define TILE_TITLE_FIREANIM_VRAM_2 183u // borrows H4 pit-hazard slot
#define TILE_TITLE_FIREANIM_VRAM_3 184u // borrows I4 item slot
#define TILE_SMILE_L10     155u  /* L10 — smile over player on level-up; ROM >127 → TILE_LEVELUP_SMILE_VRAM */

#define TILE_LEVELUP_SMILE_VRAM 244u // OBJ — copied from TILE_SMILE_L10 at boot

/* ── M col — directional arrows ─────────────────────────────────────────── */
#define TILE_ARROW_NE       12   /* M1  - top-right diagonal               */
#define TILE_ARROW_NW       28   /* M2  - top-left diagonal                */
#define TILE_ARROW_SW       44   /* M3  - down arrow / ladder marker       */
#define TILE_ARROW_SE       60   /* M4  - bottom-right diagonal            */
#define TILE_ARROW_LADDER   TILE_ARROW_SW
#define TILE_HOURGLASS_BELT_OFF 76u /* M5  - hourglass; belt cooldown indicator */
#define TILE_POOF_CLOUD    108u  /* M7  - enemy death puff (sprite; OCP0 grey/white ramp) */
#define TILE_EQUIP_MARK    172u  /* M11 - equipped indicator overlay; ROM past first 128 → TILE_EQUIP_MARK_VRAM */
#define TILE_EQUIP_MARK_VRAM 241u /* borrows B8 VRAM slot (tile 113; not placed by any map code) */
#define TILE_SHEET_M12     188u  /* M12 - witch bolt art in source sheet   */
#define TILE_PLAYER_AURA_ROM_A 236u /* M15 — copied to TILE_PLAYER_AURA_VRAM_* for OBJ */
#define TILE_PLAYER_AURA_ROM_B 252u /* M16 */
#define TILE_SHEET_M9          140u   /* M9  - root indicator glyph; ROM index (9-1)*16+12; copied to VRAM at boot */
#define TILE_SHEET_L11         171u   /* L11 - root indicator glyph; ROM index (11-1)*16+11 */
#define TILE_ROOT_ICON_VRAM    242u   /* borrows unused C8 VRAM slot (ROM tile 114, not placed by any map code) */
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
#define PLAYER_HP_BASE_MAX 20
#define PLAYER_LEVEL_XP_BASE 45u
#define PLAYER_LEVEL_XP_STEP 15u
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
#define PAL_GORGON_BODY     4u // boss floor: reuses skeleton slot (no skeletons on floor 3)
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

