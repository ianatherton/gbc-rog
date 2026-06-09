#pragma bank 10

#include "map.h"
#include "globals.h"
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

BANKREF(generate_level)
void generate_level(uint16_t floor_seed) BANKED { // full regen: clears map, walks, pits, then nav graph
    uint16_t i;
    uint32_t mix = (uint32_t)floor_seed * 2654435761u ^ (uint32_t)floor_seed << 13;
    mix ^= mix >> 17;
    mix *= 2246523629u;
    if (floor_num == 1u) {
        active_map_w = 20u;
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
    pit_present = 0u;

    set_floor(x, y); // ensure spawn is open
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

    {
        uint8_t target_count;
        uint16_t attempts = 0u;
        if (floor_num == 1u) target_count = 4u;
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

    build_nav_graph(); // enemies need graph after geometry is known
}
