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
#define MACE_STUN_CHANCE_PCT 25u // chance per melee hit while Mace is equipped
#define MACE_STUN_TURNS       3u // enemy_stun[] duration on a successful Mace stun

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
/* Miniboss/boss floors are per-dungeon now — locals 2/4 of each dungeon (see dungeon.h). */
#define MAX_FLOORS     50u // persistence-array size; floors 1-36 = dungeons, 37-45 = guardroom keys

/* ── Enemy roster ────────────────────────────────────────────────────────── */
#define MAX_ENEMIES    23 // OAM: 23 body slots (4..26) + 4 skeleton-head slots (27..30) before ally base
#define NUM_ENEMIES    23 // was 24; capped when the hero went 2-tile (hero now owns OAM 1..2, debuff icon 3)
#define ENEMY_DEAD    255

/* OAM draw order: lower index = in front (hardware). Aura must be < player or the 8×8 hero covers it completely.
   Flight FX (witch bolt, shield fireball) borrow SP_PLAYER_AURA_OAM during entity_sprites_run_projectile so bolts sit above the hero.
   Big Skell heads use SP_BIG_SKELL_HEAD_BASE..+MAX_BIG_SKELL_HEADS-1 (27..30) — managed by entity_sprites, excluded from hide sweep. */
#define SP_PLAYER_AURA_OAM    0u // M15/M16 gold flicker — slot also drives bolt/fireball + weapon-lunge FX (same index = above hero)
#define SP_PLAYER_HEAD        1u // hero head — top tile of the 2-tile hero; below only the weapon/FX slot so
                                  // nothing (body, enemies, allies) ever draws over the hero's face.
#define SP_PLAYER             2u // hero body — bottom tile of the 2-tile-tall hero; head overlaps it by 2px.
#define SP_ENEMY_BASE         4u // enemies use OAM [SP_ENEMY_BASE .. SP_ENEMY_BASE + MAX_ENEMIES - 1] = 4..26
                                  // (Sphinx boss borrows the first 10; the hub borrows the first 2 for waypoint stun-fx — no enemies on floor 0)
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
#define ENEMY_SLIME_BIG 8 // 2x-scaled Slime miniboss; reuses Slime AI; 2-tile Gorgon-style footprint;
                          // animated 2-frame; guaranteed ~10-slime spawn on death (enemy_extras.c)
#define ENEMY_SPHINX    9 // floor-6 boss; 3x2 body + flapping wings, 10 OAM tiles; 2-tile Gorgon-style footprint
#define NUM_ENEMY_TYPES 10

/* ── Animation ───────────────────────────────────────────────────────────── */
// DIV_REG runs at 16384 Hz; 1638 ticks ≈ 0.10s between frame flips
#define ENEMY_ANIM_DIV_TICKS 1638UL

/* ── Corpses ─────────────────────────────────────────────────────────────── */
#define MAX_CORPSES MAX_ENEMIES

/* ── Timing ──────────────────────────────────────────────────────────────── */
#define SCROLL_SPEED 1 // px/frame; 2 = 4-frame glide per tile (smooth); 4 was snappy but visible jump
#define AUTO_SCROLL_SPEED 4 // px/frame while auto-exploring — 2-frame fast walk (camera.c gates on auto_explore_active)
#define ENEMY_GLIDE_SPEED 1u // px/frame for enemy slide
#define ALLY_GLIDE_SPEED  1u // px/frame for fox slide; offset capped to 8px in glide_begin so always converges in 8 scroll frames
#define TURN_DELAY_MS 0 // extra ms after each resolved turn; 0 = only VBlank/scroll pacing (see main.c guard)

/* ── Zone-confirm gate (state_gameplay armed-latch; prompts built in ui.c) ── */
// Stepping toward a transition tile arms a latch + prints a prompt instead of zoning
// instantly; the next A rising-edge fires the stored transition. Walking away cancels.
#define CONFIRM_NONE      0u
#define CONFIRM_PIT       1u // pit / down-ladder            → TRANS_FLOOR_PIT
#define CONFIRM_BOSS_EXIT 2u // boss exit portal (dead boss) → TRANS_DUNGEON_EXIT
#define CONFIRM_UP        3u // stairs-up (spawn cell)       → TRANS_FLOOR_UP
#define CONFIRM_ENTRANCE  4u // hub cave mouth, aux = dungeon id → TRANS_FLOOR_PORT
#define CONFIRM_SEALED    5u // completed dungeon's mouth — message only, A does nothing
#define CONFIRM_TOWN      6u // hub town door, aux = town id → TRANS_FLOOR_PORT to TOWN_FLOOR_BASE+aux

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
#define TILE_CLASS_KNIGHT    1   /* B1 — legacy class glyph, no longer the gameplay hero sprite (see below) */
#define TILE_CLASS_BERSERKER 17  /* B2 — Zerker in-game */
#define TILE_CLASS_WITCH     33  /* B3  */
#define TILE_CLASS_SCOUNDREL 49  /* B4 — Scoundrel / rogue */
#define TILE_B6              81  /* B6 — unused placeholder (ladder/fence); reused as torch-frame decor on the title screen */

/* ── 2-tile-tall hero sprite (shared by all classes; one fixed grey/dark-blue/gold palette) ──
   Head (top OAM = SP_PLAYER_HEAD) + body (bottom OAM = SP_PLAYER). Body animates between a
   standing and a mid-stride frame while walking; the head swaps to a helmet graphic when a
   HEAD-slot item is worn. ROM sources are column-K rows 13–15 (sheet index > 127, so NOT in the
   boot bulk upload) — main.c boot-copies each into the now-freed B1–B4 class VRAM slots. */
#define TILE_SHEET_K13     202u /* K13 — bare head        ROM (13-1)*16+10 */
#define TILE_SHEET_K14     218u /* K14 — body standing    ROM (14-1)*16+10 */
#define TILE_SHEET_K15     234u /* K15 — body mid-stride  ROM (15-1)*16+10 */
#define TILE_SHEET_HELMET1 TILE_ITEM_5 /* helmeted head = I5 helmet item art for now; K12 (ROM 186) is
                                          still blank — repoint here when dedicated head art is drawn */

/* Witch hat (H6): item icon + worn-head art in one tile. H6's natural VRAM slot (215) is taken by
   the boot-copied ring icon, so the art borrows the unused M6 slot instead (sheet 92, never placed;
   N6/O6 next door are the ITEM label — verified in use, don't touch). Slot ≥182 and outside the
   title-logo table → a single boot upload survives (same as ring/bow/book icons). */
#define TILE_SHEET_H6          87u  /* H6 — witch hat art, ROM (6-1)*16+7 */
#define TILE_WITCH_HAT_VRAM   220u  /* borrows unused M6 VRAM slot — BG belt/floor icon + OBJ worn head */
#define TILE_WITCHHAT_BELT_OFF ((uint8_t)(TILE_WITCH_HAT_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(WITCH_HAT)
#define TILE_PLAYER_BODY_STAND_VRAM  ((uint8_t)(TILESET_VRAM_OFFSET + TILE_CLASS_KNIGHT))    /* 129 — freed B1 slot */
#define TILE_PLAYER_BODY_STRIDE_VRAM ((uint8_t)(TILESET_VRAM_OFFSET + TILE_CLASS_BERSERKER)) /* 145 — freed B2 slot */
#define TILE_PLAYER_HEAD_VRAM        ((uint8_t)(TILESET_VRAM_OFFSET + TILE_CLASS_WITCH))     /* 161 — freed B3 slot */
#define TILE_PLAYER_HELMET_VRAM      ((uint8_t)(TILESET_VRAM_OFFSET + TILE_CLASS_SCOUNDREL)) /* 177 — freed B4 slot */

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
#define TILE_LIGHT_5        66   /* C5  — dead variant, never placed; VRAM 194 = big-slime frame-1 TL scratch */
#define TILE_LIGHT_6        82   /* C6  */

/* ── D col — decorative columns ─────────────────────────────────────────── */
/* Placed when a wall tile has no orthogonal wall neighbour, or randomly.   */
#define TILE_COLUMN_1        3   /* D1  */
#define TILE_COLUMN_2       19   /* D2  */
#define TILE_COLUMN_3       35   /* D3  */
#define TILE_COLUMN_4       51   /* D4  */
#define TILE_COLUMN_5       67   /* D5  — dead variant, never placed; VRAM 195 = big-slime frame-1 TR scratch */
#define TILE_COLUMN_6       83   /* D6  */
#define TILE_COLUMN_7       99   /* D7  */

/* ── E col — ground / floor tiles ───────────────────────────────────────── */
#define TILE_GROUND_A        4   /* E1  */
#define TILE_GROUND_B       20   /* E2  */
#define TILE_GROUND_C       36   /* E3  */
#define TILE_GROUND_D       52   /* E4  — also title-menu fire particle glyph */
#define TILE_TITLE_FIRE     TILE_GROUND_D
#define TILE_GROUND_E       68   /* E5  — dead variant, never placed; VRAM 196 = big-slime frame-1 BL scratch */

/* ── F col — props ───────────────────────────────────────────────────────── */
#define TILE_F5              69u  /* F5  — dead variant, never placed; VRAM 197 borrowed by TILE_STUN_ICON_VRAM */
#define TILE_CHEST           5   /* F1  */
#define TILE_BARREL         21   /* F2  */
#define TILE_ROOF_A         37   /* F3 — town building roof variant 1 (was the never-placed mushroom) */
#define TILE_ROOF_B         53   /* F4 — town building roof variant 2. VRAM 165/181 are title-logo
                                    stomp slots, but the restore re-uploads every slot from the sheet
                                    at (VRAM-128) = exactly F3/F4 — safe with no boot copy. */
/* Overworld terrain art (hub only). Both ROM sources live past the first-128 VRAM upload, so they
   are boot-copied into title-safe VRAM slots — the title logo (title_logo.c) patches+restores
   128..181, so the wall/water slots must sit ≥182 and outside that table or they get blanked. */
#define TILE_C10                 146u /* C10 sheet source = (10-1)*16 + 2 — overworld wall (pine tree) art */
#define TILE_F10                 149u /* F10 sheet source = (10-1)*16 + 5 — overworld water (border) art */
#define TILE_OVERWORLD_WALL_OFF   85u /* borrows unused F6 VRAM slot (213); renderer adds TILESET_VRAM_OFFSET */
#define TILE_OVERWORLD_WATER_OFF  86u /* borrows unused G6 VRAM slot (214) */
#define TILE_OVERWORLD_WALL_VRAM  ((uint8_t)(TILESET_VRAM_OFFSET + TILE_OVERWORLD_WALL_OFF))  /* =213 tree */
#define TILE_OVERWORLD_WATER_VRAM ((uint8_t)(TILESET_VRAM_OFFSET + TILE_OVERWORLD_WATER_OFF)) /* =214 water */
#define OVERWORLD_BORDER_BAND      2u /* hub: outermost N tiles are always ocean (forced water margin) */

/* overworld_cell_render() region codes — caller picks the ground/blank palette by hub region. */
#define OW_REGION_GRASS  0u
#define OW_REGION_DESERT 1u
#define OW_REGION_SNOW   2u

/* Overworld prefab features: multi-tile structures placed on land at gen-time (seed-stable), drawn by
   overworld_cell_render's overlay, and (Part D) triggering a sub-map on the entrance cell. Footprint
   bodies are stamped blocking (floor_bits cleared); the entrance cell stays walkable. */
#define OW_FEAT_TOWN      0u  /* 3x3 */
#define OW_FEAT_WAYPOINT  1u  /* 2x2 */
#define OW_FEAT_ENTRANCE  2u  /* 1x1 cave/dungeon mouth */
#define OW_FEAT_BOSSDOOR  3u  /* 2x2 final-dungeon door (O15/P15/O16/P16) */
#define OW_FEAT_SIGNPOST  4u  /* 1x1 readable marker (tile B8); step on it to print its label to the chat box */
#define OW_FEAT_FOUNTAIN  5u  /* 1x1 town-interior heal fountain; step on it to restore full HP */
#define OW_FEAT_TREE      6u  /* 1x1 town-interior deco pine; the cell itself is carved WALL (blocking) */
#define OW_FEAT_COUNT     7u
#define MAX_OW_FEATURES   44u /* 17 structures (3 towns + 9 entrances + 4 waypoints + 1 boss) + a signpost beside each */
#define TILE_SHEET_B8        113u /* B8 signpost art (row 8, col B; directly below flag tile B7=97) */
#define PREFAB_VRAM_SIGNPOST 205u /* dedicated free slot (blank sheet cell N5, ≥182 so title restore won't blank it); B8 boot-copied here in main.c */

/* Signpost label code packed into OwFeature.aux: high nibble = kind, low nibble = index/direction. */
#define SIGN_KIND_TOWN     0x00u /* low = town index 0..2 */
#define SIGN_KIND_WAYPOINT 0x10u /* low = quadrant 0=NE 1=NW 2=SE 3=SW */
#define SIGN_KIND_DUNGEON  0x20u /* low = entrance index 0..8 */
#define SIGN_KIND_BOSS     0x30u /* final dungeon */
#define SIGN_KIND_NPC      0x40u /* town-interior NPC; low = villager slot (line = low & 3) */
#define SIGN_KIND_BUILDING 0x50u /* town building sign; low = type 0..7 (INN/SMITH/... in overworld_signpost_read) */

/* A waypoint must sit "within 1 screen" of the town/entrance it serves. Use a conservative co-visibility
   box (smaller than the GRID_W×GRID_H viewport) so the waypoint and its feature can share the screen. */
#define WAYPOINT_NEAR_DX  (GRID_W - 3u) /* 17 */
#define WAYPOINT_NEAR_DY  (GRID_H - 2u) /* 11 */

/* Towns must sit far apart: each region's town must be ≥ this many tiles (Manhattan, centre-to-centre)
   from every other town. ~3 screens-wide. The 96-tile map is only ~5 screens across, so this is near the
   geometric max for 3 diagonally-spread towns — placement drops the rule as a last resort (see map_gen). */
#define MIN_TOWN_SEP_TILES  (3u * GRID_W) /* 60 */

/* Town interiors (biome_town.c): building table cap — feature budget is the real limit
   (1 fountain + ≤20 signs + ≤8 NPCs + pines must stay ≤ MAX_OW_FEATURES). */
#define MAX_TOWN_BUILDINGS  20u

/* The 3 dungeon entrances of a region cluster within this many tiles of their town (entrance-cell to
   town anchor), so each town is ringed by its own dungeons. ~1 screen. */
#define DUNGEON_CLUSTER_DX  GRID_W /* 20 */
#define DUNGEON_CLUSTER_DY  GRID_H /* 13 */

/* Prefab feature art (hub only). Sheet sources (offset = (row-1)*16 + col). Like coast, these are
   uploaded by biome_load_active(BIOME_OVERWORLD) into idle hub OBJ slots and the borrowed enemy art is
   restored on dungeon/miniboss floors. Upload order must match the PREFAB_VRAM_* slots below. */
#define TILE_PREFAB_ENTRANCE_D9   131u /* D9 — dungeon/cave mouth      */
#define TILE_PREFAB_TOWN_WALL_EW  134u /* G9 — town E/W wall (swapped) */
#define TILE_PREFAB_TOWN_CORNER   133u /* F9 — town corner            */
#define TILE_PREFAB_TOWN_WALL_NS  132u /* E9 — town N/S wall (swapped) */
#define TILE_PREFAB_WP_TL         100u /* E7 — waypoint top-left  */
#define TILE_PREFAB_WP_TR         101u /* F7 — waypoint top-right */
#define TILE_PREFAB_WP_BL         116u /* E8 — waypoint bot-left  */
#define TILE_PREFAB_WP_BR         117u /* F8 — waypoint bot-right */
#define TILE_PREFAB_DOOR_TL       238u /* O15 — boss door top-left  */
#define TILE_PREFAB_DOOR_TR       239u /* P15 — boss door top-right */
#define TILE_PREFAB_DOOR_BL       254u /* O16 — boss door bot-left  */
#define TILE_PREFAB_DOOR_BR       255u /* P16 — boss door bot-right */
#define TILE_PREFAB_MTN_L         129u /* B9 — snow mountain, left half  */
#define TILE_PREFAB_MTN_R         130u /* C9 — snow mountain, right half */

/* Borrowed hub VRAM slots (all idle on the enemy-less hub):
   194/195/196/198 = C5/D5/E5/G5 dead cells (miniboss re-uploads its big-slime there; no restore needed);
   230/231 = gorgon feet (boss-only → restored in the dungeon branch, which the boss floor also takes);
   217/218 = small-slime (restored in BOTH the dungeon and miniboss branches — both draw small slimes). */
#define PREFAB_VRAM_ENTRANCE      198u
#define PREFAB_VRAM_TOWN_WALL_EW  195u
#define PREFAB_VRAM_TOWN_CORNER   194u
#define PREFAB_VRAM_TOWN_WALL_NS  196u
#define PREFAB_VRAM_WP_TL         230u
#define PREFAB_VRAM_WP_TR         231u
#define PREFAB_VRAM_WP_BL         217u
#define PREFAB_VRAM_WP_BR         218u
/* Boss door uses 3 dead sheet cells — wall variants 4/5/7 (A5/A6/A8), never placed by any map code
   (confirmed zero refs) and owned by no sprite, so like the C5/D5/E5/G5 dead cells they need no restore —
   plus the enemy death-poof slot (M7), which IS live on combat floors, so biome.c restores TILE_POOF_CLOUD
   on the first non-hub floor (else branch). (An earlier version wrongly used A7/D7/I7/J7 — live content:
   WALL_F/COLUMN_7/boots/skull.) */
#define PREFAB_VRAM_DOOR_TL       192u /* A5 = TILE_WALL_D (dead)                  */
#define PREFAB_VRAM_DOOR_TR       208u /* A6 = TILE_WALL_E (dead)                  */
#define PREFAB_VRAM_DOOR_BL       240u /* A8 = TILE_WALL_G (dead)                  */
#define PREFAB_VRAM_DOOR_BR       236u /* M7 = TILE_POOF_CLOUD (borrowed; restored off-hub) */
/* Snow-biome mountains borrow the stun/root enemy-overlay slots: those icons are only ever drawn ON
   enemies (debuff_icon.c), so they are never on screen on the enemy-less hub. biome.c restores them when
   leaving to floor 1 (else branch) — same borrow-and-restore mechanism as the gorgon/slime coast tiles. */
#define PREFAB_VRAM_MTN_L         197u /* = TILE_STUN_ICON_VRAM */
#define PREFAB_VRAM_MTN_R         242u /* = TILE_ROOT_ICON_VRAM */

/* Overworld coastline tiles — sheet cells D11..G12 (rows 11-12 are past the first-128 VRAM upload,
   so biome_load_active() boot-copies them into borrowed VRAM slots when floor 0 loads). ROM offset
   = (row-1)*16 + col, A=0..H=7. */
#define TILE_COAST_D11   163u /* NW corner */
#define TILE_COAST_E11   164u /* N edge    */
#define TILE_COAST_F11   165u /* N edge alt*/
#define TILE_COAST_G11   166u /* NE corner */
#define TILE_COAST_D12   179u /* SW corner */
#define TILE_COAST_E12   180u /* S edge    */
#define TILE_COAST_F12   181u /* S edge alt*/
#define TILE_COAST_G12   182u /* SE corner */
/* The 8 coast tiles alias enemy OBJ VRAM slots (no enemies on the hub). biome_load_active() uploads
   coast art here on floor 0 and restores the enemy sprites on every dungeon floor. Order below must
   match the upload order in biome.c. */
/* Biome-border transition tiles (grass↔snow, grass↔desert). Snow borders reuse the 8 coast tiles
   verbatim with attr = palette 0: on the hub, slot 0's idx2/3 are snow shades, so the coast art's
   blue stroke recolors white (its bulk idx0 stays the green field). Desert borders use 3 tiles
   generated at hub load — coast art with idx2/3 collapsed to idx1 (= flat sand on the hub's slot 0)
   — plus BG-attr X/Y flips for the other orientations. Slots are blank sheet cells O5/P5/P6 (no
   art, no refs — verified), dead like the boss-door slots: no off-hub restore needed. */
#define BORDER_VRAM_CORNER_NW 206u /* O5 blank — from TILE_COAST_D11, remapped */
#define BORDER_VRAM_EDGE_N    207u /* P5 blank — from TILE_COAST_E11, remapped */
#define BORDER_VRAM_EDGE_NA   223u /* P6 blank — from TILE_COAST_F11, remapped */
#define COAST_VRAM_NW  237u /* TILE_SKEL_1_VRAM        */
#define COAST_VRAM_N   238u /* TILE_SKEL_2_VRAM        */
#define COAST_VRAM_NA  239u /* TILE_RAT_VRAM (N alt)   */
#define COAST_VRAM_NE  234u /* TILE_BIG_SKELL_BODY_VRAM*/
#define COAST_VRAM_SW  225u /* TILE_GORGON_HEAD_L_VRAM */
#define COAST_VRAM_S   226u /* TILE_GORGON_HEAD_R_VRAM */
#define COAST_VRAM_SA  228u /* TILE_GORGON_BODY_L_VRAM (S alt) */
#define COAST_VRAM_SE  229u /* TILE_GORGON_BODY_R_VRAM */

/* Overworld preset: 5 seeded continent layouts. run_seed picks one per playthrough (set in
   generate_level into the overworld_preset global; render.c reads it). land_thresh scales the
   landmass radius², n_rivers/n_lakes control interior water, desert_num scales the SE sand region
   (larger = bigger desert; the lowest-water preset bumps this ~20%). */
#define OVERWORLD_PRESET_COUNT 5u
typedef struct {
    uint8_t land_thresh; /* radius² budget for land, in units of (w+h)²/256 */
    uint8_t n_rivers;
    uint8_t n_lakes;
    uint8_t desert_num;  /* desert threshold numerator over 8 (see overworld_is_desert) */
} OverworldPreset;

/* ── G col — doors + shrine states ──────────────────────────────────────── */
#define TILE_DOOR_OPEN       6   /* G1  */
#define TILE_DOOR_CLOSED    22   /* G2  */
#define TILE_SHRINE_ON_1    38   /* G3  - active shrine animation frame 1  */
#define TILE_SHRINE_ON_2    54   /* G4  - active shrine animation frame 2  */
#define TILE_SHRINE_OFF     70   /* G5  - inactive shrine; dead, never placed; VRAM 198 = big-slime frame-1 BR scratch */

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
#define TILE_SHEET_I16           248u // I16 — ring item art; ROM (16-1)*16+8, sheet >127 → boot copy
#define TILE_RING_VRAM           215u // belt/pickup UI — copied from TILE_SHEET_I16 at boot; borrows unused H6 slot (sheet 87, never placed). All 30 ring kinds share this tile; tiers differ by palette only.
#define TILE_RING_OFF ((uint8_t)(TILE_RING_VRAM - TILESET_VRAM_OFFSET)) // items_kind_tile(RING_*)
#define RING_DROP_PCT            12u  // % chance an equipment drop is instead a (random) ring

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

/* Gorgon boss sprite (2×3 tiles). Art lives in bosses.png rows 9-11 cols A-B (bank 24) —
   the _ROM values are bosses_tiles[] indices (16 cols/row), NOT tileset_tiles indices;
   biome_load_active maps bank 24 and uploads the tiles verbatim (tone fixes belong in
   the art / tools/prep_assets.py, not load-time swaps).
   Uploaded to borrowed row-7 slots (B7/C7/E7/F7/G7/H7) whose original ROM content
   is never placed as a BG tile. Head row flips horizontally for animation. */
#define TILE_GORGON_HEAD_L_ROM  128u  /* bosses A9  — head left             */
#define TILE_GORGON_HEAD_R_ROM  129u  /* bosses B9  — head right            */
#define TILE_GORGON_BODY_L_ROM  144u  /* bosses A10 — body left             */
#define TILE_GORGON_BODY_R_ROM  145u  /* bosses B10 — body right            */
#define TILE_GORGON_FEET_L_ROM  160u  /* bosses A11 — feet left (collision) */
#define TILE_GORGON_FEET_R_ROM  161u  /* bosses B11 — feet right            */
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

/* ── Sphinx boss (BIOME_BOSS2, floor 6) ──────────────────────────────────────
   10 sprite-VRAM scratch slots, all free on the sphinx floor (gorgon + skel/rat/big-skull
   never spawn here; biome_load_active's else-branch restores their art on every other floor).
   Layout = 3x2 body (B0..B5) + 2x2 wings (W0..W3, drawn on top). Pixel data is re-uploaded
   per animation frame from bosses_tiles[] (bank 24) — OAM/positions stay fixed (water-style swap). */
#define TILE_SPHINX_B0_VRAM 225u /* body top-left  (col A) — reuses gorgon head-L */
#define TILE_SPHINX_B1_VRAM 226u /* body top-mid   (col B) — gorgon head-R */
#define TILE_SPHINX_B2_VRAM 228u /* body top-right (col C) — gorgon body-L */
#define TILE_SPHINX_B3_VRAM 229u /* body bot-left  (col A) — gorgon body-R */
#define TILE_SPHINX_B4_VRAM 230u /* body bot-mid   (col B) — gorgon feet-L */
#define TILE_SPHINX_B5_VRAM 231u /* body bot-right (col C) — gorgon feet-R */
#define TILE_SPHINX_W0_VRAM 237u /* wing top-left  — reuses skeleton-1 */
#define TILE_SPHINX_W1_VRAM 238u /* wing top-right — skeleton-2 */
#define TILE_SPHINX_W2_VRAM 239u /* wing bot-left  — rat */
#define TILE_SPHINX_W3_VRAM 234u /* wing bot-right — big-skull body (blank tile during wing_up) */
/* bosses_tiles[] source indices (128x128 sheet, 16 cols/row → N = row*16 + col); see the
   body_src[]/wing_src0[] tables in biome_boss2.c: legs_up {0,1,2,16,17,18} (+32 → legs_down),
   wing_up {64,65,80,81} (+32 → wing_down; B6=81 is the blank 4th cell). */
#define PAL_SPHINX_BODY PAL_GORGON_BODY /* OCP slot 4 — gorgon's slot, free on the sphinx floor */
#define PAL_SPHINX_WING PAL_ENEMY_RAT   /* OCP slot 5 — no rats on boss floors; white/grey wing ramp */
/* Sphinx behavioral states (g_sphinx_mode): grounded chases+melees & is hittable normally;
   flying is melee-immune (ranged-only), repositions toward the player and pelts a stun-glyph bolt. */
#define SPHINX_GROUNDED     0u
#define SPHINX_FLYING       1u
#define SPHINX_PHASE_TURNS  5u   /* 5 grounded turns, then 5 flying turns, repeating */
#define SPHINX_RANGED_RANGE 5u   /* Chebyshev reach of the flying ranged bolt */

/* ── Miniboss elite quadrant scratch ────────────────────────────────────────
   The 2x elite (ENEMY_SLIME_BIG id; 2×2 tiles, animated 2-frame) spawns on every dungeon's
   miniboss floor (FLOORKIND_MINIBOSS, dungeon.h). Its 8 quadrant tiles are BUILT AT RUNTIME:
   dungeon_elite_load_art (bank 28, dungeon_floors.c) pixel-doubles the two frames of
   elite_base_type's regular sprite from the ROM tileset — no dedicated PNG (the old
   res/enemies_miniboss.png / bank-27 sheet is retired). It occupies 2 logical map tiles
   (like Gorgon) so both are attackable — see ENEMY_GORGON checks in enemy.c/combat.c/map.c,
   mirrored for ENEMY_SLIME_BIG.

   VRAM slots reused (proven safe; no permanent baking):
   • Frame 1 (TL/TR/BL/BR) → 4 dead background cells C5/D5/E5/G5 (TILE_LIGHT_5/COLUMN_5/
     GROUND_E/SHRINE_OFF — never placed by any map code, confirmed zero refs). Dead cells
     have no owner, so they need no restore on leaving the floor.
   • Frame 2 (TL/TR/BL/BR) → the Gorgon slots (TILE_GORGON_HEAD_L/R_VRAM,
     TILE_GORGON_BODY_L/R_VRAM = 225/226/228/229). The Gorgon never spawns on a miniboss
     floor, and biome_load_active()'s else-branch restores those slots from ROM on every
     dungeon/crypt/cavern load — which runs BEFORE the elite overlay (ordering matters).
     Skeleton/Rat/BigSkell slots (the old frame-2 borrow) are live fodder art now.
   The renderer (entity_sprites.c) toggles frame 1 (the _OFF constants below) vs frame 2
   (TILE_GORGON_HEAD_L/R_OFF, TILE_GORGON_BODY_L/R_OFF) on enemy_anim_toggle.
   NOTE: an earlier version borrowed A7/D7/I7/J7, which turned out to be live content
   (TILE_WALL_F, TILE_COLUMN_7, TILE_ITEM_7, TILE_LOADING_SKULL/TILE_BIG_SKELL_HEAD) — do not
   reuse those for anything. */
#define TILE_SLIMEBIG_TL_OFF   66u  /* frame-1 quadrant offsets; VRAM = TILESET_VRAM_OFFSET + off */
#define TILE_SLIMEBIG_TR_OFF   67u  /* (194/195/196/198 = C5/D5/E5/G5 dead cells) */
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
#define TILE_SHEET_M13         204u   /* M13 - stun indicator glyph; ROM index (13-1)*16+12; art left blank for now, copied to VRAM at boot */
#define TILE_STUN_ICON_VRAM    197u   /* borrows unused F5 VRAM slot (ROM tile 69 = TILE_F5, dead prop-column variant, not placed by any map code) */
#define TILE_ZERKER_WHIRLWIND_VRAM 247u // copied from TILE_ITEM_10 (I10) at boot for Zerker Whirlwind belt icon
#define TILE_PLAYER_AURA_VRAM_A 248u // below CLASS_EMBLEM_VRAM_START 252 — gameplay aura only
#define TILE_PLAYER_AURA_VRAM_B 249u
#define TILE_WITCH_BOLT_VRAM 250u // copied from TILE_SHEET_M12 at boot for UI icon + projectile sprite
#define TILE_KNIGHT_SHIELD_VRAM 251u // copied from TILE_ITEM_9 (I9) at boot for knight shield UI icon + corner buff sprite
#define TILE_SHEET_M14     220u  /* M14 — empty belt slot (ROM); (14-1)*16+12, past VRAM 0..127 pack */

/* Overworld town flag (hub only) — 2-frame animated OBJ in the town courtyard center. Art lives at
   sheet cells B7/C7 (ROM 97/98, otherwise blank — the gorgon-head borrow uses VRAM 225/226 but gets
   its art from a different ROM cell). Boot-copied into permanently-free OBJ slots 253/254. */
#define TILE_SHEET_FLAG_1  97u   /* B7 — flag anim frame 1 */
#define TILE_SHEET_FLAG_2  98u   /* C7 — flag anim frame 2 */
#define TILE_FLAG_F1_VRAM 253u   /* free OBJ slot — boot-copied from B7 */
#define TILE_FLAG_F2_VRAM 254u   /* free OBJ slot — boot-copied from C7 */

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
#define PAL_ENEMY_RAT       5 // red–rose; OCP5 (BKG5 = life/UI text — separate CRAM)
#define PAL_ENEMY_GOBLIN    6 // magenta–pink; OCP6 (BKG6 now free for biome palettes)
#define PAL_ENEMY_BAT       7 // aqua–turquoise; OCP7 (shared with PAL_XP_UI sprite gold ramp)
#define PAL_LIFE_UI 5   // red on black; index 3 = white — life bar fill + all white HUD/UI text
#define PAL_UI      PAL_LIFE_UI // white HUD text rides the heart palette (index 3 = white). BKG-only
#define PAL_XP_UI   7   // OCP7 gold ramp: sprite gold (belt selector, aura, arrows) + story screen only
#define PAL_XP_UI_BG PAL_LADDER // BKG gold rides the static fire/ladder ramp (slot 4, index 3 ≈ gold)
#define PAL_CORPSE  0   // corpse 'x' uses default grayscale ramp
/* BKG slots 6 & 7 are now free for biome palettes; OCP 6/7 still hold goblin/bat (separate CRAM). */
#define PAL_OW_FOLIAGE 6 // overworld-only BKG: dedicated tree/foliage ramp (slot freed from UI)
#define PAL_OW_ACCENT  7 // overworld-only BKG: spare ramp for new hub deco (slot freed from UI)
#define PAL_ITEM_GOLD_BG PAL_OW_FOLIAGE // dungeon ground-item gold (true orange-gold ramp); shares slot 6
                                        // with hub foliage — safe, the hub has no ground items

/* ── Player/floor stats: globals.h ────────────────────────────────────────── */
extern uint16_t camera_px;    // pixel x of viewport top-left (defined in camera.c)
extern uint16_t camera_py;    // pixel y of viewport top-left (defined in camera.c)
#define CAM_TX (camera_px >> 3)
#define CAM_TY (camera_py >> 3)
#define RING_BKG_VY_WORLD(my) ((uint8_t)((my) & 31u)) // ring Y for world row my — SCY applies from line 0 in gameplay

/* ── Tileset pixel data ──────────────────────────────────────────────────── */
extern const uint8_t tileset_tiles[];

#endif // DEFS_H

