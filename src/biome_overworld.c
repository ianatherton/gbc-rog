#pragma bank 22

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h" // overworld_preset
#include "map.h"     // active_map_w / active_map_h
#include "render.h"  // render_strip_* buffers + render_blit_strip_* (placed here to relieve bank 2)
#include <gb/cgb.h>

// Top-level hub floor (floor 0). No enemy roster, no items (the item scatter loop in
// map.c is skipped for BIOME_OVERWORLD). Future "areas" would add extra transition tiles
// here, routed by a position->destination lookup feeding pending_transition.

// Dark-green field: color 0 of BG slot 0 (open field / blank cells) and of PAL_FLOOR_BG
// (E3/E4 ground deco) replace the usual black. Remaining colors mirror render.c's
// pal_default / pal_floor_deco so non-hub visuals are unchanged.
static const palette_color_t pal_overworld_field[] = {
    RGB(12, 23, 5), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
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
#define OW_COAST_GRASS_BAND 2

// Per-preset invariants, computed once by ow_prepare() instead of being re-derived (with a modulo)
// for every one of the ~8.8k cells. ow_water() then reads these — no multiplies, no modulo.
static uint8_t  ow_prep_preset = 0xFFu, ow_prep_w = 0u, ow_prep_h = 0u;
static uint8_t  ow_cx, ow_cy, ow_land_thresh, ow_nlakes, ow_nrivers;
static uint8_t  ow_lake_x[3], ow_lake_y[3];
static uint16_t ow_lake_r2[3];
static uint16_t ow_lake_rb2[3]; // (rad + OW_COAST_GRASS_BAND)^2 — banded radius for ow_near_water()
static uint8_t  ow_river_col[3];
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
    for (y = 1u; y < (uint8_t)(active_map_h - 1u); y++)
        for (x = 1u; x < (uint8_t)(active_map_w - 1u); x++)
            if (!ow_water(x, y)) BIT_SET(floor_bits, TILE_IDX(x, y));
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
    // preset 0 (least water) widens the sand: 19/32 vs 20/32 (== legacy 5/8) → ~+17% area.
    uint16_t thresh = ((uint16_t)active_map_w + (uint16_t)active_map_h) * (overworld_preset == 0u ? 19u : 20u) / 32u;
    uint8_t  jitter = (uint8_t)(((uint8_t)(mx * 7u) ^ (uint8_t)(my * 13u)) & 3u); // ragged edge, +0..3 tiles
    if ((sum + jitter) < thresh) return 0u;       // outside the SE sand region
    return ow_near_water(mx, my) ? 0u : 1u;       // grass band hugs the coast
}

static uint8_t ow_snow(uint8_t mx, uint8_t my) { // hub NW corner: snowfield on the freed PAL_WALL_BG slot
    uint16_t sum = (uint16_t)mx + (uint16_t)my;
    uint16_t thresh = ((uint16_t)active_map_w + (uint16_t)active_map_h) / 3u; // ~20% less area than 3/8 (corner ∝ thresh^2)
    uint8_t  jitter = (uint8_t)(((uint8_t)(mx * 11u) ^ (uint8_t)(my * 5u)) & 3u); // ragged edge
    if (sum > (uint16_t)(thresh + jitter)) return 0u; // outside the NW snow region
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
static uint8_t ow_coast(uint8_t mx, uint8_t my) { // assumes ow_prepare() ran
    uint8_t wn = ow_water(mx, (uint8_t)(my - 1u)); // water to the north?
    uint8_t ws = ow_water(mx, (uint8_t)(my + 1u));
    uint8_t we = ow_water((uint8_t)(mx + 1u), my);
    uint8_t ww = ow_water((uint8_t)(mx - 1u), my);
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

// Prefab tile lookup: which VRAM tile to draw for local cell (lx,ly) of a w×h feature of the given type.
// Returns 0 for the town's interior cell (a grass courtyard — caller draws normal ground). Town wall ring
// uses corner / E-W / N-S art; waypoint is a 2×2 of distinct quadrants; entrance is its single mouth tile.
static uint8_t ow_prefab_vram(uint8_t type, uint8_t lx, uint8_t ly, uint8_t w, uint8_t h) {
    if (type == OW_FEAT_ENTRANCE) return PREFAB_VRAM_ENTRANCE;
    if (type == OW_FEAT_WAYPOINT) {
        if (ly == 0u) return (lx == 0u) ? PREFAB_VRAM_WP_TL : PREFAB_VRAM_WP_TR;
        return (lx == 0u) ? PREFAB_VRAM_WP_BL : PREFAB_VRAM_WP_BR;
    }
    { // OW_FEAT_TOWN — 3×3 wall ring, grass centre
        uint8_t edge_x = (lx == 0u || lx == (uint8_t)(w - 1u));
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
        uint8_t fi;
        for (fi = 0u; fi < ow_feature_count; fi++) {
            uint8_t fx = ow_features[fi].x, fy = ow_features[fi].y;
            const OwPrefabDef *d = &ow_prefab_defs[ow_features[fi].type];
            if (mx >= fx && mx < (uint8_t)(fx + d->w) && my >= fy && my < (uint8_t)(fy + d->h)) {
                uint8_t v = ow_prefab_vram(ow_features[fi].type, (uint8_t)(mx - fx), (uint8_t)(my - fy),
                                           d->w, d->h);
                if (v) { *pal_out = PAL_FLOOR_BG; return v; } // grey wall art over the green field (idx0)
                break; // town courtyard centre → fall through to grass ground
            }
        }
    }

    if (base_tile == TILE_WALL) {
        if (ow_water(mx, my)) { *pal_out = PAL_PILLAR_BG; return TILE_OVERWORLD_WATER_VRAM; } // open sea (sparkle overlay flips a few cells per step — render.c)
        *pal_out = (region == OW_REGION_SNOW)   ? PAL_WALL_BG    // frosted tree
                 : (region == OW_REGION_DESERT) ? PAL_OW_ACCENT  // sandy mound
                 :                                PAL_OW_FOLIAGE; // green pine
        return TILE_OVERWORLD_WALL_VRAM;
    }
    if (base_tile == TILE_FLOOR) {
        uint8_t coast = ow_coast(mx, my); // shore tile when this land cell borders water
        if (coast) { *pal_out = PAL_PILLAR_BG; return coast; } // green land bulk + blue shore edge
    }
    return 0u; // interior ground (or a visible pit/other tile) — caller draws via its own path
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
