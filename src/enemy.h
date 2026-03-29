#ifndef ENEMY_H
#define ENEMY_H

#include "defs.h"

typedef struct {
    uint8_t tile;         // sheet offset (added to TILESET_VRAM_OFFSET in renderer)
    uint8_t tile_alt;     // second idle frame; may equal tile for static sprite
    uint8_t max_hp;       // full health when spawned
    uint8_t damage;       // subtracted from player_hp on collision
    uint8_t palette;      // CGB attribute palette 0..7
    uint8_t move_style;   // MOVE_CHASE / MOVE_RANDOM / MOVE_WANDER
} EnemyDef;

extern const EnemyDef enemy_defs[NUM_ENEMY_TYPES];

/* ── Per-enemy instance state ────────────────────────────────────────────── */
extern uint8_t enemy_x[MAX_ENEMIES];
extern uint8_t enemy_y[MAX_ENEMIES];
extern uint8_t enemy_type[MAX_ENEMIES];
extern uint8_t enemy_hp[MAX_ENEMIES];
extern uint8_t num_enemies;

/* ── Spatial bitsets for O(1) presence checks (replaces linear scans) ────── */
extern uint8_t enemy_occ[BITSET_BYTES];  // 1 = enemy present at this tile
extern uint8_t corpse_occ[BITSET_BYTES]; // 1 = corpse present at this tile

/* ── Corpse state ────────────────────────────────────────────────────────── */
extern uint8_t corpse_x[MAX_CORPSES];
extern uint8_t corpse_y[MAX_CORPSES];
extern uint8_t corpse_tile[MAX_CORPSES]; // TILE_FLOOR_DECO_1..5 sheet offset (picked at kill)
extern uint8_t num_corpses;

/* ── Animation state ─────────────────────────────────────────────────────── */
extern uint8_t enemy_anim_toggle;
extern uint8_t enemy_attack_slots[MAX_ENEMIES]; // slots that struck player this phase (prefix of length enemy_attack_count)
extern uint8_t enemy_attack_count;

void    enemy_grids_init(void); // clear enemy_grid + corpse_grid (call on level load)
void    enemy_anim_reset(void); // reset DIV accumulator when entering a floor
uint8_t enemy_anim_update(void); // 1 if toggled animation frame this call
uint8_t enemy_at(uint8_t x, uint8_t y); // enemy slot occupying tile, else ENEMY_DEAD
uint8_t corpse_at(uint8_t x, uint8_t y); // nonzero if a corpse marker sits here
uint8_t corpse_sheet_at(uint8_t x, uint8_t y); // TILE_FLOOR_DECO_* offset or 255 if none
uint8_t corpse_deco_random(void);                // random L1–L5 sheet offset for new corpse
void    spawn_enemies(void); // fill world with NUM_ENEMIES instances
uint8_t move_enemies(uint8_t px, uint8_t py); // enemy turn: moves + records strikes in enemy_attack_* (no HP yet); 0 none, 1 pending hits
void    enemy_resolve_hit(uint8_t slot);      // combat log + apply that slot's damage (call before its lunge)

#endif // ENEMY_H

