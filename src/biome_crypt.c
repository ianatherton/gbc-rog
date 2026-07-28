#pragma bank 11

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "tileset_io.h" // tileset_read_tiles — HOME shim; this bank can't SWITCH_ROM to the tileset itself
#include <gb/gb.h>      // set_sprite_data
#include <string.h>

// Crypt roster — undead-leaning subset; expand with crypt-only enemy IDs in a follow-up.
static const EnemyDef defs[] = {
    /* ENEMY_BIG_SKELL */ { TILE_BIG_SKELL_BODY, TILE_BIG_SKELL_BODY, 24, 5, PAL_ENEMY_SKELETON, MOVE_CHASE, 0 }, // J8 body; head drawn separately at J7
    /* ENEMY_IMP       */ { TILE_MONSTER_2,  TILE_MONSTER_2,   8, 4, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 }, // J4 flip-anim
    /* ENEMY_RAT       */ { TILE_RAT_OFF,    TILE_RAT_OFF,     4, 3, PAL_ENEMY_RAT,      MOVE_WANDER, 0 }, // J16 flip-anim
    /* ENEMY_SKELETON  */ { TILE_SKEL_1_OFF, TILE_SKEL_2_OFF, 12, 4, 0,                  MOVE_CHASE,  0 }, // J10/K10 anim; OCP0 white/grey ramp
    // Glass cannon: hits as hard as the big skeleton but folds fast — the counterweight to being
    // untouchable at range. tile == tile_alt gets the free flip-anim. Palette 0 = pal_default,
    // whose index 3 is white, so the ghost's ink renders white and its eye cut-outs stay
    // transparent. param = % chance per turn to vanish (see MOVE_PHASE / enemy_ghost_step).
    /* ENEMY_GHOST     */ { TILE_GHOST_OFF,  TILE_GHOST_OFF,  10, 5, 0,                  MOVE_PHASE, 30 }, // J15 flip-anim
};

BANKREF(biome_crypt_copy_defs)
void biome_crypt_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) { // plain: biome.c maps this bank before calling
    // Ghost art (J15) is uploaded here, per crypt floor, rather than boot-copied in main.c. Two
    // reasons: it keeps the upload off bank 0's budget entirely, and slot 191 is the last cell of
    // char create's 176..191 emblem scratch — safe only for art re-uploaded after char create
    // (same rule as the hero's 3rd walk frame on 186). This bank can't SWITCH_ROM to the tileset
    // without unmapping itself, hence the HOME shim rather than a direct tileset_tiles read.
    { uint8_t buf[16];
      tileset_read_tiles(buf, TILE_GHOST_ROM, 1u); // HOME helper — pages bank 1, restores ours
      set_sprite_data(TILE_GHOST_VRAM, 1u, buf); }
    out[ENEMY_BIG_SKELL] = defs[0];
    out[ENEMY_IMP]       = defs[1];
    out[ENEMY_RAT]       = defs[2];
    out[ENEMY_SKELETON]  = defs[3];
    out[ENEMY_GHOST]     = defs[4];
    out_active[0] = ENEMY_BIG_SKELL;
    out_active[1] = ENEMY_IMP;
    out_active[2] = ENEMY_RAT;
    out_active[3] = ENEMY_SKELETON;
    out_active[4] = ENEMY_GHOST;
    *out_count = 5;
}
