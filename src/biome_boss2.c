#pragma bank 24

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <gb/gb.h>
#include <gb/cgb.h>

// Sphinx art (same bank 24) — read directly; bank is mapped whenever these functions run.
extern const uint8_t bosses_tiles[]; // 24 tiles, 3 cols/row: body legs_up(0-5)/down(6-11), wings(12-16,18-22)

static const EnemyDef defs[] = {
    /* ENEMY_SPHINX */ { (uint8_t)(TILE_SPHINX_B0_VRAM - TILESET_VRAM_OFFSET),
                         (uint8_t)(TILE_SPHINX_B0_VRAM - TILESET_VRAM_OFFSET), // custom render branch; tile/_alt unused
                         50, 5, PAL_SPHINX_BODY, MOVE_BLINK, 3 },
};

static const palette_color_t pal_sphinx_body[] = {
    RGB(0,  0,  0),   // outline / shadow
    RGB(20, 16,  7),  // deep sandstone
    RGB(28, 24, 14),  // tan
    RGB(31, 30, 22),  // pale highlight
};

BANKREF(biome_boss2_load_palettes)
void biome_boss2_load_palettes(void) {
    set_sprite_palette(PAL_SPHINX_BODY, 1u, pal_sphinx_body);
}

BANKREF(biome_boss2_copy_defs)
void biome_boss2_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    out[ENEMY_SPHINX] = defs[0];
    out_active[0] = ENEMY_SPHINX; // roster: only the Sphinx (no summons)
    *out_count = 1u;
}

// ── Animation: re-upload the current frame's tile pixels into the 10 fixed scratch slots ─────────
// OAM layout in entity_sprites is constant, so the wingbeat / leg cycle is a pure VRAM pixel swap.
static const uint8_t body_dst[6] = { TILE_SPHINX_B0_VRAM, TILE_SPHINX_B1_VRAM, TILE_SPHINX_B2_VRAM,
                                     TILE_SPHINX_B3_VRAM, TILE_SPHINX_B4_VRAM, TILE_SPHINX_B5_VRAM };
static const uint8_t wing_dst[4] = { TILE_SPHINX_W0_VRAM, TILE_SPHINX_W1_VRAM,
                                     TILE_SPHINX_W2_VRAM, TILE_SPHINX_W3_VRAM };
static const uint8_t wing_src0[4] = { 12u, 13u, 15u, 16u }; // wing_up: A5,B5,A6,B6(blank); +6 → wing_down A7,B7,A8,B8

static void upload_body(uint8_t frame) { // frame 0 = legs_up (tiles 0-5), 1 = legs_down (6-11) — contiguous
    uint8_t i, base = (uint8_t)(frame * 6u);
    for (i = 0u; i < 6u; i++) set_sprite_data(body_dst[i], 1u, bosses_tiles + (uint16_t)(base + i) * 16u);
}
static void upload_wing(uint8_t frame) { // frame 0 = wing_up, 1 = wing_down (+6 per source tile)
    uint8_t i, add = (uint8_t)(frame * 6u);
    for (i = 0u; i < 4u; i++) set_sprite_data(wing_dst[i], 1u, bosses_tiles + (uint16_t)(wing_src0[i] + add) * 16u);
}

#define SPHINX_BODY_DIV_TICKS 1800u // ~0.11s slow leg cycle (DIV_REG @16384Hz)
#define SPHINX_WING_DIV_TICKS  650u // ~0.04s faster wingbeat
static uint8_t  s_div_last;
static uint16_t s_bacc, s_wacc;
static uint8_t  s_body_frame, s_wing_frame;

BANKREF(sphinx_load_initial)
void sphinx_load_initial(void) BANKED { // floor entry: sync timers + upload frame 0
    s_div_last = DIV_REG; s_bacc = 0u; s_wacc = 0u; s_body_frame = 0u; s_wing_frame = 0u;
    upload_body(0u);
    upload_wing(0u);
}

BANKREF(sphinx_anim_tick)
void sphinx_anim_tick(void) BANKED { // per gameplay frame on the boss floor; caller is in VBlank
    uint8_t d = DIV_REG;
    uint8_t delta = (uint8_t)(d - s_div_last);
    s_div_last = d;
    s_bacc += delta;
    s_wacc += delta;
    if (s_bacc >= SPHINX_BODY_DIV_TICKS) { s_bacc -= SPHINX_BODY_DIV_TICKS; s_body_frame ^= 1u; upload_body(s_body_frame); }
    if (s_wacc >= SPHINX_WING_DIV_TICKS) { s_wacc -= SPHINX_WING_DIV_TICKS; s_wing_frame ^= 1u; upload_wing(s_wing_frame); }
}
