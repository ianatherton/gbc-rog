#ifndef ENEMY_H
#define ENEMY_H

#include "defs.h"

/* ── Enemy definition struct ────────────────────────────────────────────── */
typedef struct {
    char    glyph;        // character drawn normally
    char    glyph_alt;    // character on alternate animation frame
    uint8_t max_hp;       // starting hit points
    uint8_t damage;       // HP the player loses on a hit
    uint8_t palette;      // CGB palette index (0-7)
    uint8_t move_style;   // MOVE_CHASE / MOVE_RANDOM / MOVE_WANDER
} EnemyDef;

extern const EnemyDef enemy_defs[NUM_ENEMY_TYPES];

/* ── Per-enemy instance state ────────────────────────────────────────────── */
extern uint8_t enemy_x[MAX_ENEMIES];
extern uint8_t enemy_y[MAX_ENEMIES];
extern uint8_t enemy_type[MAX_ENEMIES];
extern uint8_t enemy_hp[MAX_ENEMIES];
extern uint8_t num_enemies;

/* ── Corpse state ────────────────────────────────────────────────────────── */
extern uint8_t corpse_x[MAX_CORPSES];
extern uint8_t corpse_y[MAX_CORPSES];
extern uint8_t num_corpses;

/* ── Animation state ─────────────────────────────────────────────────────── */
extern uint8_t enemy_anim_counter;
extern uint8_t enemy_anim_toggle;

/* ── Functions ───────────────────────────────────────────────────────────── */
uint8_t enemy_at(uint8_t x, uint8_t y);    // slot index at (x,y), or ENEMY_DEAD
uint8_t corpse_at(uint8_t x, uint8_t y);   // 1 if corpse at (x,y), else 0
void    spawn_enemies(void);               // place NUM_ENEMIES on valid floor tiles
uint8_t move_enemies(uint8_t px, uint8_t py); // move all enemies; 0=ok 1=hit 2=dead

#endif // ENEMY_H

