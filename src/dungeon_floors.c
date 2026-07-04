#pragma bank 28

// Miniboss elite art: a 2x nearest-neighbor upscale of the elite's base fodder sprite,
// built at floor-gen time from the ROM tileset — no dedicated PNG. Each 8x8 source frame
// becomes 4 quadrant tiles (TL/TR/BL/BR). Two frames upload to:
//   frame 1 -> VRAM 194/195/196/198 (C5/D5/E5/G5 dead cells, no owner, no restore needed)
//   frame 2 -> VRAM 225/226/228/229 (the Gorgon slots — free here: the Gorgon never spawns
//              on a miniboss floor, and biome_load_active's else-branch restores them from
//              ROM on every dungeon/crypt/cavern load before this runs)
// The old frame-2 borrow (Skeleton/Rat/BigSkell slots 237/238/239/234) is gone on purpose:
// those types are live fodder on miniboss floors now that the elite lives inside the
// normal biomes. entity_sprites.c's ENEMY_SLIME_BIG render reads the matching offsets.

#include "biome.h"
#include "defs.h"
#include "dungeon.h"
#include "globals.h"
#include "tileset_io.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

// enemy type -> ROM sheet tiles (frame 1, frame 2). Flip-anim types repeat one tile.
typedef struct { uint8_t type, rom1, rom2; } EliteSrc;
static const EliteSrc elite_srcs[] = {
    { ENEMY_SNAKE,    TILE_SNAKE_1,     TILE_SNAKE_2     }, // 73/74 — OFF == ROM index (<128)
    { ENEMY_SLIME,    TILE_SLIME_ROM_1, TILE_SLIME_ROM_2 }, // 169/170
    { ENEMY_RAT,      TILE_RAT_ROM,     TILE_RAT_ROM     }, // 249 flip-anim
    { ENEMY_BAT,      TILE_BAT_1,       TILE_BAT_2       }, // 25/26
    { ENEMY_IMP,      TILE_MONSTER_2,   TILE_MONSTER_2   }, // 57 flip-anim
    { ENEMY_SKELETON, TILE_SKEL_ROM_1,  TILE_SKEL_ROM_2  }, // 153/154
};
#define ELITE_SRC_COUNT (sizeof(elite_srcs) / sizeof(elite_srcs[0]))

static const uint8_t dst_f1[4] = {
    (uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_TL_OFF), // 194
    (uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_TR_OFF), // 195
    (uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_BL_OFF), // 196
    (uint8_t)(TILESET_VRAM_OFFSET + TILE_SLIMEBIG_BR_OFF), // 198
};
static const uint8_t dst_f2[4] = {
    TILE_GORGON_HEAD_L_VRAM, // 225
    TILE_GORGON_HEAD_R_VRAM, // 226
    TILE_GORGON_BODY_L_VRAM, // 228
    TILE_GORGON_BODY_R_VRAM, // 229
};

// nibble -> byte with every bit doubled: b3b2b1b0 -> b3b3b2b2b1b1b0b0 (per 2bpp bitplane)
static const uint8_t dbl_lut[16] = {
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF,
};

// Build one quadrant (16 bytes) of the 2x upscale. qx: 0 = left (high nibble), 1 = right.
// qy: 0 = top (source rows 0-3), 1 = bottom (rows 4-7). Each source row is written twice.
static void dbl_quadrant(const uint8_t *src, uint8_t *out, uint8_t qx, uint8_t qy) {
    uint8_t r, o = 0u;
    for (r = (uint8_t)(qy * 4u); r < (uint8_t)(qy * 4u + 4u); r++) {
        uint8_t p0 = src[(uint8_t)(r * 2u)], p1 = src[(uint8_t)(r * 2u + 1u)];
        uint8_t b0 = qx ? dbl_lut[p0 & 0x0Fu] : dbl_lut[p0 >> 4];
        uint8_t b1 = qx ? dbl_lut[p1 & 0x0Fu] : dbl_lut[p1 >> 4];
        out[o++] = b0; out[o++] = b1; // row doubled vertically:
        out[o++] = b0; out[o++] = b1;
    }
}

static void dbl_upload_frame(uint8_t rom_tile, const uint8_t *dst4) {
    uint8_t src[16], quad[16], q;
    tileset_read_tiles(src, rom_tile, 1u); // HOME helper — pages the tileset bank and restores ours
    for (q = 0u; q < 4u; q++) {
        dbl_quadrant(src, quad, (uint8_t)(q & 1u), (uint8_t)(q >> 1));
        set_sprite_data(dst4[q], 1u, quad);
    }
}

BANKREF(dungeon_elite_load_art)
void dungeon_elite_load_art(void) BANKED { // called by biome_apply_floor_kind on FLOORKIND_MINIBOSS
    uint8_t i;
    for (i = 0u; i < (uint8_t)ELITE_SRC_COUNT; i++) {
        if (elite_srcs[i].type != elite_base_type) continue;
        dbl_upload_frame(elite_srcs[i].rom1, dst_f1);
        dbl_upload_frame(elite_srcs[i].rom2, dst_f2);
        return;
    }
    // Unknown base (shouldn't happen): fall back to doubling the small slime.
    dbl_upload_frame(TILE_SLIME_ROM_1, dst_f1);
    dbl_upload_frame(TILE_SLIME_ROM_2, dst_f2);
}
