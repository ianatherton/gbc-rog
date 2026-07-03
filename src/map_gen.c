#pragma bank 10

#include "map.h"
#include "globals.h"
#include "biome.h" // overworld_water_at (bank 22, BANKED) — hub continent carve
#include <rand.h>

BANKREF_EXTERN(is_walkable) // build_nav_graph traces corridors via is_walkable (bank 2, BANKED)

extern uint8_t pit_x, pit_y, pit_present; // owned by map.c; written here, read via map_pit_position

// Duplicate of static in map.c; both read the same HOME globals.
// Kept static here to avoid a cross-bank call per iteration inside the generation loops.
static uint8_t brazier_index_at(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0u; i < brazier_count; i++)
        if (brazier_x[i] == x && brazier_y[i] == y) return i;
    return 255u;
}

static void set_floor(uint8_t x, uint8_t y) {
    BIT_SET(floor_bits, TILE_IDX(x, y));
}

static void set_pit(uint8_t x, uint8_t y) {
    uint16_t idx = TILE_IDX(x, y);
    BIT_SET(floor_bits, idx);
    BIT_SET(pit_bits,   idx);
    pit_x = x;
    pit_y = y;
    pit_present = 1u;
}

static const int8_t NAV_DX[4] = {  0,  0, -1,  1 }; // 0=up 1=down 2=left 3=right: Δx per step
static const int8_t NAV_DY[4] = { -1,  1,  0,  0 }; // Δy per step along corridor trace

static uint8_t is_straight_corridor(uint8_t x, uint8_t y) { // NS-only or WE-only adjacency → no junction
    uint8_t n = (y > 0       && is_walkable(x,   y-1)); // walkable north
    uint8_t s = (y < MAP_H-1 && is_walkable(x,   y+1));
    uint8_t w = (x > 0       && is_walkable(x-1, y  ));
    uint8_t e = (x < MAP_W-1 && is_walkable(x+1, y  ));
    return ((n && s && !w && !e) || (w && e && !n && !s)); // straight hall, not worth a node
}

static uint8_t min_dist_to_existing_node(uint8_t x, uint8_t y) { // spacing filter so nodes aren't clumped
    uint8_t i, min_d = 255;
    for (i = 0; i < num_nav_nodes; i++) {
        uint8_t dx = (nav_nodes[i].x > x) ? nav_nodes[i].x - x : x - nav_nodes[i].x; // abs without int
        uint8_t dy = (nav_nodes[i].y > y) ? nav_nodes[i].y - y : y - nav_nodes[i].y;
        uint8_t d  = dx + dy; // Manhattan
        if (d < min_d) min_d = d;
    }
    return min_d;
}

static uint8_t find_node_at(uint8_t x, uint8_t y) { // linear scan; graph is tiny (≤48 nodes)
    uint8_t i;
    for (i = 0; i < num_nav_nodes; i++)
        if (nav_nodes[i].x == x && nav_nodes[i].y == y) return i;
    return NAV_NO_LINK;
}

static void build_nav_graph(void) { // run after floor/pit layout is final
    uint8_t x, y, i, dir;
    num_nav_nodes = 0;

    for (y = 1; y < active_map_h-1 && num_nav_nodes < MAX_NAV_NODES; y++) { // skip map border (always wall)
        for (x = 1; x < active_map_w-1 && num_nav_nodes < MAX_NAV_NODES; x++) {
            if (!is_walkable(x, y))          continue;
            if (is_straight_corridor(x, y))  continue; // defer routing along halls to edge links
            if (num_nav_nodes > 0 && min_dist_to_existing_node(x, y) < NAV_MIN_SPACE) continue; // spread nodes
            nav_nodes[num_nav_nodes].x      = x;
            nav_nodes[num_nav_nodes].y      = y;
            nav_nodes[num_nav_nodes].adj[0] = NAV_NO_LINK; // filled in pass 2
            nav_nodes[num_nav_nodes].adj[1] = NAV_NO_LINK;
            nav_nodes[num_nav_nodes].adj[2] = NAV_NO_LINK;
            nav_nodes[num_nav_nodes].adj[3] = NAV_NO_LINK;
            num_nav_nodes++;
        }
    }

    for (i = 0; i < num_nav_nodes; i++) { // link nodes visible along clear corridors
        for (dir = 0; dir < 4; dir++) {
            uint8_t cx = nav_nodes[i].x;
            uint8_t cy = nav_nodes[i].y;
            uint8_t step;
            for (step = 0; step < NAV_MAX_TRACE; step++) { // cap so huge rooms don't scan forever
                int16_t ncx = (int16_t)cx + NAV_DX[dir]; // next tile in this direction
                int16_t ncy = (int16_t)cy + NAV_DY[dir];
                if (ncx <= 0 || ncx >= active_map_w-1 || ncy <= 0 || ncy >= active_map_h-1) break; // stay off outer wall
                cx = (uint8_t)ncx;
                cy = (uint8_t)ncy;
                if (!is_walkable(cx, cy)) break; // corridor ends at wall
                uint8_t j = find_node_at(cx, cy);
                if (j != NAV_NO_LINK && j != i) { // first other node along ray wins
                    nav_nodes[i].adj[dir] = j;
                    break;
                }
            }
        }
    }
}

// ── Overworld prefab feature placement ─────────────────────────────────────────────────────────
// Seed-stable (hashed from run_seed, NOT rand()) so the hub regenerates the same towns/waypoints/
// entrances when the player returns from a sub-map. Each of the three hub regions (grass/desert/snow)
// receives 1 town + 3 entrances; then a waypoint is seated within 1 screen of every town and entrance.
// Footprints stay walkable land; overworld_cell_render (bank 22) draws the prefab art over them.
static uint8_t ow_feat_hash(uint8_t a, uint8_t b) { // run_seed-keyed, position-independent
    uint8_t h = (uint8_t)((uint8_t)(a * 73u) ^ (uint8_t)(b * 151u)
              ^ (uint8_t)(run_seed & 0xFFu) ^ (uint8_t)((uint8_t)(run_seed >> 8) * 89u));
    h ^= (uint8_t)(h >> 3);
    h = (uint8_t)(h * 17u);
    h ^= (uint8_t)(h >> 5);
    return h;
}

static uint8_t ow_abs_diff(uint8_t a, uint8_t b) { return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a); }

static uint8_t ow_region_at(uint8_t cx, uint8_t cy) { // OW_REGION_* for a hub cell (banked region tests, bank 22)
    if (overworld_is_snow(cx, cy))   return OW_REGION_SNOW;
    if (overworld_is_desert(cx, cy)) return OW_REGION_DESERT;
    return OW_REGION_GRASS;
}

static uint8_t ow_footprint_clear(uint8_t fx, uint8_t fy, uint8_t w, uint8_t h) {
    uint8_t dx, dy;
    for (dy = 0u; dy < h; dy++)
        for (dx = 0u; dx < w; dx++) {
            uint8_t cx = (uint8_t)(fx + dx), cy = (uint8_t)(fy + dy);
            uint16_t idx = TILE_IDX(cx, cy);
            if (!BIT_GET(floor_bits, idx)) return 0u; // open land only (excludes water + tree-cleared cells)
            if (BIT_GET(pit_bits, idx))    return 0u; // never cover the down-ladder
            uint8_t adx = (cx > player_spawn_x) ? (uint8_t)(cx - player_spawn_x) : (uint8_t)(player_spawn_x - cx);
            uint8_t ady = (cy > player_spawn_y) ? (uint8_t)(cy - player_spawn_y) : (uint8_t)(player_spawn_y - cy);
            if (adx <= 3u && ady <= 3u)    return 0u; // keep a clearing around spawn
        }
    return 1u;
}

static uint8_t ow_footprint_in_region(uint8_t fx, uint8_t fy, uint8_t w, uint8_t h, uint8_t region) {
    uint8_t dx, dy;
    for (dy = 0u; dy < h; dy++)
        for (dx = 0u; dx < w; dx++)
            if (ow_region_at((uint8_t)(fx + dx), (uint8_t)(fy + dy)) != region) return 0u;
    return 1u;
}

static uint8_t ow_overlaps_feature(uint8_t fx, uint8_t fy, uint8_t w, uint8_t h) { // 1-tile margin between features
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
        uint8_t ax0 = ow_features[i].x, ay0 = ow_features[i].y;
        if (fx < (uint8_t)(ax0 + d->w + 1u) && (uint8_t)(fx + w + 1u) > ax0
                && fy < (uint8_t)(ay0 + d->h + 1u) && (uint8_t)(fy + h + 1u) > ay0) return 1u;
    }
    return 0u;
}

// True if a town footprint at (fx,fy) would sit closer than `sep` tiles (Manhattan, centre-to-centre) to
// an already-placed town. Keeps the three region towns spread across the map.
static uint8_t ow_town_too_close(uint8_t fx, uint8_t fy, uint8_t w, uint8_t h, uint8_t sep) {
    const OwPrefabDef *td = &ow_prefab_defs[OW_FEAT_TOWN];
    uint8_t cx = (uint8_t)(fx + w / 2u), cy = (uint8_t)(fy + h / 2u);
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_TOWN) continue;
        uint8_t ox = (uint8_t)(ow_features[i].x + td->w / 2u);
        uint8_t oy = (uint8_t)(ow_features[i].y + td->h / 2u);
        if ((uint8_t)(ow_abs_diff(cx, ox) + ow_abs_diff(cy, oy)) < sep) return 1u;
    }
    return 0u;
}

// Try to seat one feature of `type`. region_mode: 0 = whole footprint in `region`, 1 = center cell in
// `region`, 2 = any open land (region ignored). min_town_sep > 0 also rejects positions within that
// Manhattan distance of an existing town (towns only). `salt` distinguishes each feature's deterministic
// position sequence. Returns 1 if placed. The footprint stays walkable (validated open) — the player
// enters by stepping onto a feature cell (Part D); overworld_cell_render draws the prefab art over it.
static uint8_t ow_place_one(uint8_t type, uint8_t region, uint8_t region_mode, uint8_t min_town_sep, uint8_t salt) {
    const OwPrefabDef *d = &ow_prefab_defs[type];
    uint8_t spanx = (uint8_t)(active_map_w - d->w - 4u);
    uint8_t spany = (uint8_t)(active_map_h - d->h - 4u);
    uint8_t attempt;
    if (ow_feature_count >= MAX_OW_FEATURES) return 0u;
    for (attempt = 0u; attempt < 64u; attempt++) {
        uint8_t fx = (uint8_t)(2u + (ow_feat_hash((uint8_t)(salt + 1u), (uint8_t)(attempt * 2u + 1u)) % spanx));
        uint8_t fy = (uint8_t)(2u + (ow_feat_hash((uint8_t)(attempt * 2u + 2u), (uint8_t)(salt + 10u)) % spany));
        if (!ow_footprint_clear(fx, fy, d->w, d->h)) continue;
        if (ow_overlaps_feature(fx, fy, d->w, d->h)) continue;
        if (region_mode == 0u) {
            if (!ow_footprint_in_region(fx, fy, d->w, d->h, region)) continue;
        } else if (region_mode == 1u) {
            if (ow_region_at((uint8_t)(fx + d->w / 2u), (uint8_t)(fy + d->h / 2u)) != region) continue;
        }
        if (min_town_sep && ow_town_too_close(fx, fy, d->w, d->h, min_town_sep)) continue;
        ow_features[ow_feature_count].x = fx;
        ow_features[ow_feature_count].y = fy;
        ow_features[ow_feature_count].type = type;
        ow_feature_count++;
        return 1u;
    }
    return 0u;
}

// Seat a feature in `region`, relaxing the region constraint in graceful steps so a tight desert/snow
// corner never hangs generation: whole-footprint-in-region → center-in-region → any open land.
static void ow_place_feature(uint8_t type, uint8_t region, uint8_t salt) {
    if (ow_place_one(type, region, 0u, 0u, salt)) return;
    if (ow_place_one(type, region, 1u, 0u, salt)) return;
    ow_place_one(type, region, 2u, 0u, salt);
}

// Towns also stay ≥ MIN_TOWN_SEP_TILES from each other. Escalate region relaxation while holding the
// separation; only as an absolute last resort drop the separation so a town is always placed.
static void ow_place_town(uint8_t region, uint8_t salt) {
    if (ow_place_one(OW_FEAT_TOWN, region, 0u, MIN_TOWN_SEP_TILES, salt)) return;
    if (ow_place_one(OW_FEAT_TOWN, region, 1u, MIN_TOWN_SEP_TILES, salt)) return;
    if (ow_place_one(OW_FEAT_TOWN, region, 2u, MIN_TOWN_SEP_TILES, salt)) return;
    ow_place_feature(OW_FEAT_TOWN, region, salt); // separation infeasible this seed — place anyway
}

// Seat a feature of `type` whose entrance cell lands within (rdx,rdy) tiles of anchor (ax,ay). Candidate
// top-lefts are drawn (hashed) from a box around the anchor, clamped to the valid map range. Used to
// cluster dungeon entrances around their town and to drop a waypoint beside a town / the final dungeon.
static uint8_t ow_place_near(uint8_t type, uint8_t ax, uint8_t ay, uint8_t rdx, uint8_t rdy, uint8_t salt) {
    const OwPrefabDef *d = &ow_prefab_defs[type];
    uint8_t max_x = (uint8_t)(active_map_w - d->w - 2u);
    uint8_t max_y = (uint8_t)(active_map_h - d->h - 2u);
    uint8_t lo_x = (ax > (uint8_t)(rdx + 2u)) ? (uint8_t)(ax - rdx) : 2u;
    uint8_t lo_y = (ay > (uint8_t)(rdy + 2u)) ? (uint8_t)(ay - rdy) : 2u;
    uint8_t hi_x = (uint8_t)(ax + rdx); if (hi_x > max_x) hi_x = max_x;
    uint8_t hi_y = (uint8_t)(ay + rdy); if (hi_y > max_y) hi_y = max_y;
    uint8_t spanx = (hi_x >= lo_x) ? (uint8_t)(hi_x - lo_x + 1u) : 1u;
    uint8_t spany = (hi_y >= lo_y) ? (uint8_t)(hi_y - lo_y + 1u) : 1u;
    uint8_t attempt;
    if (ow_feature_count >= MAX_OW_FEATURES) return 0u;
    for (attempt = 0u; attempt < 64u; attempt++) {
        uint8_t fx = (uint8_t)(lo_x + (ow_feat_hash((uint8_t)(salt + 3u), (uint8_t)(attempt * 2u + 1u)) % spanx));
        uint8_t fy = (uint8_t)(lo_y + (ow_feat_hash((uint8_t)(attempt * 2u + 2u), (uint8_t)(salt + 7u)) % spany));
        if (!ow_footprint_clear(fx, fy, d->w, d->h)) continue;
        if (ow_overlaps_feature(fx, fy, d->w, d->h)) continue;
        if (ow_abs_diff((uint8_t)(fx + d->ent_dx), ax) > rdx) continue;
        if (ow_abs_diff((uint8_t)(fy + d->ent_dy), ay) > rdy) continue;
        ow_features[ow_feature_count].x = fx;
        ow_features[ow_feature_count].y = fy;
        ow_features[ow_feature_count].type = type;
        ow_feature_count++;
        return 1u;
    }
    return 0u;
}

// Anchor (walkable entrance cell) of an already-placed feature.
static void ow_feature_anchor(uint8_t i, uint8_t *ax, uint8_t *ay) {
    const OwPrefabDef *d = &ow_prefab_defs[ow_features[i].type];
    *ax = (uint8_t)(ow_features[i].x + d->ent_dx);
    *ay = (uint8_t)(ow_features[i].y + d->ent_dy);
}

// Hub feature layout: per region a town (kept far from other towns) ringed by 3 dungeon entrances with
// one waypoint beside it; then a single "final dungeon" — a 2x2 boss door placed anywhere — with its own
// waypoint. Waypoints total 1 per town + 1 for the final dungeon (= 4).
static void place_overworld_features(void) {
    static const uint8_t regions[3] = { OW_REGION_GRASS, OW_REGION_DESERT, OW_REGION_SNOW };
    uint8_t r, e, salt = 0u, ax, ay;
    ow_feature_count = 0u;
    for (r = 0u; r < 3u; r++) {
        uint8_t before = ow_feature_count;
        ow_place_town(regions[r], salt++);
        if (ow_feature_count == before) continue; // town couldn't be placed (degenerate seed) — skip region
        ow_feature_anchor((uint8_t)(ow_feature_count - 1u), &ax, &ay);
        for (e = 0u; e < 3u; e++) { // 3 dungeon entrances ringed around the town
            if (!ow_place_near(OW_FEAT_ENTRANCE, ax, ay, DUNGEON_CLUSTER_DX, DUNGEON_CLUSTER_DY, salt))
                ow_place_feature(OW_FEAT_ENTRANCE, regions[r], salt); // fall back to anywhere in-region
            salt++;
        }
        ow_place_near(OW_FEAT_WAYPOINT, ax, ay, WAYPOINT_NEAR_DX, WAYPOINT_NEAR_DY, salt++); // 1 waypoint per town
    }
    { // Final dungeon: a lone 2x2 boss door somewhere random, with a waypoint within 1 screen of it.
        uint8_t before = ow_feature_count;
        ow_place_one(OW_FEAT_BOSSDOOR, OW_REGION_GRASS, 2u, 0u, salt++); // region_mode 2 = any open land
        if (ow_feature_count > before) {
            ow_feature_anchor((uint8_t)(ow_feature_count - 1u), &ax, &ay);
            ow_place_near(OW_FEAT_WAYPOINT, ax, ay, WAYPOINT_NEAR_DX, WAYPOINT_NEAR_DY, salt++);
        }
    }
}

// Roads (rendered as open sand by overworld_cell_render, so no new art). Carve an L-shaped path —
// horizontal then vertical — setting road_bit only on floor cells, so the road gaps naturally at rivers
// and tree clumps instead of paving over them.
static void carve_road_seg(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    uint8_t x = x0, y = y0;
    while (x != x1) { if (BIT_GET(floor_bits, TILE_IDX(x, y))) road_set(TILE_IDX(x, y)); x = (uint8_t)((x < x1) ? x + 1u : x - 1u); }
    while (y != y1) { if (BIT_GET(floor_bits, TILE_IDX(x, y))) road_set(TILE_IDX(x, y)); y = (uint8_t)((y < y1) ? y + 1u : y - 1u); }
    if (BIT_GET(floor_bits, TILE_IDX(x1, y1))) road_set(TILE_IDX(x1, y1));
}

// Connect each dungeon entrance to its nearest town's door. Called on the hub after features are placed.
static void place_overworld_roads(void) {
    uint8_t i, j;
    const OwPrefabDef *td = &ow_prefab_defs[OW_FEAT_TOWN];
    road_clear_all();
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_ENTRANCE) continue;
        uint8_t ex = ow_features[i].x, ey = ow_features[i].y; // entrance is 1x1: its cell is the trigger
        uint8_t best = 255u, bestd = 0u, btx = 0u, bty = 0u;
        for (j = 0u; j < ow_feature_count; j++) {
            if (ow_features[j].type != OW_FEAT_TOWN) continue;
            uint8_t tx = (uint8_t)(ow_features[j].x + td->ent_dx), ty = (uint8_t)(ow_features[j].y + td->ent_dy);
            uint8_t dx = (uint8_t)((ex > tx) ? ex - tx : tx - ex), dy = (uint8_t)((ey > ty) ? ey - ty : ty - ey);
            uint8_t dd = (uint8_t)((dx > dy) ? dx : dy);
            if (best == 255u || dd < bestd) { bestd = dd; best = j; btx = tx; bty = ty; }
        }
        if (best != 255u) carve_road_seg(ex, ey, btx, bty);
    }
}

// Signposts: place a readable B8 marker on an open cell just outside each structure, tagged with a
// label code (SIGN_KIND_*) that overworld_signpost_read() turns into chat-box text on step.
static uint8_t ow_add_signpost(uint8_t sx, uint8_t sy, uint8_t aux) {
    if (ow_feature_count >= MAX_OW_FEATURES) return 0u;
    if (!ow_footprint_clear(sx, sy, 1u, 1u)) return 0u; // open land, not pit, off the spawn clearing
    ow_features[ow_feature_count].x = sx;
    ow_features[ow_feature_count].y = sy;
    ow_features[ow_feature_count].type = OW_FEAT_SIGNPOST;
    ow_features[ow_feature_count].aux = aux;
    ow_feature_count++;
    return 1u;
}

static void place_overworld_signposts(void) {
    uint8_t n = ow_feature_count; // snapshot: only label the structures already placed, not new signposts
    uint8_t i, townc = 0u, dungc = 0u;
    uint8_t cx = (uint8_t)(active_map_w >> 1), cy = (uint8_t)(active_map_h >> 1);
    for (i = 0u; i < n; i++) {
        uint8_t t = ow_features[i].type, fx = ow_features[i].x, fy = ow_features[i].y, aux;
        const OwPrefabDef *d = &ow_prefab_defs[t];
        if (t == OW_FEAT_TOWN)          { aux = (uint8_t)(SIGN_KIND_TOWN | (townc & 0x0Fu)); townc++; }
        else if (t == OW_FEAT_WAYPOINT) { aux = (uint8_t)(SIGN_KIND_WAYPOINT | ((fx >= cx) ? 0u : 1u) | ((fy >= cy) ? 2u : 0u)); }
        else if (t == OW_FEAT_ENTRANCE) { aux = (uint8_t)(SIGN_KIND_DUNGEON | (dungc & 0x0Fu)); dungc++; }
        else if (t == OW_FEAT_BOSSDOOR) { aux = SIGN_KIND_BOSS; }
        else continue;
        // Try a few cells around the footprint for an open spot to stand the sign.
        if (ow_add_signpost(fx, (uint8_t)(fy + d->h), aux)) continue;                       // below-left
        if (ow_add_signpost((uint8_t)(fx + d->w - 1u), (uint8_t)(fy + d->h), aux)) continue; // below-right
        if (fy >= 1u && ow_add_signpost(fx, (uint8_t)(fy - 1u), aux)) continue;              // above
        if (ow_add_signpost((uint8_t)(fx + d->w), fy, aux)) continue;                        // right
        if (fx >= 1u) ow_add_signpost((uint8_t)(fx - 1u), fy, aux);                          // left
    }
}

// Hub continent shape lives in overworld_water_at (bank 22, biome_overworld.c); generate_level calls
// it as a BANKED entry to carve land. overworld_preset (set below) selects one of 5 seeded layouts.

BANKREF(generate_level)
void generate_level(uint16_t floor_seed) BANKED { // full regen: clears map, walks, pits, then nav graph
    uint16_t i;
    uint32_t mix = (uint32_t)floor_seed * 2654435761u ^ (uint32_t)floor_seed << 13;
    mix ^= mix >> 17;
    mix *= 2246523629u;
    if (floor_num == 1u || floor_num == MINIBOSS_FLOOR_NUM || floor_num == BOSS_FLOOR_NUM || floor_num == BOSS2_FLOOR_NUM) {
        active_map_w = 20u; // entry floor (1), miniboss, both bosses: fixed compact arena
        active_map_h = 20u;
    } else {
        active_map_w = MAP_W;
        active_map_h = MAP_H;
    }
    {
        uint8_t span_x = (uint8_t)(active_map_w - 2u), span_y = (uint8_t)(active_map_h - 2u);
        player_spawn_x = (uint8_t)(1u + (uint8_t)(mix % span_x));
        mix     = mix * 1597334677u + 1u;
        player_spawn_y = (uint8_t)(1u + (uint8_t)(mix % span_y));
    }

    uint8_t x = player_spawn_x, y = player_spawn_y; // drunkard starts at deterministic spawn

    for (i = 0; i < BITSET_BYTES; i++) { floor_bits[i] = 0; pit_bits[i] = 0; } // all wall to start; visual blanking is hashed now
    brazier_count = 0u;
    ow_feature_count = 0u; // no prefab features off the hub; place_overworld_features() fills it on floor 0
    pit_present = 0u;

    set_floor(x, y); // ensure spawn is open
    if (floor_num == 0u) {
        // Hub overworld: a seeded continent (one of OVERWORLD_PRESET_COUNT layouts) surrounded by
        // ocean, with rivers and lakes carved as water. Land = !overworld_water_at; trees scatter on
        // land only. render.c recomputes the same mask to split water/trees and pick coast tiles.
        uint16_t t;
        overworld_preset = (uint8_t)(((run_seed ^ (uint16_t)(run_seed >> 7)) & 0xFFu) % OVERWORLD_PRESET_COUNT);
        overworld_carve(); // fills floor_bits for the whole landmass in one banked call (no per-cell trampoline)
        // spawn on the land cell nearest map centre (handles a lake/river sitting on centre)
        player_spawn_x = (uint8_t)(active_map_w >> 1);
        player_spawn_y = (uint8_t)(active_map_h >> 1);
        {
            uint8_t ccx = (uint8_t)(active_map_w >> 1), ccy = (uint8_t)(active_map_h >> 1);
            uint8_t rad, found = 0u;
            for (rad = 0u; rad < (uint8_t)(active_map_w >> 1) && !found; rad++) {
                int8_t ox, oy;
                for (oy = -(int8_t)rad; oy <= (int8_t)rad && !found; oy++)
                    for (ox = -(int8_t)rad; ox <= (int8_t)rad && !found; ox++) {
                        uint8_t cx2 = (uint8_t)(ccx + ox), cy2 = (uint8_t)(ccy + oy);
                        if (BIT_GET(floor_bits, TILE_IDX(cx2, cy2))) {
                            player_spawn_x = cx2; player_spawn_y = cy2; found = 1u;
                        }
                    }
            }
        }
        for (t = 0u; t < 900u; t++) { // scatter tree/mountain clumps on land only
            uint8_t tx = (uint8_t)(1u + (uint8_t)(rand() % (uint8_t)(active_map_w - 2u)));
            uint8_t ty = (uint8_t)(1u + (uint8_t)(rand() % (uint8_t)(active_map_h - 2u)));
            uint8_t adx = (tx > player_spawn_x) ? (uint8_t)(tx - player_spawn_x) : (uint8_t)(player_spawn_x - tx);
            uint8_t ady = (ty > player_spawn_y) ? (uint8_t)(ty - player_spawn_y) : (uint8_t)(player_spawn_y - ty);
            uint8_t region;
            if (adx <= 2u && ady <= 2u) continue;               // clearing around spawn
            if (!BIT_GET(floor_bits, TILE_IDX(tx, ty))) continue; // skip water — walls only on land
            // Per-region density (out of 16): snow mountains densest, desert palms sparsest, grass between.
            region = ow_region_at(tx, ty);
            {
                uint8_t keep = (region == OW_REGION_SNOW) ? 16u : (region == OW_REGION_DESERT) ? 5u : 11u;
                if ((uint8_t)(rand() & 0x0Fu) >= keep) continue;
            }
            BIT_CLR(floor_bits, TILE_IDX(tx, ty));
            // Snow always grows the clump (solid 2-wide ranges); elsewhere ~50% for scattered 2-tile clumps.
            if ((region == OW_REGION_SNOW || (rand() & 0x40u)) && (uint8_t)(tx + 1u) < (uint8_t)(active_map_w - 1u)
                    && BIT_GET(floor_bits, TILE_IDX((uint8_t)(tx + 1u), ty)))
                BIT_CLR(floor_bits, TILE_IDX((uint8_t)(tx + 1u), ty));
        }
        set_floor(player_spawn_x, player_spawn_y); // guarantee spawn open after scatter
    } else {
        for (i = 0; i < WALK_STEPS; i++) {
        uint8_t d  = rand() >> 6; // use top bits of rand(); low bits are weak on this LCG
        uint8_t nx = x, ny = y;
        if      (d == 0) ny = y > 1           ? y - 1 : y; // stay one tile inside border
        else if (d == 1) ny = y < active_map_h - 2   ? y + 1 : y;
        else if (d == 2) nx = x > 1           ? x - 1 : x;
        else             nx = x < active_map_w - 2   ? x + 1 : x;
        set_floor(nx, ny);
        x = nx; y = ny; // wander
        }
    }

    uint8_t placed = 0;
    for (uint8_t attempts = 0; attempts < 200 && placed < NUM_PITS; attempts++) { // random floor tile, not spawn
        uint8_t tx = (uint8_t)(rand() % active_map_w);
        uint8_t ty = (uint8_t)(rand() % active_map_h);
        if ((tx != player_spawn_x || ty != player_spawn_y) // never ladder on spawn
                && BIT_GET(floor_bits, TILE_IDX(tx, ty))
                && !BIT_GET(pit_bits,  TILE_IDX(tx, ty))) {
            set_pit(tx, ty);
            placed++;
        }
    }
    if (placed < NUM_PITS) { // guarantee NUM_PITS ladders if RNG never hit an open tile
        uint8_t fx, fy;
        for (fy = 0; fy < active_map_h && placed < NUM_PITS; fy++) {
            for (fx = 0; fx < active_map_w && placed < NUM_PITS; fx++) {
                uint16_t idx = TILE_IDX(fx, fy);
                if ((fx != player_spawn_x || fy != player_spawn_y)
                        && BIT_GET(floor_bits, idx)
                        && !BIT_GET(pit_bits, idx)) {
                    set_pit(fx, fy);
                    placed++;
                }
            }
        }
    }
    if (floor_num == 0u && pit_present) {
        // Put the player a few tiles off the ladder, on a land cell of the same landmass (so the
        // ladder stays reachable even when a river splits the continent). Scan rings 2..6 around it.
        uint8_t rad, placed2 = 0u;
        for (rad = 2u; rad <= 6u && !placed2; rad++) {
            int8_t ox, oy;
            for (oy = -(int8_t)rad; oy <= (int8_t)rad && !placed2; oy++)
                for (ox = -(int8_t)rad; ox <= (int8_t)rad && !placed2; ox++) {
                    if (ox > -(int8_t)rad && ox < (int8_t)rad && oy > -(int8_t)rad && oy < (int8_t)rad) continue; // ring only
                    int16_t cx2 = (int16_t)pit_x + ox, cy2 = (int16_t)pit_y + oy;
                    if (cx2 < 1 || cy2 < 1 || cx2 >= (int16_t)(active_map_w - 1u) || cy2 >= (int16_t)(active_map_h - 1u)) continue;
                    if (BIT_GET(floor_bits, TILE_IDX((uint8_t)cx2, (uint8_t)cy2))
                            && !BIT_GET(pit_bits, TILE_IDX((uint8_t)cx2, (uint8_t)cy2))) {
                        player_spawn_x = (uint8_t)cx2; player_spawn_y = (uint8_t)cy2; placed2 = 1u;
                    }
                }
        }
        set_floor(player_spawn_x, player_spawn_y);
    }

    {
        uint8_t target_count;
        uint16_t attempts = 0u;
        if (floor_num == 0u) target_count = 0u; // hub: no braziers
        else if (floor_num == 1u) target_count = 4u;
        else if (floor_num == BOSS_FLOOR_NUM) target_count = 0u; // unlit boss arena
        else {
            uint8_t base = (uint8_t)(10u + (uint8_t)(rand() % 11u)); // 10..20
            target_count = (floor_num >= base) ? 0u : (uint8_t)(base - floor_num);
        }
        if (target_count > MAX_BRAZIERS) target_count = MAX_BRAZIERS;
        while (brazier_count < target_count && attempts < (uint16_t)(80u + (uint16_t)target_count * 24u)) {
            uint8_t tx = (uint8_t)(rand() % active_map_w);
            uint8_t ty = (uint8_t)(rand() % active_map_h);
            uint16_t idx = TILE_IDX(tx, ty);
            attempts++;
            if ((tx == player_spawn_x && ty == player_spawn_y)
                    || !BIT_GET(floor_bits, idx)
                    || BIT_GET(pit_bits, idx)
                    || brazier_index_at(tx, ty) != 255u) continue;
            brazier_x[brazier_count] = tx;
            brazier_y[brazier_count] = ty;
            brazier_type[brazier_count] = (uint8_t)(rand() & 1u);
            brazier_count++;
        }
    }

    // Hub prefab structures: placed after the landmass, trees, pit and spawn are final so footprints
    // only land on clear open land away from the down-ladder and the spawn clearing.
    if (floor_num == 0u) { place_overworld_features(); place_overworld_roads(); place_overworld_signposts(); }

    // The hub has no enemies, so its nav graph is never consulted — skip the ~9k banked is_walkable
    // probes (a big chunk of floor-0 load) and just present an empty graph to any nav consumer.
    if (floor_num == 0u) num_nav_nodes = 0u;
    else                 build_nav_graph(); // enemies need graph after geometry is known
}
