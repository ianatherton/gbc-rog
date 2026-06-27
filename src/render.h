#ifndef RENDER_H
#define RENDER_H

#include "defs.h"

void load_palettes(void) BANKED;   // upload 8 CGB background palette slots (wall slot from table[0])
void render_sprite_palette_player_default(void) NONBANKED; // OCP PAL_PLAYER — class ramp from class_palettes (after hurt flash)
void render_sprite_palette_player_hurt(void) NONBANKED;    // OCP PAL_PLAYER — saturated red tint
void apply_wall_palette(void); // PAL_WALL_BG + PAL_PILLAR_BG from wall_palette_index / pillar_palette_index
void apply_field_palette(void); // slot 0 (blank field) + PAL_FLOOR_BG per biome — restore after a menu blanks slot 0
void draw_screen(uint8_t px, uint8_t py); // full BG redraw + sprite refresh
void draw_gameplay_overlays(uint8_t px, uint8_t py); // WIN/HUD + sprites only — skip BKG dungeon ring when unchanged
void draw_gameplay_overlays_profiled(uint8_t px, uint8_t py); // overlay-only metric path; excludes draw_screen()
void draw_gameplay_overlays_profiled_far(uint8_t px, uint8_t py) BANKED; // cross-bank shim (combat.c); bank-2 callers use the near version
void draw_cell(uint8_t mx, uint8_t my); // single map cell if visible (terrain only)
void draw_col_strip(uint8_t mx); // one world column for horizontal scroll
void draw_row_strip(uint8_t my); // one world row for vertical scroll

// Strip blit scratch + helpers. render.c (bank 2) fills the buffers via classify_cell, then calls one
// of these to bulk-blit a column/row, splitting at the 32-tile ring wrap. The helpers live in bank 22
// (biome_overworld.c) — they use only HOME gbdk calls + these RAM buffers, so relocating them off the
// near-full render bank 2 costs just one trampoline per strip (not per cell).
extern uint8_t render_strip_tiles[GRID_W + 1];
extern uint8_t render_strip_attrs[GRID_W + 1];
BANKREF_EXTERN(render_blit_strip_col)
BANKREF_EXTERN(render_blit_strip_row)
void render_blit_strip_col(uint8_t vx, uint8_t vy0, uint8_t n) BANKED;
void render_blit_strip_row(uint8_t vy, uint8_t vx0, uint8_t n) BANKED;
void draw_enemy_cells(uint8_t px, uint8_t py); // idle enemy glyph flip: OAM only (no BG/WIN redraw)
void draw_corpse_cells(void); // redraw BG tiles for corpses and dropped items after non-melee kills
void draw_corpse_cells_far(void) BANKED; // cross-bank shim (combat.c)
void draw_boss_reveal_cells_far(void) BANKED; // reveal stairs + pit on Gorgon kill (combat.c)
void draw_ui_rows(void); // window text panel + bottom HUD after camera moves (delegates to ui.c)

#endif // RENDER_H
