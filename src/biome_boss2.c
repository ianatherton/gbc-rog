#pragma bank 24

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h" // g_sphinx_mode
#include <gb/gb.h>
#include <gb/cgb.h>

// Sphinx art (same bank 24) — read directly; bank is mapped whenever these functions run.
extern const uint8_t bosses_tiles[]; // 128x128 sheet, 16 cols/row (index = row*16+col); sphinx in cols a-c:
                                     // body legs_up rows 1-2 / legs_down rows 3-4, wings rows 5-8 (cols a-b)

static const EnemyDef defs[] = {
    /* ENEMY_SPHINX */ { (uint8_t)(TILE_SPHINX_B0_VRAM - TILESET_VRAM_OFFSET),
                         (uint8_t)(TILE_SPHINX_B0_VRAM - TILESET_VRAM_OFFSET), // custom render branch; tile/_alt unused
                         50, 5, PAL_SPHINX_BODY, MOVE_BLINK, 3 },
};

// Colors map 1:1 onto the sheet's 2bpp channels (no load-time swaps) — if a shade lands in
// the wrong channel, fix the art or tools/prep_assets.py, not this table.
static const palette_color_t pal_sphinx_body[] = {
    RGB(0,  0,  0),   // idx0 transparent for OBJ — unused
    RGB(20, 16,  7),  // deep sandstone
    RGB(28, 24, 14),  // tan
    RGB(31, 30, 22),  // pale highlight
};

static const palette_color_t pal_sphinx_wing[] = { // white/grey ramp — wings only (OCP5, rat's slot)
    RGB(0,  0,  0),   // idx0 transparent for OBJ — unused
    RGB(26, 26, 28),  // pale feather
    RGB(13, 13, 16),  // grey shading
    RGB(31, 31, 31),  // white highlight
};

// Safe to claim OCP4/OCP5 here: no gorgon/skeleton/rat spawns on the sphinx floor, and biome
// palettes load AFTER render_palettes' load_palettes() on floor entry.
BANKREF(biome_boss2_load_palettes)
void biome_boss2_load_palettes(void) {
    set_sprite_palette(PAL_SPHINX_BODY, 1u, pal_sphinx_body);
    set_sprite_palette(PAL_SPHINX_WING, 1u, pal_sphinx_wing);
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
static const uint8_t body_src[6]  = { 0u, 1u, 2u, 16u, 17u, 18u }; // legs_up A1,B1,C1,A2,B2,C2; +32 → legs_down rows 3-4
static const uint8_t wing_src0[4] = { 64u, 65u, 80u, 81u }; // wing_up: A5,B5,A6,B6(blank); +32 → wing_down A7,B7,A8,B8

static void upload_body(uint8_t frame) { // frame 0 = legs_up, 1 = legs_down (+32 per source tile = 2 rows down)
    uint8_t i, add = (uint8_t)(frame * 32u);
    for (i = 0u; i < 6u; i++) set_sprite_data(body_dst[i], 1u, bosses_tiles + (uint16_t)(body_src[i] + add) * 16u);
}
static void upload_wing(uint8_t frame) { // frame 0 = wing_up, 1 = wing_down (+32 per source tile)
    uint8_t i, add = (uint8_t)(frame * 32u);
    for (i = 0u; i < 4u; i++) set_sprite_data(wing_dst[i], 1u, bosses_tiles + (uint16_t)(wing_src0[i] + add) * 16u);
}

#define SPHINX_BODY_DIV_TICKS 1800u // ~0.11s slow leg cycle (DIV_REG @16384Hz)
#define SPHINX_WING_DIV_TICKS  650u // ~0.04s faster wingbeat
static uint8_t  s_div_last;
static uint16_t s_bacc, s_wacc;
static uint8_t  s_body_frame, s_wing_frame;
static uint8_t  s_phase_ctr; // counts sphinx turns to drive the 5-grounded / 5-flying cadence

BANKREF(sphinx_load_initial)
void sphinx_load_initial(void) BANKED { // floor entry: start grounded (grounded body + wings-down pose)
    s_div_last = DIV_REG; s_bacc = 0u; s_wacc = 0u; s_body_frame = 0u; s_wing_frame = 1u;
    g_sphinx_mode = SPHINX_GROUNDED; sphinx_fire_pending = 0u; s_phase_ctr = 0u;
    upload_body(0u); // grounded body frame
    upload_wing(1u); // wings-down (resting) pose
}

BANKREF(sphinx_ai_decide)
uint8_t sphinx_ai_decide(uint8_t sx, uint8_t sy, uint8_t px, uint8_t py) BANKED {
    if (++s_phase_ctr >= SPHINX_PHASE_TURNS) {          // flip grounded<->flying every 5 turns
        s_phase_ctr = 0u;
        g_sphinx_mode = (g_sphinx_mode == SPHINX_GROUNDED) ? SPHINX_FLYING : SPHINX_GROUNDED;
    }
    if (g_sphinx_mode != SPHINX_FLYING) return SPHINX_ACT_GROUNDED;
    // Flying: reposition every turn (the caller blinks it toward the player but suppresses melee), so
    // it stays on-screen and engaged — no vanish state, the boss is always visible while airborne.
    // Fire the ranged bolt when the player is within reach of the current tile.
    { uint8_t apx = (px > sx) ? (uint8_t)(px - sx) : (uint8_t)(sx - px);
      uint8_t apy = (py > sy) ? (uint8_t)(py - sy) : (uint8_t)(sy - py);
      if (((apx > apy) ? apx : apy) <= SPHINX_RANGED_RANGE) sphinx_fire_pending = 1u; }
    return SPHINX_ACT_FLY;
}

BANKREF(sphinx_anim_tick)
void sphinx_anim_tick(void) BANKED { // per gameplay frame on the boss floor; caller is in VBlank
    uint8_t d = DIV_REG;
    uint8_t delta = (uint8_t)(d - s_div_last);
    s_div_last = d;
    if (g_sphinx_mode == SPHINX_GROUNDED) { // grounded body + wings-down (resting), no flapping
        if (s_body_frame != 0u) { s_body_frame = 0u; upload_body(0u); }
        if (s_wing_frame != 1u) { s_wing_frame = 1u; upload_wing(1u); }
        return;
    }
    // FLYING / AWAY: airborne body frame, wings flap on the wingbeat timer
    if (s_body_frame != 1u) { s_body_frame = 1u; upload_body(1u); }
    s_wacc += delta;
    if (s_wacc >= SPHINX_WING_DIV_TICKS) { s_wacc -= SPHINX_WING_DIV_TICKS; s_wing_frame ^= 1u; upload_wing(s_wing_frame); }
}
