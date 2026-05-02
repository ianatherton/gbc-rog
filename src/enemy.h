#ifndef ENEMY_H
#define ENEMY_H

#include "defs.h"
#include <gbdk/platform.h>

typedef struct {
    uint8_t tile;         // sheet offset (added to TILESET_VRAM_OFFSET in renderer)
    uint8_t tile_alt;     // second idle frame; may equal tile for static sprite
    uint8_t max_hp;       // full health when spawned
    uint8_t damage;       // subtracted from player_hp on collision
    uint8_t palette;      // CGB attribute palette 0..7
    uint8_t move_style;   // MOVE_CHASE / MOVE_RANDOM / MOVE_WANDER / MOVE_BLINK
    uint8_t param;        // per-style tunable (e.g. MOVE_BLINK Chebyshev jump cap); unused styles set 0
} EnemyDef;

extern EnemyDef enemy_defs[NUM_ENEMY_TYPES]; // HOME-resident; biome_load_active fills from bank 10/11/12
extern uint8_t  enemy_defs_count;            // <= NUM_ENEMY_TYPES; spawn picks rand()%enemy_defs_count

/* ── Per-enemy instance state ────────────────────────────────────────────── */
extern uint8_t enemy_x[MAX_ENEMIES]; // last tile column while slot used; undefined if !enemy_alive[i]
extern uint8_t enemy_y[MAX_ENEMIES];
extern uint8_t enemy_type[MAX_ENEMIES];
extern uint8_t enemy_hp[MAX_ENEMIES];
extern uint8_t num_enemies;

/* ── Spatial occupancy ────────────────────────────────────────────────────── */
extern uint8_t enemy_occ[BITSET_BYTES];  // 1 = enemy present at this tile

/* ── Corpse state ────────────────────────────────────────────────────────── */
extern uint8_t corpse_x[MAX_CORPSES];
extern uint8_t corpse_y[MAX_CORPSES];
extern uint8_t corpse_tile[MAX_CORPSES]; // TILE_FLOOR_DECO_1..5 sheet offset (picked at kill)
extern uint8_t num_corpses;

/* ── Animation state ─────────────────────────────────────────────────────── */
extern uint8_t enemy_anim_toggle;
extern uint8_t enemy_attack_slots[MAX_ENEMIES]; // slots that struck player this phase (prefix of length enemy_attack_count)
extern uint8_t enemy_attack_count;
extern uint8_t enemy_force_active[MAX_ENEMIES]; // 1 = AI runs even when unrevealed/offscreen (boss hook)

// Copies the short name into caller-supplied buffer (NUL-terminated, capped at cap-1 chars).
// Use the copy variant from ANY non-bank-2 caller — a returned pointer would land in a
// non-mapped ROM range after the bcall trampoline restores the caller's bank.
void enemy_type_short_name_copy(uint8_t t, char *out, uint8_t cap) BANKED;
uint8_t enemy_effective_max_hp(uint8_t type) BANKED;  // base max_hp scaled by floor_num (cap 255)
uint8_t enemy_effective_damage(uint8_t type) BANKED;  // base damage scaled by floor_num (cap 255)

void    enemy_grids_init(void); // clear enemy_grid + corpse_grid (call on level load)
void    enemy_place_slot(uint8_t slot, uint8_t x, uint8_t y); // sync occupancy structures after spawn or move
void    enemy_clear_slot(uint8_t x, uint8_t y) BANKED; // clear occupancy structures before death or move
void    corpse_place_slot(uint8_t slot, uint8_t x, uint8_t y) BANKED; // sync corpse hash after a kill
void    corpse_clear_slot(uint8_t x, uint8_t y); // sync corpse hash when tile is reclaimed
void    enemy_anim_reset(void); // reset DIV accumulator when entering a floor
uint8_t enemy_anim_update(void); // 1 if toggled animation frame this call
uint8_t enemy_at(uint8_t x, uint8_t y) BANKED; // enemy slot occupying tile, else ENEMY_DEAD
uint8_t corpse_at(uint8_t x, uint8_t y); // nonzero if a corpse marker sits here
uint8_t corpse_sheet_at(uint8_t x, uint8_t y); // TILE_FLOOR_DECO_* offset or 255 if none
uint8_t corpse_deco_random(void) BANKED;          // random L1–L5 sheet offset for new corpse
void    spawn_enemies(void); // fill world with NUM_ENEMIES instances
uint8_t move_enemies(uint8_t px, uint8_t py); // enemy turn: moves + records strikes in enemy_attack_* (no HP yet); 0 none, 1 pending hits
void    enemy_resolve_hit(uint8_t slot) BANKED; // combat log + apply that slot's damage (call before its lunge)
void    enemy_set_force_active(uint8_t slot, uint8_t on); // per-enemy override for always-active AI (future boss behavior)
uint8_t enemy_get_force_active(uint8_t slot); // query override state (0 or 1)

#endif // ENEMY_H

