#pragma bank 22

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "dungeon.h" // TOWN_FLOOR_BASE
#include "globals.h" // overworld_preset
#include "map.h"     // active_map_w / active_map_h
#include "render.h"  // render_strip_* buffers + render_blit_strip_* (placed here to relieve bank 2)
#include "ui.h"      // ui_combat_log_push — signpost labels print to the chat box
#include "names.h"   // town/dungeon/NPC flavor names — second chat-log line in overworld_signpost_read
#include "lcd.h"     // lcd_note_bkg0 — panic flash restores the live slot-0 ramp
#include <gb/cgb.h>

// Top-level hub floor (floor 0). No enemy roster, no items (the item scatter loop in
// map.c is skipped for BIOME_OVERWORLD). Future "areas" would add extra transition tiles
// here, routed by a position->destination lookup feeding pending_transition.

// Dark-green field: color 0 of BG slot 0 (open field / blank cells) and of PAL_FLOOR_BG
// (E3/E4 ground deco) replace the usual black. On the hub, slot 0's other three colors are
// never displayed by field art (no corpses/stairs here), so they carry the biome-border blend
// instead: idx1 = flat sand stroke (desert border tiles); for snow borders (coast tiles, whose
// stroke is inner-line idx2 + outer-band idx3) idx2 = dark separation line and idx3 = bright
// snow edge. idx3 MUST stay pure white — the loading screen's "Ascending" printf renders as
// attr-0 pen-3 while these palettes are live. Keep in sync with the copy in
// apply_field_palette() (render_palettes.c).
static const palette_color_t pal_overworld_field[] = {
    RGB(12, 23, 5), RGB(29, 24, 13), RGB(15, 19, 27), RGB(31, 31, 31),
};
static const palette_color_t pal_overworld_floor_deco[] = {
    RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17),
};
// PAL_OW_ACCENT (slot 7, freed from UI): desert sand ramp for the SE corner region.
// idx0 = open sand base (shows on blank desert floor cells); idx1 = darker grain speckle;
// idx2 = mid sand; idx3 = bright highlight. Tree/rock tiles in the desert reuse this ramp so
// they read as sandy mounds. See overworld_is_desert() / draw_cell_terrain_only in render.c.
static const palette_color_t pal_overworld_accent[] = {
    RGB(29, 24, 13), RGB(20, 15, 7), RGB(26, 21, 11), RGB(31, 29, 20),
};

// Hub town-flag colors: the flag art is drawn in the OBJ palette's mid tone (index 2). The hub has no
// enemies, so we repurpose the four enemy OBJ ramps (4/5/6/7) here, brightening only index 2 to a
// saturated primary (blue/red/green/yellow) so the flag reads as a bold banner. Indices 1 (dark outline)
// and 3 (highlight) keep the original enemy ramp so the art's shading still holds. load_palettes()
// restores the true enemy ramps on every dungeon floor, so this override is hub-only.
static const palette_color_t pal_flag_skeleton[] = { RGB(0,0,0), RGB(8,6,20),  RGB(6,12,31),  RGB(22,16,31) }; // OCP4 → bright blue mid
static const palette_color_t pal_flag_rat[]      = { RGB(0,0,0), RGB(22,6,10), RGB(31,2,2),   RGB(31,18,22) }; // OCP5 → bright red mid
static const palette_color_t pal_flag_goblin[]   = { RGB(0,0,0), RGB(18,4,18), RGB(4,31,6),   RGB(31,14,28) }; // OCP6 → bright green mid
static const palette_color_t pal_flag_bat[]      = { RGB(0,0,0), RGB(23,9,0),  RGB(31,31,2),  RGB(31,27,1)  }; // OCP7 → bright yellow mid

BANKREF(biome_overworld_load_palettes)
void biome_overworld_load_palettes(void) {
    set_bkg_palette(0, 1u, pal_overworld_field);
    lcd_note_bkg0(pal_overworld_field);
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_overworld_floor_deco);
    set_bkg_palette(PAL_OW_ACCENT, 1u, pal_overworld_accent); // slot 6 (foliage) is set by apply_wall_palette
    set_sprite_palette(PAL_LADDER, 1u, pal_flag_skeleton);     // OCP4 — enemy-less on the hub; flag uses these
    set_sprite_palette(PAL_ENEMY_RAT, 1u, pal_flag_rat);       // OCP5
    set_sprite_palette(PAL_ENEMY_GOBLIN, 1u, pal_flag_goblin); // OCP6
    set_sprite_palette(PAL_XP_UI, 1u, pal_flag_bat);           // OCP7 (also the player gold aura — stays gold-ish)
}

BANKREF(biome_overworld_copy_defs)
void biome_overworld_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for the hub
}

// ── Continent water mask ─────────────────────────────────────────────────────
// One of OVERWORLD_PRESET_COUNT seeded layouts (picked into overworld_preset by generate_level).
// Lives here in bank 22 (not the full bank-2 render bank). map_gen.c carves land from it; render.c
// draws coast from it — both call the BANKED entry points below. ow_water() is the same-bank impl
// so overworld_coast_vram() can sample neighbours without per-call trampolines.
static const OverworldPreset OW_PRESETS[OVERWORLD_PRESET_COUNT] = {
    /* land_thresh, rivers, lakes, desert_num (/32; 20 == legacy 5/8).
       Tuned for ~30% less water than the original layouts (bigger landmasses; wettest presets
       also drop one lake) — see scratch sweep. Resulting water coverage ≈ 19/29/33/44/47%. */
    { 48u, 0u, 1u, 19u }, // 0: least water — big landmass, enlarged desert (~+17%)
    { 46u, 1u, 2u, 20u }, // 1
    { 44u, 2u, 2u, 20u }, // 2
    { 43u, 2u, 2u, 20u }, // 3
    { 41u, 3u, 2u, 20u }, // 4: most water — small landmass cut by rivers/lakes
};

static uint8_t ow_hash8(uint8_t a, uint8_t b) {
    uint8_t h = (uint8_t)((uint8_t)(a * 73u) ^ (uint8_t)(b * 151u) ^ (uint8_t)(overworld_preset * 89u));
    h ^= (uint8_t)(h >> 3);
    h = (uint8_t)(h * 17u);
    h ^= (uint8_t)(h >> 5);
    return h;
}

// n² for n = 0..79 — replaces every per-cell 16-bit multiply in ow_water with a table read. The
// largest distance we ever square is ~72 (lake centre to far corner), comfortably inside 80.
static const uint16_t ow_sq[80] = {
    0, 1, 4, 9, 16, 25, 36, 49, 64, 81,
    100, 121, 144, 169, 196, 225, 256, 289, 324, 361,
    400, 441, 484, 529, 576, 625, 676, 729, 784, 841,
    900, 961, 1024, 1089, 1156, 1225, 1296, 1369, 1444, 1521,
    1600, 1681, 1764, 1849, 1936, 2025, 2116, 2209, 2304, 2401,
    2500, 2601, 2704, 2809, 2916, 3025, 3136, 3249, 3364, 3481,
    3600, 3721, 3844, 3969, 4096, 4225, 4356, 4489, 4624, 4761,
    4900, 5041, 5184, 5329, 5476, 5625, 5776, 5929, 6084, 6241,
};
#define OW_SQ(n) ow_sq[(n) < 80u ? (n) : 79u]

// Coast tiles only exist in the grass-green palette (no free CRAM slot for desert/snow coast art), so
// keep desert and snow back from every shoreline: any land within OW_COAST_GRASS_BAND tiles of water
// is forced to grassland. Used by ow_near_water() and pre-banded into ow_lake_rb2[] by ow_prepare().
// (The grass↔snow / grass↔desert borders themselves DO have transition art — see ow_border() — but
// it also lives in grass-bulk palettes, so the band rule stands.)
#define OW_COAST_GRASS_BAND 2

// Per-preset invariants, computed once by ow_prepare() instead of being re-derived (with a modulo)
// for every one of the ~8.8k cells. ow_water() then reads these — no multiplies, no modulo.
static uint8_t  ow_prep_preset = 0xFFu, ow_prep_w = 0u, ow_prep_h = 0u;
static uint8_t  ow_cx, ow_cy, ow_land_thresh, ow_nlakes, ow_nrivers;
static uint8_t  ow_lake_x[3], ow_lake_y[3];
static uint16_t ow_lake_r2[3];
static uint16_t ow_lake_rb2[3]; // (rad + OW_COAST_GRASS_BAND)^2 — banded radius for ow_near_water()
static uint8_t  ow_river_col[3];
static uint16_t ow_snow_thresh, ow_desert_thresh; // (mx+my) diagonals of the two region borders
static int8_t   ow_lobe[6][6]; // coast undulation per 16×16 block — 36 values, indexed [x>>4][y>>4]

static void ow_prepare(void) {
    const OverworldPreset *p;
    uint8_t i, a, b;
    if (overworld_preset == ow_prep_preset && active_map_w == ow_prep_w && active_map_h == ow_prep_h) return;
    ow_prep_preset = overworld_preset; ow_prep_w = active_map_w; ow_prep_h = active_map_h;
    p = &OW_PRESETS[overworld_preset];
    ow_cx = (uint8_t)(active_map_w >> 1); ow_cy = (uint8_t)(active_map_h >> 1);
    ow_land_thresh = p->land_thresh;
    ow_nlakes = p->n_lakes; ow_nrivers = p->n_rivers;
    // Region-border diagonals, hoisted out of ow_snow/ow_desert (ow_border tests them per cell).
    // Desert: preset 0 (least water) widens the sand, 19/32 vs 20/32 (== legacy 5/8) → ~+17% area.
    // Snow: ~20% less area than 3/8 (corner ∝ thresh²).
    ow_snow_thresh   = ((uint16_t)active_map_w + (uint16_t)active_map_h) / 3u;
    ow_desert_thresh = ((uint16_t)active_map_w + (uint16_t)active_map_h)
                       * (overworld_preset == 0u ? 19u : 20u) / 32u;
    for (a = 0u; a < 6u; a++)
        for (b = 0u; b < 6u; b++)
            ow_lobe[a][b] = (int8_t)((ow_hash8((uint8_t)(a + 1u), (uint8_t)(b + 1u)) & 15u)) - 7; // ~16-tile bays/capes
    for (i = 0u; i < ow_nlakes; i++) {
        uint8_t rad = (uint8_t)(3u + (ow_hash8((uint8_t)(120u + i), overworld_preset) & 3u)); // 3..6
        ow_lake_x[i]  = (uint8_t)((active_map_w >> 2) + (ow_hash8((uint8_t)(100u + i), (uint8_t)(overworld_preset + 7u)) % (active_map_w >> 1)));
        ow_lake_y[i]  = (uint8_t)((active_map_h >> 2) + (ow_hash8((uint8_t)(110u + i), (uint8_t)(overworld_preset + 9u)) % (active_map_h >> 1)));
        ow_lake_r2[i] = OW_SQ(rad);
        ow_lake_rb2[i] = OW_SQ((uint8_t)(rad + OW_COAST_GRASS_BAND)); // (rad+2)^2 for the near-water band test
    }
    for (i = 0u; i < ow_nrivers; i++)
        ow_river_col[i] = (uint8_t)((active_map_w >> 2) + (ow_hash8((uint8_t)(200u + i), (uint8_t)(overworld_preset + 50u)) % (active_map_w >> 1)));
}

static uint8_t ow_water(uint8_t x, uint8_t y) { // 1 = water (ocean / river / lake), 0 = land
    uint8_t w = active_map_w, h = active_map_h;
    uint8_t i, adx, ady;
    if (x < OVERWORLD_BORDER_BAND || y < OVERWORLD_BORDER_BAND
            || x >= (uint8_t)(w - OVERWORLD_BORDER_BAND)
            || y >= (uint8_t)(h - OVERWORLD_BORDER_BAND)) return 1u; // forced ocean margin
    {
        uint16_t d2;
        int16_t  lobe = ow_lobe[x >> 4][y >> 4];                                  // precomputed ~16-tile bays/capes
        uint8_t  jit  = (uint8_t)(((uint8_t)(x * 7u) ^ (uint8_t)(y * 13u)) & 3u); // ragged per-tile edge
        int16_t  r    = (int16_t)ow_land_thresh + lobe + (int16_t)jit;
        adx = (x > ow_cx) ? (uint8_t)(x - ow_cx) : (uint8_t)(ow_cx - x);
        ady = (y > ow_cy) ? (uint8_t)(y - ow_cy) : (uint8_t)(ow_cy - y);
        d2  = (uint16_t)(OW_SQ(adx) + OW_SQ(ady));
        if (r < 6) r = 6;
        if (d2 >= OW_SQ((uint8_t)r)) return 1u; // outside continent → ocean
    }
    for (i = 0u; i < ow_nlakes; i++) { // inland lakes
        adx = (x > ow_lake_x[i]) ? (uint8_t)(x - ow_lake_x[i]) : (uint8_t)(ow_lake_x[i] - x);
        ady = (y > ow_lake_y[i]) ? (uint8_t)(y - ow_lake_y[i]) : (uint8_t)(ow_lake_y[i] - y);
        if ((uint16_t)(OW_SQ(adx) + OW_SQ(ady)) <= ow_lake_r2[i]) return 1u;
    }
    for (i = 0u; i < ow_nrivers; i++) { // wobbling vertical channels carving the continent
        uint8_t wob = (uint8_t)(ow_hash8((uint8_t)(y >> 2), (uint8_t)(30u + i)) & 7u); // coarse wobble per 4 rows
        uint8_t col = (uint8_t)(ow_river_col[i] + wob - 3u);
        adx = (x > col) ? (uint8_t)(x - col) : (uint8_t)(col - x);
        if (adx <= 1u) return 1u; // 3-tile-wide river
    }
    return 0u; // land
}

BANKREF(overworld_water_at)
uint8_t overworld_water_at(uint8_t x, uint8_t y) BANKED { ow_prepare(); return ow_water(x, y); }

// Carve the whole landmass into floor_bits in one banked call (was ~8.8k per-cell banked calls from
// bank 10). Trees/spawn/pit are layered on afterward by generate_level.
BANKREF(overworld_carve)
void overworld_carve(void) BANKED {
    uint8_t x, y;
    ow_prepare();
    // Record every cell's water/land into the hub water mask (bank-2 WRAM) as we go, reusing the
    // ow_water() calls we already make for the landmass. Render reads these bits — for the coast lookup
    // (4 neighbours per land cell) and for the open-sea test — instead of re-running ow_water(). Iterate
    // the FULL map, not just the interior: the outermost ring (the forced-ocean border band) is rendered
    // directly, so its own water bit must be set or it draws as land. clear_all first so land cells read 0.
    overworld_water_clear_all();
    for (y = 0u; y < active_map_h; y++)
        for (x = 0u; x < active_map_w; x++) {
            uint16_t idx = TILE_IDX(x, y);
            if (ow_water(x, y)) overworld_water_set(idx); // includes the forced-ocean border ring
            else                BIT_SET(floor_bits, idx);
        }
}

// 1 if any water lies within OW_COAST_GRASS_BAND tiles of land cell (x,y) — drives the grass coast band
// that keeps desert/snow off shorelines. Instead of sampling the 5×5 neighbourhood (25 ow_water() calls,
// the overworld scroll hot path near snow/sand), test each water source analytically with its threshold
// relaxed outward by BAND. Mirrors ow_water()'s source set (lines below); the caller's cell is land, so
// every test asks "is water within BAND". Only cells already inside the desert/snow region run this.
static uint8_t ow_near_water(uint8_t x, uint8_t y) {
    uint8_t w = active_map_w, h = active_map_h;
    uint8_t i, adx, ady;
    const uint8_t margin = (uint8_t)(OVERWORLD_BORDER_BAND + OW_COAST_GRASS_BAND);
    if (x < margin || y < margin || x >= (uint8_t)(w - margin) || y >= (uint8_t)(h - margin))
        return 1u; // within BAND of the forced ocean margin
    {
        uint16_t d2;
        int16_t  lobe = ow_lobe[x >> 4][y >> 4];                                  // same coast undulation as ow_water
        uint8_t  jit  = (uint8_t)(((uint8_t)(x * 7u) ^ (uint8_t)(y * 13u)) & 3u); // same ragged per-tile edge
        int16_t  r    = (int16_t)ow_land_thresh + lobe + (int16_t)jit;
        adx = (x > ow_cx) ? (uint8_t)(x - ow_cx) : (uint8_t)(ow_cx - x);
        ady = (y > ow_cy) ? (uint8_t)(y - ow_cy) : (uint8_t)(ow_cy - y);
        d2  = (uint16_t)(OW_SQ(adx) + OW_SQ(ady));
        if (r < 6) r = 6;
        r -= OW_COAST_GRASS_BAND; if (r < 0) r = 0;
        if (d2 >= OW_SQ((uint8_t)r)) return 1u; // within BAND of the continent coast (land side)
    }
    for (i = 0u; i < ow_nlakes; i++) {
        adx = (x > ow_lake_x[i]) ? (uint8_t)(x - ow_lake_x[i]) : (uint8_t)(ow_lake_x[i] - x);
        ady = (y > ow_lake_y[i]) ? (uint8_t)(y - ow_lake_y[i]) : (uint8_t)(ow_lake_y[i] - y);
        if ((uint16_t)(OW_SQ(adx) + OW_SQ(ady)) <= ow_lake_rb2[i]) return 1u; // within BAND of a lake
    }
    for (i = 0u; i < ow_nrivers; i++) {
        uint8_t wob = (uint8_t)(ow_hash8((uint8_t)(y >> 2), (uint8_t)(30u + i)) & 7u);
        uint8_t col = (uint8_t)(ow_river_col[i] + wob - 3u);
        adx = (x > col) ? (uint8_t)(x - col) : (uint8_t)(col - x);
        if (adx <= (uint8_t)(1u + OW_COAST_GRASS_BAND)) return 1u; // within BAND of the 3-wide river
    }
    return 0u;
}

// Same-bank region tests (assume ow_prepare() already ran). The BANKED wrappers below add the
// trampoline + ow_prepare for cross-bank callers; overworld_cell_render() calls these directly so a
// hub cell pays one prepare + zero internal trampolines instead of re-entering the bank per query.
static uint8_t ow_desert(uint8_t mx, uint8_t my) { // hub SE corner: diagonal "coast" meeting the sea
    uint16_t sum = (uint16_t)mx + (uint16_t)my;
    uint8_t  jitter = (uint8_t)(((uint8_t)(mx * 7u) ^ (uint8_t)(my * 13u)) & 3u); // ragged edge, +0..3 tiles
    if ((sum + jitter) < ow_desert_thresh) return 0u; // outside the SE sand region
    return ow_near_water(mx, my) ? 0u : 1u;       // grass band hugs the coast
}

static uint8_t ow_snow(uint8_t mx, uint8_t my) { // hub NW corner: snowfield on the freed PAL_WALL_BG slot
    uint16_t sum = (uint16_t)mx + (uint16_t)my;
    uint8_t  jitter = (uint8_t)(((uint8_t)(mx * 11u) ^ (uint8_t)(my * 5u)) & 3u); // ragged edge
    if (sum > (uint16_t)(ow_snow_thresh + jitter)) return 0u; // outside the NW snow region
    return ow_near_water(mx, my) ? 0u : 1u;           // grass band hugs the coast
}

BANKREF(overworld_is_desert)
uint8_t overworld_is_desert(uint8_t mx, uint8_t my) BANKED { ow_prepare(); return ow_desert(mx, my); }

BANKREF(overworld_is_snow)
uint8_t overworld_is_snow(uint8_t mx, uint8_t my) BANKED { ow_prepare(); return ow_snow(mx, my); }

// Coast tiles are drawn on the LAND cell that borders water: their bulk (index 0) is the green field
// and the shore stroke (index 2/3) is the blue water edge, oriented toward whichever side is sea. For
// a land cell, return the coast VRAM tile matching its water neighbours, or 0 if it is interior land
// (caller then draws normal ground). The 8 sheet tiles give top/bottom edges + 4 corners only, so
// E/W shores reuse the corner art. Land never touches the map edge (border band is forced ocean), so
// neighbours are always in-bounds.
static uint8_t ow_coast(uint8_t mx, uint8_t my) { // assumes the water mask was built by overworld_carve
    uint8_t wn = overworld_water_bit(TILE_IDX(mx, (uint8_t)(my - 1u))); // water to the north?
    uint8_t ws = overworld_water_bit(TILE_IDX(mx, (uint8_t)(my + 1u)));
    uint8_t we = overworld_water_bit(TILE_IDX((uint8_t)(mx + 1u), my));
    uint8_t ww = overworld_water_bit(TILE_IDX((uint8_t)(mx - 1u), my));
    if (wn && we) return COAST_VRAM_NE;
    if (wn && ww) return COAST_VRAM_NW;
    if (ws && we) return COAST_VRAM_SE;
    if (ws && ww) return COAST_VRAM_SW;
    if (wn) return (mx & 1u) ? COAST_VRAM_NA : COAST_VRAM_N;
    if (ws) return (mx & 1u) ? COAST_VRAM_SA : COAST_VRAM_S;
    if (we) return (my & 1u) ? COAST_VRAM_SE : COAST_VRAM_NE; // E shore ← corner art
    if (ww) return (my & 1u) ? COAST_VRAM_SW : COAST_VRAM_NW; // W shore ← corner art
    return 0u; // interior land — no coast, draw normal ground
}

BANKREF(overworld_coast_vram)
uint8_t overworld_coast_vram(uint8_t mx, uint8_t my) BANKED { ow_prepare(); return ow_coast(mx, my); }

// Biome-border transition on a GRASS cell — same shape as ow_coast, but the "water" oracle is the
// snow/desert region test of the 4 neighbours, and the stroke colors ride slot 0 (see
// pal_overworld_field). Snow borders reuse the coast tiles verbatim (their stroke reads as a dark
// idx2 separation line + bright white idx3 edge under palette 0); desert borders use the 3
// flat-stroke tiles biome.c remaps
// at hub load (stroke idx1 = sand), oriented with BG-attr X/Y flips. attr_out gets palette 0 plus
// any flip bits; only meaningful when the return is nonzero.
// Pre-filter: both regions are ragged (mx+my) diagonals with jitter ≤ 3, so a grass cell can only
// border snow when sum-1 <= thresh+3, resp. desert when sum+1 >= thresh-3 (+5 margin used for
// safety) — every other cell skips the ow_near_water-heavy region tests entirely. The two bands
// sit at ~(w+h)/3 vs ~5(w+h)/8 (opposite corners), so they never overlap. Caller guarantees the
// cell is grass floor and not a road; like ow_coast, land never touches the forced-ocean map
// border, so all four neighbours are in-bounds.
static uint8_t ow_border(uint8_t mx, uint8_t my, uint8_t *attr_out) {
    uint16_t sum = (uint16_t)mx + (uint16_t)my;
    if (sum <= (uint16_t)(ow_snow_thresh + 5u)) {
        uint8_t bn = ow_snow(mx, (uint8_t)(my - 1u)), bs = ow_snow(mx, (uint8_t)(my + 1u));
        uint8_t be = ow_snow((uint8_t)(mx + 1u), my), bw = ow_snow((uint8_t)(mx - 1u), my);
        *attr_out = 0u; // palette 0: green bulk idx0, snow stroke idx2/3
        if (bn && be) return COAST_VRAM_NE;
        if (bn && bw) return COAST_VRAM_NW;
        if (bs && be) return COAST_VRAM_SE;
        if (bs && bw) return COAST_VRAM_SW;
        if (bn) return (mx & 1u) ? COAST_VRAM_NA : COAST_VRAM_N;
        if (bs) return (mx & 1u) ? COAST_VRAM_SA : COAST_VRAM_S;
        if (be) return (my & 1u) ? COAST_VRAM_SE : COAST_VRAM_NE; // E border ← corner art
        if (bw) return (my & 1u) ? COAST_VRAM_SW : COAST_VRAM_NW; // W border ← corner art
        return 0u;
    }
    if ((uint16_t)(sum + 5u) >= ow_desert_thresh) {
        uint8_t bn = ow_desert(mx, (uint8_t)(my - 1u)), bs = ow_desert(mx, (uint8_t)(my + 1u));
        uint8_t be = ow_desert((uint8_t)(mx + 1u), my), bw = ow_desert((uint8_t)(mx - 1u), my);
        uint8_t t = 0u, fl = 0u;
        if      (bn && be) { t = BORDER_VRAM_CORNER_NW; fl = S_FLIPX; }
        else if (bn && bw)   t = BORDER_VRAM_CORNER_NW;
        else if (bs && be) { t = BORDER_VRAM_CORNER_NW; fl = S_FLIPX | S_FLIPY; }
        else if (bs && bw) { t = BORDER_VRAM_CORNER_NW; fl = S_FLIPY; }
        else if (bn)         t = (mx & 1u) ? BORDER_VRAM_EDGE_NA : BORDER_VRAM_EDGE_N;
        else if (bs)       { t = (mx & 1u) ? BORDER_VRAM_EDGE_NA : BORDER_VRAM_EDGE_N; fl = S_FLIPY; }
        else if (be)       { t = BORDER_VRAM_CORNER_NW; fl = (my & 1u) ? (S_FLIPX | S_FLIPY) : S_FLIPX; } // E ← corner art
        else if (bw)       { t = BORDER_VRAM_CORNER_NW; fl = (my & 1u) ? S_FLIPY : 0u; }                  // W ← corner art
        *attr_out = fl; // palette 0: green bulk idx0, flat sand stroke idx1
        return t;
    }
    return 0u;
}

// Part D: if (x,y) is the walkable trigger cell of a placed hub feature, return its OW_FEAT_* type,
// else 255. The trigger cell is the footprint's top-left plus the prefab's ent_dx/ent_dy (globals.c).
// state_gameplay uses this on the hub to route entrances (and, later, towns/waypoints/encounters) into
// a sub-map transition. Cheap: called once per player step, not per frame.
BANKREF(overworld_trigger_at)
uint8_t overworld_trigger_at(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
        if ((uint8_t)(ow_features[i].x + d->ent_dx) == x && (uint8_t)(ow_features[i].y + d->ent_dy) == y)
            return ow_features[i].type;
    }
    return 255u;
}

// Ordinal (0..8) of the OW_FEAT_ENTRANCE at (x,y) — the dungeon id. Placement order is
// grass x3, desert x3, snow x3 (place_overworld_features), so ordinals are seed-stable
// and match the signpost numbering. 255 if no entrance sits there.
BANKREF(overworld_entrance_id_at)
uint8_t overworld_entrance_id_at(uint8_t x, uint8_t y) BANKED {
    uint8_t i, ord = 0u;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_ENTRANCE) continue; // entrance is 1x1: its cell is the trigger
        if (ow_features[i].x == x && ow_features[i].y == y) return ord;
        ord++;
    }
    return 255u;
}

// Ordinal (0..2) of the OW_FEAT_TOWN whose door trigger cell is (x,y) — the town id. Placement
// order is one town per region (grass, desert, snow), so ordinals are seed-stable and match the
// TOWN signpost numbering. 255 if no town door sits there.
BANKREF(overworld_town_id_at)
uint8_t overworld_town_id_at(uint8_t x, uint8_t y) BANKED {
    uint8_t i, ord = 0u;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_TOWN) continue;
        if ((uint8_t)(ow_features[i].x + ow_prefab_defs[OW_FEAT_TOWN].ent_dx) == x
                && (uint8_t)(ow_features[i].y + ow_prefab_defs[OW_FEAT_TOWN].ent_dy) == y)
            return ord;
        ord++;
    }
    return 255u;
}

// Walked onto (x,y): handle any 1×1 step feature there — signpost/NPC prints its line, a town
// fountain restores full HP. One banked call replaces the old signpost aux_at+read pair.
BANKREF(overworld_step_feature)
void overworld_step_feature(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].x != x || ow_features[i].y != y) continue;
        if (ow_features[i].type == OW_FEAT_SIGNPOST) {
            overworld_signpost_read(ow_features[i].aux);
            return;
        }
        if (ow_features[i].type == OW_FEAT_FOUNTAIN) {
            char buf[8]; // RAM copy — bank-22 ROM literal would garble in the bank-5 log push
            const char *s = "RESTED";
            uint8_t k;
            for (k = 0u; s[k]; k++) buf[k] = s[k];
            buf[k] = 0;
            player_hp = player_hp_max;
            ui_combat_log_push(buf);
            return;
        }
    }
}

// 1 if (x,y) lands inside any placed feature's footprint (town walls, waypoints, signs, mouths).
static uint8_t ow_cell_in_footprint(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
        if (x >= ow_features[i].x && x < (uint8_t)(ow_features[i].x + d->w)
                && y >= ow_features[i].y && y < (uint8_t)(ow_features[i].y + d->h))
            return 1u;
    }
    return 0u;
}

// Returning from dungeon k (or town, id 0x80|k): override the hub spawn to an open land cell
// ringing the feature's footprint, so the player pops out beside the mouth/door they used instead
// of the continent centre. Called by level_generate_and_spawn after generate_level.
BANKREF(overworld_place_player_near_entrance)
void overworld_place_player_near_entrance(uint8_t id) BANKED {
    uint8_t i, ord = 0u, ex = 0u, ey = 0u, found = 0u;
    uint8_t want_type = (id & 0x80u) ? OW_FEAT_TOWN : OW_FEAT_ENTRANCE;
    uint8_t want_ord  = (uint8_t)(id & 0x7Fu);
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != want_type) continue;
        if (ord == want_ord) { // towns are 3×3 — ring-scan from the door cell so radius 1 clears the walls
            ex = (uint8_t)(ow_features[i].x + ow_prefab_defs[want_type].ent_dx);
            ey = (uint8_t)(ow_features[i].y + ow_prefab_defs[want_type].ent_dy);
            found = 1u;
            break;
        }
        ord++;
    }
    if (!found) return; // degenerate seed placed fewer features — keep the default spawn
    {
        uint8_t rad;
        for (rad = 1u; rad <= 4u; rad++) {
            int8_t ox, oy;
            for (oy = -(int8_t)rad; oy <= (int8_t)rad; oy++)
                for (ox = -(int8_t)rad; ox <= (int8_t)rad; ox++) {
                    int16_t cx, cy;
                    if (ox > -(int8_t)rad && ox < (int8_t)rad && oy > -(int8_t)rad && oy < (int8_t)rad) continue; // ring only
                    cx = (int16_t)ex + ox; cy = (int16_t)ey + oy;
                    if (cx < 1 || cy < 1 || cx >= (int16_t)(active_map_w - 1u) || cy >= (int16_t)(active_map_h - 1u)) continue;
                    if (!BIT_GET(floor_bits, TILE_IDX((uint8_t)cx, (uint8_t)cy))) continue; // water/tree
                    if (ow_cell_in_footprint((uint8_t)cx, (uint8_t)cy)) continue;           // don't spawn inside a structure
                    player_spawn_x = (uint8_t)cx;
                    player_spawn_y = (uint8_t)cy;
                    return;
                }
        }
    }
}

// If a signpost sits at (x,y), return its packed label code (SIGN_KIND_* | index); else 255.
BANKREF(overworld_signpost_aux_at)
uint8_t overworld_signpost_aux_at(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++)
        if (ow_features[i].type == OW_FEAT_SIGNPOST && ow_features[i].x == x && ow_features[i].y == y)
            return ow_features[i].aux;
    return 255u;
}

// Build the signpost's label from its aux code and print it to the chat box. The label text is composed
// into a RAM buffer before the cross-bank ui_combat_log_push (bank 5) — passing a bank-22 ROM literal
// straight into a call in another bank garbles it (see project_cross_bank_string_literal_gotcha).
BANKREF(overworld_signpost_read)
void overworld_signpost_read(uint8_t aux) BANKED {
    char buf[16];
    uint8_t kind = (uint8_t)(aux & 0xF0u), num = (uint8_t)(aux & 0x0Fu), i;
    if (kind == SIGN_KIND_TOWN) {
        const char *s = "TOWN "; for (i = 0u; s[i]; i++) buf[i] = s[i];
        buf[i] = (char)('1' + num); buf[i + 1u] = 0;
    } else if (kind == SIGN_KIND_WAYPOINT) {
        static const char dirs[4][3] = { "NE", "NW", "SE", "SW" };
        const char *s = " WAYPOINT";
        if (num > 3u) num = 0u;
        buf[0] = dirs[num][0]; buf[1] = dirs[num][1];
        for (i = 0u; s[i]; i++) buf[2u + i] = s[i]; buf[2u + i] = 0;
    } else if (kind == SIGN_KIND_DUNGEON) {
        // "DNG3 F09-12" — fixed depth band of dungeon `num` (dungeon.h); '*' suffix once completed
        uint8_t fb = (uint8_t)(num * 4u + 1u), fe = (uint8_t)(num * 4u + 4u);
        buf[0] = 'D'; buf[1] = 'N'; buf[2] = 'G'; buf[3] = (char)('1' + num);
        buf[4] = ' '; buf[5] = 'F';
        buf[6] = (char)('0' + fb / 10u); buf[7] = (char)('0' + fb % 10u);
        buf[8] = '-';
        buf[9] = (char)('0' + fe / 10u); buf[10] = (char)('0' + fe % 10u);
        i = 11u;
        if (dungeon_complete_mask & (uint16_t)((uint16_t)1u << num)) buf[i++] = '*';
        buf[i] = 0;
    } else if (kind == SIGN_KIND_NPC) { // town villager — canned line by index
        static const char *const npc_lines[4] = { "WELCOME, HERO", "REST AT THE WELL", "SAFE INSIDE WALLS", "FINE DAY, NO?" };
        const char *s = npc_lines[num & 3u];
        for (i = 0u; s[i] && i < 15u; i++) buf[i] = s[i];
        buf[i] = 0;
    } else { // SIGN_KIND_BOSS
        const char *s = "FINAL DUNGEON"; for (i = 0u; s[i]; i++) buf[i] = s[i]; buf[i] = 0;
    }
    ui_combat_log_push(buf);

    // Second line: the generated flavor name (src/names.c), deterministic per (run_seed, id).
    if (kind == SIGN_KIND_TOWN || kind == SIGN_KIND_DUNGEON || kind == SIGN_KIND_NPC) {
        char nbuf[16];
        if (kind == SIGN_KIND_TOWN) {
            town_name_copy(num, nbuf, sizeof nbuf);
        } else if (kind == SIGN_KIND_DUNGEON) {
            dungeon_name_copy(num, nbuf, sizeof nbuf);
        } else {
            // aux's low nibble is only the villager slot (0..2, biome_town.c); this signpost
            // only exists inside a town interior, so the town id is floor_num - TOWN_FLOOR_BASE.
            npc_name_copy((uint8_t)(floor_num - TOWN_FLOOR_BASE), num, nbuf, sizeof nbuf);
        }
        ui_combat_log_push(nbuf);
    }
}

// Prefab tile lookup: which VRAM tile to draw for local cell (lx,ly) of a w×h feature of the given type.
// Returns 0 for the town's interior cell (a grass courtyard — caller draws normal ground). Town wall ring
// uses corner / E-W / N-S art; waypoint is a 2×2 of distinct quadrants; entrance is its single mouth tile.
static uint8_t ow_prefab_vram(uint8_t type, uint8_t lx, uint8_t ly, uint8_t w, uint8_t h) {
    if (type == OW_FEAT_SIGNPOST) return PREFAB_VRAM_SIGNPOST;
    if (type == OW_FEAT_ENTRANCE) return PREFAB_VRAM_ENTRANCE;
    if (type == OW_FEAT_WAYPOINT) {
        if (ly == 0u) return (lx == 0u) ? PREFAB_VRAM_WP_TL : PREFAB_VRAM_WP_TR;
        return (lx == 0u) ? PREFAB_VRAM_WP_BL : PREFAB_VRAM_WP_BR;
    }
    if (type == OW_FEAT_BOSSDOOR) {
        if (ly == 0u) return (lx == 0u) ? PREFAB_VRAM_DOOR_TL : PREFAB_VRAM_DOOR_TR;
        return (lx == 0u) ? PREFAB_VRAM_DOOR_BL : PREFAB_VRAM_DOOR_BR;
    }
    { // OW_FEAT_TOWN — 3×3 wall ring, grass centre, open door (G1) at the entrance cell
        uint8_t edge_x = (lx == 0u || lx == (uint8_t)(w - 1u));
        if (lx == ow_prefab_defs[OW_FEAT_TOWN].ent_dx && ly == ow_prefab_defs[OW_FEAT_TOWN].ent_dy)
            return (uint8_t)(TILESET_VRAM_OFFSET + TILE_DOOR_OPEN);
        uint8_t edge_y = (ly == 0u || ly == (uint8_t)(h - 1u));
        if (edge_x && edge_y) return PREFAB_VRAM_TOWN_CORNER;
        if (edge_y)           return PREFAB_VRAM_TOWN_WALL_NS;
        if (edge_x)           return PREFAB_VRAM_TOWN_WALL_EW;
        return 0u; // interior courtyard
    }
}

// Single per-cell classifier consumed by render.c (bank 2). Folds the desert/snow region test, the
// water/tree split for WALL cells, and the coast lookup for FLOOR cells into one banked trampoline.
// Returns the finished VRAM tile (0 = interior ground — render.c draws floor-deco using *region_out).
// Future prefab-feature overlays (towns/waypoints/entrances) slot in at the marked point: a covered
// cell returns its prefab tile + palette here, so the renderer stays one-trampoline-per-cell.
BANKREF(overworld_cell_render)
uint8_t overworld_cell_render(uint8_t mx, uint8_t my, uint8_t base_tile,
                              uint8_t *pal_out, uint8_t *region_out) BANKED {
    uint8_t region;
    if (floor_biome == BIOME_TOWN) {
        // Town interior: grass field like the hub. Features resolve first (fountain / NPC / sign /
        // deco pine); wall cells are drawn as dungeon brick here (uniform bulk art — the thin
        // building walls would otherwise hit render.c's pillar heuristic); road-lane floor cells
        // report OW_REGION_DESERT so they render as the hub's open-sand roads. Every tile used is
        // title-stomp-safe: 161 re-uploads per floor, shrine/brick are main-sheet, 205/213 are
        // permanent boot copies.
        uint8_t fi;
        *region_out = OW_REGION_GRASS;
        // NPC body tile: one cell below the head
        for (fi = 0u; fi < ow_feature_count; fi++) {
            if (ow_features[fi].type == OW_FEAT_SIGNPOST
                    && (uint8_t)(ow_features[fi].aux & 0xF0u) == SIGN_KIND_NPC
                    && ow_features[fi].x == mx && (uint8_t)(ow_features[fi].y + 1u) == my) {
                *pal_out = PAL_PILLAR_BG; return TILE_PLAYER_BODY_STAND_VRAM;
            }
        }
        for (fi = 0u; fi < ow_feature_count; fi++) {
            if (ow_features[fi].x != mx || ow_features[fi].y != my) continue; // town features are all 1×1
            if (ow_features[fi].type == OW_FEAT_TREE) {
                *pal_out = PAL_OW_FOLIAGE; return TILE_OVERWORLD_WALL_VRAM; // deco pine (cell is blocking wall)
            }
            if (ow_features[fi].type == OW_FEAT_FOUNTAIN) {
                *pal_out = PAL_PILLAR_BG; return (uint8_t)(TILESET_VRAM_OFFSET + TILE_SHRINE_ON_1); // stone well
            }
            if (ow_features[fi].type == OW_FEAT_SIGNPOST) {
                if ((uint8_t)(ow_features[fi].aux & 0xF0u) == SIGN_KIND_NPC) {
                    *pal_out = PAL_PILLAR_BG; return TILE_PLAYER_HEAD_VRAM; // villager: hero-head art, stone ramp
                }
                *pal_out = PAL_FLOOR_BG; return PREFAB_VRAM_SIGNPOST;
            }
        }
        if (base_tile == TILE_WALL) { // town wall ring + building walls: uniform dungeon brick on grass
            *pal_out = PAL_WALL_BG;
            return (uint8_t)(TILESET_VRAM_OFFSET + wall_tileset_index);
        }
        { // road cross: door column up to the plaza + full plaza row — same open-sand look as hub roads
            uint8_t cx = (uint8_t)(active_map_w >> 1), cy = (uint8_t)(active_map_h >> 1);
            if ((mx == cx && my >= cy) || my == cy) *region_out = OW_REGION_DESERT;
        }
        return 0u;
    }
    ow_prepare();
    if (ow_snow(mx, my))        region = OW_REGION_SNOW;   // NW corner takes priority (regions don't overlap)
    else if (ow_desert(mx, my)) region = OW_REGION_DESERT; // SE corner
    else                        region = OW_REGION_GRASS;
    *region_out = region;

    // Prefab-feature overlay: a covered cell returns its prefab tile before the procedural terrain below,
    // so render stays one trampoline per cell. All feature cells are walkable (Part D triggers a sub-map
    // when you step onto any of them); the town's interior cell returns 0 here so its courtyard draws as
    // normal grass ground.
    {
        uint8_t fi, ent_ord = 0u;
        for (fi = 0u; fi < ow_feature_count; fi++) {
            uint8_t fx = ow_features[fi].x, fy = ow_features[fi].y;
            uint8_t is_ent = (ow_features[fi].type == OW_FEAT_ENTRANCE);
            const OwPrefabDef *d = &ow_prefab_defs[ow_features[fi].type];
            if (mx >= fx && mx < (uint8_t)(fx + d->w) && my >= fy && my < (uint8_t)(fy + d->h)) {
                uint8_t v = ow_prefab_vram(ow_features[fi].type, (uint8_t)(mx - fx), (uint8_t)(my - fy),
                                           d->w, d->h);
                if (is_ent && (dungeon_complete_mask & (uint16_t)((uint16_t)1u << ent_ord)))
                    v = PREFAB_VRAM_TOWN_CORNER; // completed dungeon: mouth drawn as a bricked-up grey block
                if (v) { *pal_out = PAL_FLOOR_BG; return v; } // grey wall art over the green field (idx0)
                break; // town courtyard centre → fall through to grass ground
            }
            if (is_ent) ent_ord++; // ordinal = dungeon id (same counting as overworld_entrance_id_at)
        }
    }

    if (base_tile == TILE_WALL) {
        if (overworld_water_bit(TILE_IDX(mx, my))) { *pal_out = PAL_PILLAR_BG; return TILE_OVERWORLD_WATER_VRAM; } // open sea (sparkle overlay flips a few cells per step — render.c)
        if (region == OW_REGION_SNOW) { // snow → 2-wide mountains (B9 on even cols, C9 on odd, forming a range)
            *pal_out = PAL_WALL_BG;
            return (mx & 1u) ? PREFAB_VRAM_MTN_R : PREFAB_VRAM_MTN_L;
        }
        if (region == OW_REGION_DESERT) { // desert → sparse D6 palms (already boot-loaded at VRAM 211)
            *pal_out = PAL_OW_ACCENT;
            return (uint8_t)(TILESET_VRAM_OFFSET + TILE_COLUMN_6);
        }
        *pal_out = PAL_OW_FOLIAGE; // grassland → green pine
        return TILE_OVERWORLD_WALL_VRAM;
    }
    if (base_tile == TILE_FLOOR) {
        uint8_t coast = ow_coast(mx, my); // shore tile when this land cell borders water
        if (coast) { *pal_out = PAL_PILLAR_BG; return coast; } // green land bulk + blue shore edge
        if (road_bit(TILE_IDX(mx, my))) *region_out = OW_REGION_DESERT; // road → sand ramp; A1 texture via floor_tile_sheet_offset
        else if (region == OW_REGION_GRASS) { // grass cell at a snow/desert border → transition stroke
            uint8_t v = ow_border(mx, my, pal_out);
            if (v) return v; // pal_out carries palette 0 + any flip bits
        }
    }
    return 0u; // interior ground (or a visible pit/other tile) — caller draws via its own path
}

// ── Batched strip classification ─────────────────────────────────────────────
// One BANKED entry classifies a whole camera strip (14/21 cells) instead of render.c paying a
// bank-2→22 trampoline per cell. Water/road mask bits are fetched as BYTES via wram2_read_byte
// (one SVBK round-trip per 8 bits) instead of 4-5 single-bit SVBK switches per floor cell.
// Behavior is branch-for-branch identical to classify_cell + overworld_cell_render for hub cells;
// the hub-only shortcuts (no corpses, no stairs-up glyph on floor 0) hold because the hub never
// spawns enemies and floor_num==0 skips the spawn-cell art in floor_tile_sheet_offset.

#define OW_MASK_WATER 0x480u          // wram2_read_byte offsets (lighting.c mask maps)
#define OW_MASK_ROAD  0x900u
#define OW_ROW_BYTES  ((uint8_t)(MAP_W >> 3))

// Bit test via mask table, NOT variable shifts: SDCC 4.x miscompiled the dense
// `(arr[i] >> (x & 7)) & 1` idiom here (it shifted the shift count and never loaded the byte —
// found by the in-ROM parity sweep). The table is also faster on SM83 (no shift loop).
static const uint8_t ow_bitmask[8] = { 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u };

#define OWB_C    0x01u // packed neighbour water bits + road bit for one cell
#define OWB_N    0x02u
#define OWB_S    0x04u
#define OWB_E    0x08u
#define OWB_W    0x10u
#define OWB_ROAD 0x20u

// Features whose footprint intersects the current strip (pre-filtered once per strip so the
// per-cell overlay test scans ≤ a handful instead of all ow_features). fl_ord preserves the
// entrance ordinal counting of overworld_cell_render (= dungeon id for the SEALED brick-up).
#define OW_FL_MAX 8u
static uint8_t fl_x[OW_FL_MAX], fl_y[OW_FL_MAX], fl_w[OW_FL_MAX], fl_h[OW_FL_MAX];
static uint8_t fl_type[OW_FL_MAX], fl_ord[OW_FL_MAX];
static uint8_t fl_n;

static void ow_filter_features(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) { // inclusive bbox
    uint8_t i, ent_ord = 0u;
    fl_n = 0u;
    for (i = 0u; i < ow_feature_count; i++) {
        const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
        uint8_t fx = ow_features[i].x, fy = ow_features[i].y;
        if (fx <= x1 && (uint8_t)(fx + d->w) > x0 && fy <= y1 && (uint8_t)(fy + d->h) > y0
                && fl_n < OW_FL_MAX) {
            fl_x[fl_n] = fx; fl_y[fl_n] = fy; fl_w[fl_n] = d->w; fl_h[fl_n] = d->h;
            fl_type[fl_n] = ow_features[i].type; fl_ord[fl_n] = ent_ord;
            fl_n++;
        }
        if (ow_features[i].type == OW_FEAT_ENTRANCE) ent_ord++;
    }
}

static uint8_t owb_attr; // attr side-channel of ow_cell_batch (avoids a 5th call argument)

// Classify one hub cell from pre-fetched mask bits. Mirrors overworld_cell_render + the hub arms of
// classify_cell / floor_tile_sheet_offset, with the region tests reordered AFTER the water/coast/road
// early-outs (region only affects the output of cells that reach the ground/tree branches, so ocean,
// coast and road cells now skip the two ow_near_water-calling region tests entirely).
static uint8_t ow_cell_batch(uint8_t mx, uint8_t my, uint8_t t, uint8_t wb) {
    uint8_t i;
    for (i = 0u; i < fl_n; i++) { // prefab overlay first, same order as overworld_cell_render
        if (mx >= fl_x[i] && mx < (uint8_t)(fl_x[i] + fl_w[i])
                && my >= fl_y[i] && my < (uint8_t)(fl_y[i] + fl_h[i])) {
            uint8_t v = ow_prefab_vram(fl_type[i], (uint8_t)(mx - fl_x[i]), (uint8_t)(my - fl_y[i]),
                                       fl_w[i], fl_h[i]);
            if (fl_type[i] == OW_FEAT_ENTRANCE
                    && (dungeon_complete_mask & (uint16_t)((uint16_t)1u << fl_ord[i])))
                v = PREFAB_VRAM_TOWN_CORNER; // completed dungeon: bricked-up mouth
            if (v) { owb_attr = PAL_FLOOR_BG; return v; }
            break; // town courtyard centre → fall through to terrain
        }
    }
    if (t == TILE_WALL) {
        if (wb & OWB_C) { owb_attr = PAL_PILLAR_BG; return TILE_OVERWORLD_WATER_VRAM; } // open sea
        if (ow_snow(mx, my)) { owb_attr = PAL_WALL_BG; return (mx & 1u) ? PREFAB_VRAM_MTN_R : PREFAB_VRAM_MTN_L; }
        if (ow_desert(mx, my)) { owb_attr = PAL_OW_ACCENT; return (uint8_t)(TILESET_VRAM_OFFSET + TILE_COLUMN_6); }
        owb_attr = PAL_OW_FOLIAGE; return TILE_OVERWORLD_WALL_VRAM; // grassland pine
    }
    if (t == TILE_PIT) { // hub never places pits (map_gen), but keep classify_cell's fallback exact
        owb_attr = PAL_LADDER; return (uint8_t)(TILESET_VRAM_OFFSET + TILE_LADDER_DOWN);
    }
    { // TILE_FLOOR: coast stroke from the cached neighbour bits (same branch chain as ow_coast)
        uint8_t coast = 0u;
        uint8_t wn = (uint8_t)(wb & OWB_N), ws = (uint8_t)(wb & OWB_S);
        uint8_t we = (uint8_t)(wb & OWB_E), ww = (uint8_t)(wb & OWB_W);
        if      (wn && we) coast = COAST_VRAM_NE;
        else if (wn && ww) coast = COAST_VRAM_NW;
        else if (ws && we) coast = COAST_VRAM_SE;
        else if (ws && ww) coast = COAST_VRAM_SW;
        else if (wn) coast = (mx & 1u) ? COAST_VRAM_NA : COAST_VRAM_N;
        else if (ws) coast = (mx & 1u) ? COAST_VRAM_SA : COAST_VRAM_S;
        else if (we) coast = (my & 1u) ? COAST_VRAM_SE : COAST_VRAM_NE;
        else if (ww) coast = (my & 1u) ? COAST_VRAM_SW : COAST_VRAM_NW;
        if (coast) { owb_attr = PAL_PILLAR_BG; return coast; }
    }
    {
        uint8_t snow = 0u, desert = 0u;
        if (wb & OWB_ROAD) desert = 1u; // road → sand ramp, A1 texture below (overrides region)
        else if (ow_snow(mx, my)) snow = 1u;
        else if (ow_desert(mx, my)) desert = 1u;
        if (!snow && !desert) { // grass cell (and not road) at a snow/desert border → transition stroke
            uint8_t v = ow_border(mx, my, &owb_attr);
            if (v) return v; // owb_attr carries palette 0 + any flip bits
        }
        for (i = 0u; i < brazier_count; i++) // hub target_count is 0 today; kept for parity
            if (brazier_x[i] == mx && brazier_y[i] == my) {
                uint8_t off = (brazier_type[i] == 0u)
                    ? ((((uint8_t)(DIV_REG >> 3) + mx + my) & 1u) ? TILE_LIGHT_4 : TILE_LIGHT_3)
                    : ((((uint8_t)(DIV_REG >> 3) + mx + my) & 1u) ? TILE_LIGHT_2 : TILE_LIGHT_1);
                owb_attr = PAL_LADDER; // (off & 15) == 2 for all four light tiles
                return (uint8_t)(TILESET_VRAM_OFFSET + off);
            }
        for (i = 0u; i < MAX_GROUND_ITEMS; i++)
            if (ground_item_kind[i] != ITEM_KIND_NONE && ground_item_x[i] == mx && ground_item_y[i] == my) {
                owb_attr = PAL_ITEM_GOLD_BG;
                return (uint8_t)(TILESET_VRAM_OFFSET + TILE_ITEM_4);
            }
        if (wb & OWB_ROAD) { // road: the dungeon-floor A1 texture in the sand ramp (uniform, no scatter)
            owb_attr = PAL_OW_ACCENT;
            return (uint8_t)(TILESET_VRAM_OFFSET + TILE_TEST);
        }
        { // blank-scatter hash — identical to map.c floor_tile_is_blank
            uint16_t h = (uint16_t)mx * 2971u ^ (uint16_t)my * 1619u ^ floor_visual_seed;
            h ^= (uint16_t)(h >> 5);
            if ((uint8_t)((h & 7u) == 0u)) {
                owb_attr = snow ? PAL_WALL_BG : (desert ? PAL_OW_ACCENT : 0u); // blank snow/sand vs green field
                return 0u;
            }
        }
        { // E3/E4 ground deco pick — identical to map.c floor_tile_sheet_offset (floor_num==0 → run_seed)
            uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)floor_num * 131u));
            uint8_t mix;
            h ^= (uint16_t)((uint16_t)mx * 911u ^ (uint16_t)my * 357u);
            mix = (uint8_t)h ^ (uint8_t)(h >> 8);
            owb_attr = snow ? PAL_WALL_BG : (desert ? PAL_OW_ACCENT : PAL_FLOOR_BG);
            return (uint8_t)(TILESET_VRAM_OFFSET + ((mix & 1u) ? TILE_GROUND_D : TILE_GROUND_C));
        }
    }
}

static uint8_t owb_w3[GRID_H + 3u]; // col strip: per-row water bits (bit0=W col, bit1=mid, bit2=E col)
static uint8_t owb_rd[GRID_H + 1u]; // col strip: per-row road bit of the mid column
static uint8_t owb_wrow[3][4];      // row strip: 3 mask rows × byte span covering x0-1..x0+21
static uint8_t owb_rrow[4];         // row strip: road bytes of the strip's own row

BANKREF(overworld_classify_col_strip)
void overworld_classify_col_strip(uint8_t mx, uint8_t cam_ty) BANKED {
    const uint8_t n = (uint8_t)(GRID_H + 1u);
    uint8_t xl = (mx > 0u) ? (uint8_t)(mx - 1u) : 0u;
    uint8_t xr = (mx < (uint8_t)(MAP_W - 1u)) ? (uint8_t)(mx + 1u) : (uint8_t)(MAP_W - 1u);
    uint8_t bl = (uint8_t)(xl >> 3), br = (uint8_t)(xr >> 3), bm = (uint8_t)(mx >> 3);
    uint8_t r;
    ow_prepare();
    ow_filter_features(mx, cam_ty, mx, (uint8_t)(cam_ty + n - 1u));
    for (r = 0u; r < (uint8_t)(n + 2u); r++) { // world rows cam_ty-1 .. cam_ty+n (N/S halo), edge-clamped
        uint8_t y = (cam_ty == 0u && r == 0u) ? 0u : (uint8_t)(cam_ty - 1u + r);
        uint16_t rowoff;
        uint8_t b0, b1, w;
        if (y > (uint8_t)(MAP_H - 1u)) y = (uint8_t)(MAP_H - 1u); // halo rows off-map: value unused (border is ocean)
        rowoff = (uint16_t)y * OW_ROW_BYTES;
        b0 = wram2_read_byte((uint16_t)(OW_MASK_WATER + rowoff + bl));
        b1 = (br != bl) ? wram2_read_byte((uint16_t)(OW_MASK_WATER + rowoff + br)) : b0;
        w = 0u;
        if (b0 & ow_bitmask[xl & 7u])                     w |= 1u;
        if (((bm == bl) ? b0 : b1) & ow_bitmask[mx & 7u]) w |= 2u;
        if (b1 & ow_bitmask[xr & 7u])                     w |= 4u;
        owb_w3[r] = w;
    }
    for (r = 0u; r < n; r++) {
        uint8_t y = (uint8_t)(cam_ty + r);
        owb_rd[r] = (uint8_t)(wram2_read_byte((uint16_t)(OW_MASK_ROAD + (uint16_t)y * OW_ROW_BYTES + bm))
                              & ow_bitmask[mx & 7u]);
    }
    for (r = 0u; r < n; r++) {
        uint8_t my = (uint8_t)(cam_ty + r);
        uint16_t idx = TILE_IDX(mx, my);
        uint8_t t = !BIT_GET(floor_bits, idx) ? TILE_WALL
                  : (BIT_GET(pit_bits, idx) ? TILE_PIT : TILE_FLOOR);
        uint8_t mid = owb_w3[r + 1u];
        uint8_t wb = 0u;
        if (mid & 2u)            wb |= OWB_C;
        if (owb_w3[r] & 2u)      wb |= OWB_N;
        if (owb_w3[r + 2u] & 2u) wb |= OWB_S;
        if (mid & 4u)            wb |= OWB_E;
        if (mid & 1u)            wb |= OWB_W;
        if (owb_rd[r])           wb |= OWB_ROAD;
        render_strip_tiles[r] = ow_cell_batch(mx, my, t, wb);
        render_strip_attrs[r] = owb_attr;
    }
}

BANKREF(overworld_classify_row_strip)
void overworld_classify_row_strip(uint8_t my, uint8_t cam_tx) BANKED {
    const uint8_t n = (uint8_t)(GRID_W + 1u);
    uint8_t x0 = (cam_tx > 0u) ? (uint8_t)(cam_tx - 1u) : 0u;
    uint8_t x1 = (uint8_t)(cam_tx + n); // E halo of the last cell
    uint8_t base, nb, r, b;
    if (x1 > (uint8_t)(MAP_W - 1u)) x1 = (uint8_t)(MAP_W - 1u);
    base = (uint8_t)(x0 >> 3);
    nb   = (uint8_t)((uint8_t)(x1 >> 3) - base + 1u); // ≤ 4 (23-column span)
    ow_prepare();
    ow_filter_features(cam_tx, my, (uint8_t)(cam_tx + n - 1u), my);
    for (r = 0u; r < 3u; r++) { // mask rows my-1, my, my+1, edge-clamped
        uint8_t y = (my == 0u && r == 0u) ? 0u : (uint8_t)(my - 1u + r);
        uint16_t off;
        if (y > (uint8_t)(MAP_H - 1u)) y = (uint8_t)(MAP_H - 1u);
        off = (uint16_t)(OW_MASK_WATER + (uint16_t)y * OW_ROW_BYTES + base);
        for (b = 0u; b < nb; b++) owb_wrow[r][b] = wram2_read_byte((uint16_t)(off + b));
    }
    {
        uint16_t off = (uint16_t)(OW_MASK_ROAD + (uint16_t)my * OW_ROW_BYTES + base);
        for (b = 0u; b < nb; b++) owb_rrow[b] = wram2_read_byte((uint16_t)(off + b));
    }
    for (r = 0u; r < n; r++) {
        uint8_t mx = (uint8_t)(cam_tx + r);
        uint8_t xw = (mx > 0u) ? (uint8_t)(mx - 1u) : 0u;
        uint8_t xe = (mx < (uint8_t)(MAP_W - 1u)) ? (uint8_t)(mx + 1u) : mx;
        uint8_t bm = (uint8_t)((uint8_t)(mx >> 3) - base);
        uint16_t idx = TILE_IDX(mx, my);
        uint8_t t = !BIT_GET(floor_bits, idx) ? TILE_WALL
                  : (BIT_GET(pit_bits, idx) ? TILE_PIT : TILE_FLOOR);
        uint8_t mkc = ow_bitmask[mx & 7u];
        uint8_t wb = 0u;
        if (owb_wrow[1][bm] & mkc) wb |= OWB_C;
        if (owb_wrow[0][bm] & mkc) wb |= OWB_N;
        if (owb_wrow[2][bm] & mkc) wb |= OWB_S;
        if (owb_wrow[1][(uint8_t)((uint8_t)(xe >> 3) - base)] & ow_bitmask[xe & 7u]) wb |= OWB_E;
        if (owb_wrow[1][(uint8_t)((uint8_t)(xw >> 3) - base)] & ow_bitmask[xw & 7u]) wb |= OWB_W;
        if (owb_rrow[bm] & mkc) wb |= OWB_ROAD;
        render_strip_tiles[r] = ow_cell_batch(mx, my, t, wb);
        render_strip_attrs[r] = owb_attr;
    }
}

// Strip blit helpers, placed in bank 22 to relieve the near-full render bank 2 (declared in render.h).
// They bulk-write render.c's classified strip buffers into one BG column/row, splitting at the 32-tile
// ring wrap (set_bkg_tiles does not wrap the 32×32 map; spilling past row/col 31 corrupts the WIN map).
BANKREF(render_blit_strip_col)
void render_blit_strip_col(uint8_t vx, uint8_t vy0, uint8_t n) BANKED {
    uint8_t first = (uint8_t)(32u - vy0);
    if (first >= n) {
        set_bkg_tiles(vx, vy0, 1u, n, render_strip_tiles);
        set_bkg_attributes(vx, vy0, 1u, n, render_strip_attrs);
    } else {
        uint8_t rem = (uint8_t)(n - first);
        set_bkg_tiles(vx, vy0, 1u, first, render_strip_tiles);
        set_bkg_attributes(vx, vy0, 1u, first, render_strip_attrs);
        set_bkg_tiles(vx, 0u, 1u, rem, render_strip_tiles + first);
        set_bkg_attributes(vx, 0u, 1u, rem, render_strip_attrs + first);
    }
    VBK_REG = 0;
}

BANKREF(render_blit_strip_row)
void render_blit_strip_row(uint8_t vy, uint8_t vx0, uint8_t n) BANKED {
    uint8_t first = (uint8_t)(32u - vx0);
    if (first >= n) {
        set_bkg_tiles(vx0, vy, n, 1u, render_strip_tiles);
        set_bkg_attributes(vx0, vy, n, 1u, render_strip_attrs);
    } else {
        uint8_t rem = (uint8_t)(n - first);
        set_bkg_tiles(vx0, vy, first, 1u, render_strip_tiles);
        set_bkg_attributes(vx0, vy, first, 1u, render_strip_attrs);
        set_bkg_tiles(0u, vy, rem, 1u, render_strip_tiles + first);
        set_bkg_attributes(0u, vy, rem, 1u, render_strip_attrs + first);
    }
    VBK_REG = 0;
}

// ── Open-sea animation ───────────────────────────────────────────────────────
// All open sea shares one VRAM tile (TILE_OVERWORLD_WATER_VRAM), so rewriting that tile's 16 bytes of
// pixel data animates EVERY water cell on screen at once — O(1), independent of how much sea is visible,
// no per-cell map writes, no slowdown. Each tick we horizontally barrel-scroll the base F10 art by one
// pixel (bits wrap, so the 8px tile still tiles seamlessly): the wave specks drift sideways like a current,
// continuously, even while standing still. water_anim_base[] is the captured base tile (filled in main.c
// while the tileset bank is paged in — reading the ROM array from this bank would need a SWITCH_ROM).
uint8_t water_anim_base[16];     // base F10 pixels, snapshotted at boot
#define WATER_ANIM_DIV_TICKS 1100u // ~0.067s per 1px drift step (DIV_REG @16384Hz)
static uint8_t  wanim_last_div;
static uint16_t wanim_acc;
static uint8_t  wanim_shift;     // 0..7 current horizontal pixel offset

BANKREF(water_anim_reset)
void water_anim_reset(void) BANKED { wanim_last_div = DIV_REG; wanim_acc = 0u; wanim_shift = 0u; }

BANKREF(water_anim_tick)
void water_anim_tick(void) BANKED { // call every gameplay frame on the overworld; cheap (DIV math) until it crosses
    uint8_t div = DIV_REG;
    wanim_acc += (uint8_t)(div - wanim_last_div); // unsigned 8-bit delta wraps correctly
    wanim_last_div = div;
    if (wanim_acc < WATER_ANIM_DIV_TICKS) return;
    wanim_acc -= WATER_ANIM_DIV_TICKS;
    wanim_shift = (uint8_t)((wanim_shift + 1u) & 7u);
    {
        uint8_t buf[16], r, s = wanim_shift;
        for (r = 0u; r < 16u; r++) { // rotate each row's bits = scroll the tile 1px horizontally, with wrap
            uint8_t b = water_anim_base[r];
            buf[r] = s ? (uint8_t)((b << s) | (b >> (uint8_t)(8u - s))) : b;
        }
        set_bkg_data(TILE_OVERWORLD_WATER_VRAM, 1u, buf); // one 16-byte VRAM write repaints all sea (caller in VBlank)
    }
}
