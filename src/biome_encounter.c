#pragma bank 23

#include "biome.h"
#include "biome_encounter.h"
#include "render.h"    // encounter_palettes_apply (bank 22)
#include "enemy.h"
#include "defs.h"
#include "globals.h"
#include "map.h"
#include "dungeon.h"
#include "lcd.h"            // lcd_note_bkg0 — panic flash restores the live slot-0 ramp
#include "entity_sprites.h" // entity_sprites_run_barrel_poof — chest opens with the barrel poof
#include "items.h"          // town_barrel_try_drop_item — chest loot roll
#include <gb/cgb.h>
#include <gbdk/platform.h>

BANKREF_EXTERN(town_barrel_try_drop_item)

// Hub '?' encounters. Two halves live here:
//
// 1. MARKERS (hub, floor 0). '?' sprites scattered over the continent — most stand still, some
//    drift a lazy random walk, none ever pursue. The set is a pure hash of (run_seed, world_tick,
//    region) and world_tick increments on every hub entry, so it costs no storage across floors:
//    the marker you just entered is simply gone next time, and fresh ones have appeared. Positions
//    for the current visit ride the hub's always-empty enemy arrays (the hub spawns nothing —
//    enemy.c spawn_enemies early-returns on FLOORKIND_HUB), with num_enemies deliberately left at
//    0 so combat / move_enemies / enemy_occ never see them; enc_marker_count is the live count.
//    Same idle-resource borrow as the town villagers' OAM run and the roof mask.
//
// 2. INTERIORS (floor ENCOUNTER_FLOOR = 49, one slot reused by every encounter). An open outdoor
//    square, 24..50 a side, fully lit, with NO wall ring — walking onto any border cell prompts
//    you back out (zone_confirm_at, bank 30). Contents come from an ENC_DEFS row; terrain art,
//    palette and difficulty tier all come from enc_region, the hub region the marker stood on. The
//    two axes are deliberately independent, so every template works in all three regions.

// ── Encounter templates ──────────────────────────────────────────────────────────────────────
// Deliberately terrain-free: no row names a region. Adding an encounter is one row here.
// n_enemies is the pre-tier fodder count; enemy stats are then multiplied by zone_stat_scale.
typedef struct {
    uint8_t size;      // map side in tiles, 24..50 (must stay >= GRID_W/GRID_H — asserted below)
    uint8_t n_enemies; // fodder to spawn
    uint8_t n_barrels;
    uint8_t has_chest;
    uint8_t clutter;   // tree/rock clump attempts out of 16 — 0 = bare field
    uint8_t roster;    // ENC_ROSTER_* below
} EncounterDef;

#define ENC_ROSTER_BEASTS 0u // rats/bats/snakes/slimes — the cavern mix
#define ENC_ROSTER_UNDEAD 1u // skeletons/imps — the crypt mix
#define ENC_ROSTER_MIXED  2u

// Map sides. The camera clamps with unsigned (active_map_w - GRID_W), so a map narrower than the
// viewport underflows into a garbage scroll rather than failing loudly — every size goes through a
// named constant here so the floor is asserted once, below, instead of trusting eight literals.
#define ENC_SZ_MIN  24u
#define ENC_SZ_MAX  50u
typedef char enc_size_fits_viewport[(ENC_SZ_MIN >= GRID_W && ENC_SZ_MIN >= GRID_H
                                     && ENC_SZ_MAX <= MAP_W && ENC_SZ_MAX <= MAP_H) ? 1 : -1];

static const EncounterDef ENC_DEFS[] = {
    /* 0 CAMP     */ { 28u,          4u, 3u, 1u,  4u, ENC_ROSTER_BEASTS }, // small, loot-forward
    /* 1 NEST     */ { 32u,          8u, 1u, 0u,  9u, ENC_ROSTER_BEASTS }, // dense cover, lots of teeth
    /* 2 RUIN     */ { 36u,          5u, 4u, 1u,  6u, ENC_ROSTER_UNDEAD },
    /* 3 CACHE    */ { ENC_SZ_MIN,   2u, 6u, 1u,  2u, ENC_ROSTER_BEASTS }, // barrel field, token guard
    /* 4 WARBAND  */ { 44u,         11u, 2u, 1u,  5u, ENC_ROSTER_MIXED  }, // the big one
    /* 5 BARROW   */ { 30u,          6u, 2u, 1u,  7u, ENC_ROSTER_UNDEAD },
    /* 6 CLEARING */ { ENC_SZ_MAX,   7u, 3u, 0u, 12u, ENC_ROSTER_MIXED  }, // widest map, heaviest cover
    /* 7 WATCH    */ { 26u,          3u, 2u, 1u,  3u, ENC_ROSTER_UNDEAD },
};
#define ENC_DEF_COUNT ((uint8_t)(sizeof ENC_DEFS / sizeof ENC_DEFS[0]))

// ── Rosters ──────────────────────────────────────────────────────────────────────────────────
// The encounter keeps its own copy rather than calling biome_dungeon/crypt/cavern_copy_defs: those
// are plain functions in banks 10/11/12 and this bank cannot SWITCH_ROM to reach them (the page
// carrying the call would vanish mid-call — same constraint that keeps the boss overlay in HOME).
static const EnemyDef enc_defs_tbl[] = {
    /* 0 ENEMY_SNAKE    */ { TILE_SNAKE_1,     TILE_SNAKE_2,     4, 3, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 },
    /* 1 ENEMY_SLIME    */ { TILE_SLIME_1_OFF, TILE_SLIME_2_OFF, 2, 3, PAL_ENEMY_SNAKE,    MOVE_CHASE,  0 },
    /* 2 ENEMY_RAT      */ { TILE_RAT_OFF,     TILE_RAT_OFF,     2, 3, PAL_ENEMY_RAT,      MOVE_WANDER, 0 },
    /* 3 ENEMY_BAT      */ { TILE_BAT_1,       TILE_BAT_2,       2, 3, PAL_ENEMY_BAT,      MOVE_BLINK,  3 },
    /* 4 ENEMY_IMP      */ { TILE_MONSTER_2,   TILE_MONSTER_2,   4, 4, PAL_ENEMY_GOBLIN,   MOVE_CHASE,  0 },
    /* 5 ENEMY_SKELETON */ { TILE_SKEL_1_OFF,  TILE_SKEL_2_OFF,  6, 4, 0,                  MOVE_CHASE,  0 }, // OCP0 white/grey ramp
};
static const uint8_t enc_roster_beasts[] = { ENEMY_RAT, ENEMY_BAT, ENEMY_SNAKE, ENEMY_SLIME };
static const uint8_t enc_roster_undead[] = { ENEMY_SKELETON, ENEMY_IMP };
static const uint8_t enc_roster_mixed[]  = { ENEMY_RAT, ENEMY_SNAKE, ENEMY_SKELETON, ENEMY_IMP };

BANKREF(biome_encounter_copy_defs)
void biome_encounter_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) { // plain: biome.c maps this bank first
    const uint8_t *list;
    uint8_t n, i;
    uint8_t r = (enc_template < ENC_DEF_COUNT) ? ENC_DEFS[enc_template].roster : ENC_ROSTER_BEASTS;
    // enemy_defs[] is indexed BY TYPE ID, so every type the roster can name must be filled in.
    out[ENEMY_SNAKE]    = enc_defs_tbl[0];
    out[ENEMY_SLIME]    = enc_defs_tbl[1];
    out[ENEMY_RAT]      = enc_defs_tbl[2];
    out[ENEMY_BAT]      = enc_defs_tbl[3];
    out[ENEMY_IMP]      = enc_defs_tbl[4];
    out[ENEMY_SKELETON] = enc_defs_tbl[5];
    if (r == ENC_ROSTER_UNDEAD)     { list = enc_roster_undead; n = (uint8_t)(sizeof enc_roster_undead); }
    else if (r == ENC_ROSTER_MIXED) { list = enc_roster_mixed;  n = (uint8_t)(sizeof enc_roster_mixed);  }
    else                            { list = enc_roster_beasts; n = (uint8_t)(sizeof enc_roster_beasts); }
    for (i = 0u; i < n; i++) out_active[i] = list[i];
    *out_count = n;
}

// ── Palettes ─────────────────────────────────────────────────────────────────────────────────
// The encounter ramps live in render_palettes.c (bank 22) with the hub's and the town's, not here.
// That is deliberate: bank 22 is this project's designated home for palette data, apply_field_palette
// / apply_wall_palette must be able to re-push these ramps after a menu stomps CRAM anyway, and
// keeping one copy there means the floor-load path and the menu-restore path can never disagree.
// This row exists only so biome.c's dispatch table has something to call.
BANKREF(biome_encounter_load_palettes)
void biome_encounter_load_palettes(void) { // plain: biome.c maps this bank before calling
    encounter_palettes_apply(); // bank 22
}

// ── Deterministic sampling ───────────────────────────────────────────────────────────────────
static uint8_t eg_hash(uint8_t a, uint8_t b) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(a + 1u) * 2711u) ^ (uint16_t)((uint16_t)(b + 7u) * 947u));
    h ^= (uint16_t)(h >> 7);
    h ^= (uint16_t)(h >> 3);
    return (uint8_t)h;
}

// Local LCG, independent of rand() for the same reason the town generator keeps one: floor layout
// must not shift because unrelated gen code consumed a different number of draws from the shared
// stream. Markers use it too, so a hub revisit at the same world_tick reproduces the same set.
static uint16_t eg_rng;
static uint8_t eg_rand(void) {
    eg_rng = (uint16_t)(eg_rng * 25173u + 13849u);
    return (uint8_t)(eg_rng >> 8);
}

// 1 if (x,y) sits inside any placed hub feature's footprint (or within a 1-cell margin of it), so
// markers never bury a town door, a cave mouth or a signpost.
static uint8_t enc_cell_blocked_by_feature(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
        uint8_t fx = ow_features[i].x, fy = ow_features[i].y;
        if (x + 1u >= fx && x <= (uint8_t)(fx + d->w)
                && y + 1u >= fy && y <= (uint8_t)(fy + d->h)) return 1u;
    }
    return 0u;
}

static uint8_t enc_marker_here(uint8_t x, uint8_t y, uint8_t upto) {
    uint8_t i;
    for (i = 0u; i < upto; i++)
        if (enemy_x[i] == x && enemy_y[i] == y) return 1u;
    return 0u;
}

// ── Markers (hub only) ───────────────────────────────────────────────────────────────────────
static const uint8_t enc_region_counts[3] = { ENC_MARKERS_GRASS, ENC_MARKERS_DESERT, ENC_MARKERS_SNOW };

// The '?' glyph, hand-drawn as a 2bpp tile. It CANNOT come from the font: font_load (main.c) goes
// through set_bkg_data, and in this project BG and OBJ tile space are not the same memory — that is
// why main.c uploads every shared tile twice, once with set_bkg_data and again with set_sprite_data
// ("same tile for OAM"). Pointing an OBJ at the font's '?' tile index just draws whatever happens to
// live at that OBJ address, which came out blank.
//
// Drawn as a bright body (colour index 3) with a dark drop-shadow one pixel down-right (index 1).
// The shadow is what makes it legible in every region: index 3 of the hub's OCP7 is gold, which
// pops on grass and sand but would wash out against a snowfield on its own. Index 0 stays
// transparent so the glyph floats on the terrain instead of sitting in a box.
//   plane0 = body | shadow, plane1 = body  ->  body reads 3, shadow reads 1.
static const uint8_t enc_marker_glyph[16] = {
    0x3Cu, 0x3Cu, // ..####..
    0x7Eu, 0x66u, // .##..##.
    0x37u, 0x06u, // .....##.
    0x0Fu, 0x0Cu, // ....##..
    0x1Eu, 0x18u, // ...##...
    0x0Cu, 0x00u, // ........
    0x18u, 0x18u, // ...##...
    0x0Cu, 0x00u, // ........
};

BANKREF(encounter_markers_build)
void encounter_markers_build(void) BANKED {
    uint8_t region, n = 0u;
    // Upload into OBJ-only VRAM (see ENC_MARKER_TILE in defs.h). Nothing else can touch that range,
    // so unlike a borrowed 128-255 slot this needs no off-hub restore and can never collide with the
    // hub's prefab BG art. Re-uploading per hub gen is harmless and keeps it off bank 0's budget.
    set_sprite_data(ENC_MARKER_TILE, 1u, enc_marker_glyph);
    eg_rng = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(world_tick + 1u) * 40503u));
    if (!eg_rng) eg_rng = 0xACE1u;
    for (region = 0u; region < 3u; region++) {
        uint8_t want = enc_region_counts[region], placed = 0u, attempt;
        for (attempt = 0u; attempt < 200u && placed < want && n < MAX_ENC_MARKERS; attempt++) {
            uint8_t x = (uint8_t)(2u + (uint8_t)(eg_rand() % (uint8_t)(active_map_w - 4u)));
            uint8_t y = (uint8_t)(2u + (uint8_t)(eg_rand() % (uint8_t)(active_map_h - 4u)));
            uint16_t idx = TILE_IDX(x, y);
            uint8_t r;
            if (!BIT_GET(floor_bits, idx)) continue;              // sea, tree clump or mountain
            if (road_bit(idx)) continue;                          // keep the roads clean
            if (enc_cell_blocked_by_feature(x, y)) continue;
            if (enc_marker_here(x, y, n)) continue;
            r = overworld_is_snow(x, y) ? OW_REGION_SNOW
              : overworld_is_desert(x, y) ? OW_REGION_DESERT : OW_REGION_GRASS;
            if (r != region) continue;
            enemy_x[n] = x;
            enemy_y[n] = y;
            // Low bits = template row; high bit = "drifts". Roughly 1 in 4 moves, and it is hashed
            // (not drawn from the LCG) so a marker's mobility is a property of the marker itself.
            enemy_type[n] = (uint8_t)((eg_rand() % ENC_DEF_COUNT)
                                      | (((eg_hash((uint8_t)(world_tick + n), 3u) & 3u) == 0u) ? ENC_MOVES_FLAG : 0u));
            n++;
            placed++;
        }
    }
    enc_marker_count = n;
}

BANKREF(encounter_marker_at)
uint8_t encounter_marker_at(uint8_t x, uint8_t y) BANKED { // marker ordinal, or 255
    uint8_t i;
    for (i = 0u; i < enc_marker_count; i++)
        if (enemy_x[i] == x && enemy_y[i] == y) return i;
    return 255u;
}

// One lazy random step for the drifting markers, called once per player turn. Deliberately blind to
// the player: markers never approach, so crossing the continent is never a chase.
BANKREF(encounter_markers_tick)
void encounter_markers_tick(uint8_t px, uint8_t py) BANKED {
    uint8_t i;
    for (i = 0u; i < enc_marker_count; i++) {
        uint8_t x, y, d;
        if (!(enemy_type[i] & ENC_MOVES_FLAG)) continue;
        if ((rand() & 3u) != 0u) continue; // ~1 step every 4 turns — a drift, not a patrol
        x = enemy_x[i];
        y = enemy_y[i];
        d = (uint8_t)(rand() & 3u);
        if      (d == 0u && y > 2u) y--;
        else if (d == 1u && y < (uint8_t)(active_map_h - 3u)) y++;
        else if (d == 2u && x > 2u) x--;
        else if (d == 3u && x < (uint8_t)(active_map_w - 3u)) x++;
        else continue;
        if (x == px && y == py) continue;
        if (!BIT_GET(floor_bits, TILE_IDX(x, y))) continue;
        if (road_bit(TILE_IDX(x, y))) continue;
        if (enc_cell_blocked_by_feature(x, y)) continue;
        if (encounter_marker_at(x, y) != 255u) continue;
        enemy_x[i] = x;
        enemy_y[i] = y;
    }
}

// Latch everything the encounter floor needs before the port transition tears the hub down. The
// marker itself is not removed: world_tick++ on the way back rerolls the whole set anyway.
BANKREF(encounter_enter)
void encounter_enter(uint8_t ord) BANKED {
    if (ord >= enc_marker_count) return;
    enc_return_x = enemy_x[ord];
    enc_return_y = enemy_y[ord];
    enc_template = (uint8_t)(enemy_type[ord] & (uint8_t)~ENC_MOVES_FLAG);
    if (enc_template >= ENC_DEF_COUNT) enc_template = 0u;
    enc_region = overworld_is_snow(enc_return_x, enc_return_y) ? OW_REGION_SNOW
               : overworld_is_desert(enc_return_x, enc_return_y) ? OW_REGION_DESERT
               : OW_REGION_GRASS;
}

// ── Interior generation ──────────────────────────────────────────────────────────────────────
// Enemy count and tier both read from here so map_gen/enemy stay thin.
BANKREF(encounter_enemy_cap)
uint8_t encounter_enemy_cap(void) BANKED {
    return (enc_template < ENC_DEF_COUNT) ? ENC_DEFS[enc_template].n_enemies : 4u;
}

static void enc_place_prop(uint8_t type, uint8_t aux, uint8_t *n) {
    uint8_t attempt;
    for (attempt = 0u; attempt < 60u; attempt++) {
        uint8_t x = (uint8_t)(1u + (uint8_t)(eg_rand() % (uint8_t)(active_map_w - 2u)));
        uint8_t y = (uint8_t)(1u + (uint8_t)(eg_rand() % (uint8_t)(active_map_h - 2u)));
        uint16_t idx = TILE_IDX(x, y);
        uint8_t dx = (x > player_spawn_x) ? (uint8_t)(x - player_spawn_x) : (uint8_t)(player_spawn_x - x);
        uint8_t dy = (y > player_spawn_y) ? (uint8_t)(y - player_spawn_y) : (uint8_t)(player_spawn_y - y);
        uint8_t i, clash = 0u;
        if (!BIT_GET(floor_bits, idx)) continue;
        if (dx < 3u && dy < 3u) continue; // keep the landing clearing free
        for (i = 0u; i < *n; i++)
            if (ow_features[i].x == x && ow_features[i].y == y) { clash = 1u; break; }
        if (clash) continue;
        if (*n >= MAX_OW_FEATURES) return;
        BIT_CLR(floor_bits, idx); // props are blocking wall cells, like the town's pines and barrels
        ow_features[*n].x = x;
        ow_features[*n].y = y;
        ow_features[*n].type = type;
        ow_features[*n].aux = aux;
        (*n)++;
        return;
    }
}

BANKREF(encounter_generate)
void encounter_generate(void) BANKED {
    const EncounterDef *def = &ENC_DEFS[(enc_template < ENC_DEF_COUNT) ? enc_template : 0u];
    uint8_t w = def->size, x, y, i, n = 0u;

    eg_rng = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(world_tick + 1u) * 26141u)
                                 ^ (uint16_t)((uint16_t)(enc_template + 1u) * 40503u));
    if (!eg_rng) eg_rng = 0xACE1u;

    active_map_w = w;
    active_map_h = w;

    // Open field, border included: an encounter has no wall ring — the border IS the exit, and
    // zone_confirm_at arms the LEAVE prompt on any border cell.
    for (y = 0u; y < w; y++)
        for (x = 0u; x < w; x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));

    // Both masks carry stale data from the last hub/town floor. road_bit is read by the renderer,
    // and lighting_reset skips the fog clear for lit floors (lighting.c), so the roof buffer would
    // otherwise still hold the last town's roofs and blank out patches of ground.
    road_clear_all();
    townroof_clear_all();

    player_spawn_x = (uint8_t)(w >> 1);
    player_spawn_y = (uint8_t)(w >> 1);

    // Cover: clumps of blocking terrain (pine / palm / snow-pine, picked by enc_region at render).
    for (i = 0u; i < (uint8_t)(def->clutter * 12u); i++) {
        uint8_t cx = (uint8_t)(1u + (uint8_t)(eg_rand() % (uint8_t)(w - 2u)));
        uint8_t cy = (uint8_t)(1u + (uint8_t)(eg_rand() % (uint8_t)(w - 2u)));
        uint8_t dx = (cx > player_spawn_x) ? (uint8_t)(cx - player_spawn_x) : (uint8_t)(player_spawn_x - cx);
        uint8_t dy = (cy > player_spawn_y) ? (uint8_t)(cy - player_spawn_y) : (uint8_t)(player_spawn_y - cy);
        if (dx < 4u && dy < 4u) continue;            // clear landing pad
        if (cx == 0u || cy == 0u || cx >= (uint8_t)(w - 1u) || cy >= (uint8_t)(w - 1u)) continue; // never block the exit ring
        if (enc_region != OW_REGION_DESERT) {
            // Grass/snow cover is the 2-tall pine: canopy on an EVEN row, trunk on the odd row below
            // (the renderer splits the halves on a bare (my & 1u), like the hub). Both cells must be
            // free and inside the exit ring, or the clump is skipped rather than half-carved.
            uint8_t ay = (uint8_t)(cy & 0xFEu);
            if (ay == 0u || (uint8_t)(ay + 1u) >= (uint8_t)(w - 1u)) continue;
            if (dx < 4u && ay <= (uint8_t)(player_spawn_y + 3u)
                    && (uint16_t)(ay + 4u) >= (uint16_t)player_spawn_y) continue; // landing pad, both rows
            if (!BIT_GET(floor_bits, TILE_IDX(cx, ay))) continue;
            if (!BIT_GET(floor_bits, TILE_IDX(cx, (uint8_t)(ay + 1u)))) continue;
            BIT_CLR(floor_bits, TILE_IDX(cx, ay));
            BIT_CLR(floor_bits, TILE_IDX(cx, (uint8_t)(ay + 1u)));
            continue;
        }
        BIT_CLR(floor_bits, TILE_IDX(cx, cy)); // desert palms stay 1 cell tall
        if ((eg_rand() & 1u) && cx + 1u < (uint8_t)(w - 1u)) BIT_CLR(floor_bits, TILE_IDX((uint8_t)(cx + 1u), cy));
    }

    // Props. Barrel ordinals are irrelevant here (encounters are one-shot, so nothing persists) —
    // aux stays 255 so town_barrel_try_break's persistence write is skipped for them.
    for (i = 0u; i < def->n_barrels; i++) enc_place_prop(OW_FEAT_BARREL, 255u, &n);
    if (def->has_chest) enc_place_prop(OW_FEAT_CHEST, 0u, &n);
    ow_feature_count = n;
}

// Chests open on a bump, like barrels, but always pay out and roll a better modifier. Same ordering
// rule as town_barrel_try_break: the feature must be gone and the cell walkable BEFORE the poof's
// busy-wait, so a mid-animation repaint can never draw the opened chest's ghost.
BANKREF(encounter_chest_open)
uint8_t encounter_chest_open(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_CHEST || ow_features[i].x != x || ow_features[i].y != y) continue;
        BIT_SET(floor_bits, TILE_IDX(x, y));
        ow_feature_count--;
        ow_features[i] = ow_features[ow_feature_count]; // swap-with-last; feature order is never meaningful
        encounter_chest_drop_item(x, y);
        entity_sprites_run_barrel_poof(x, y);
        return 1u;
    }
    return 0u;
}
