#include "map.h" // public API + defs for bitset size and tile IDs

uint8_t floor_bits[BITSET_BYTES]; // 1 = open tile (floor or pit); 0 = wall
uint8_t pit_bits[BITSET_BYTES];   // subset of floor: 1 = pit hazard
uint8_t floor_blank_bits[BITSET_BYTES]; // plain-floor deco: 1 = render as empty background (same bit layout as floor_bits)
NavNode nav_nodes[MAX_NAV_NODES]; // junction graph for enemy pathing
uint8_t num_nav_nodes;            // how many nodes after build_nav_graph
uint8_t wall_tileset_index = TILE_WALL_FIRST; // offset within sheet for TILE_WALL (debug cycle)
uint8_t wall_palette_index = 0;           // index into wall_palette_table; uploaded to PAL_WALL_BG
uint8_t player_spawn_x = MAP_W / 2;       // overwritten each generate_level(floor_seed)
uint8_t player_spawn_y = MAP_H / 2;

static uint8_t floor_column_off = TILE_COLUMN_1; // one column style per floor (D1..D4), seeded in floor_ground_init

static const int8_t NAV_DX[4] = {  0,  0, -1,  1 }; // 0=up 1=down 2=left 3=right: Δx per step
static const int8_t NAV_DY[4] = { -1,  1,  0,  0 }; // Δy per step along corridor trace

uint8_t tile_at(uint8_t x, uint8_t y) { // decode logical tile from two bitsets
    uint16_t idx = TILE_IDX(x, y); // flat index for BIT_* macros
    if (!BIT_GET(floor_bits, idx)) return TILE_WALL; // not carved → solid
    if ( BIT_GET(pit_bits,   idx)) return TILE_PIT;  // carved + pit flag
    return TILE_FLOOR;
}

void set_floor(uint8_t x, uint8_t y) { // carve walkable (clears wall for this tile)
    BIT_SET(floor_bits, TILE_IDX(x, y));
}

void set_pit(uint8_t x, uint8_t y) { // walkable hole; player falls to next floor
    uint16_t idx = TILE_IDX(x, y);
    BIT_SET(floor_bits, idx); // must remain walkable for generator and enemies until they fall
    BIT_SET(pit_bits,   idx);
}

uint8_t is_walkable(uint8_t x, uint8_t y) { // used by AI and pit checks
    return BIT_GET(floor_bits, TILE_IDX(x, y)); // pits count as walkable until movement resolves
}

char tile_char(uint8_t t) { // ASCII fallback when not using custom tile in VRAM
    if (t == TILE_WALL) return '#';
    if (t == TILE_PIT)  return '0'; // digit zero reads as pit marker in font
    return '.';                     // plain floor
}

uint8_t tile_vram_index(uint8_t t) { // non-zero → set_bkg_tiles uses ROM tile data
    if (t == TILE_WALL) return (uint8_t)(TILESET_VRAM_OFFSET + wall_tileset_index);
    if (t == TILE_PIT)  return (uint8_t)(TILESET_VRAM_OFFSET + TILE_LADDER_DOWN);
    if (t == TILE_FLOOR) return 0; // 0 = use setchar(tile_char(t)) for plain floor
    return 0;
}

uint8_t tile_palette(uint8_t t) { // CGB attribute palette index per terrain type
    if (t == TILE_WALL) return PAL_WALL_BG; // colors chosen by wall_palette_index via apply_wall_palette
    if (t == TILE_PIT)  return PAL_LADDER;  // bright ladder palette in render.c
    return 0;                               // default floor text color
}

void floor_ground_init(uint16_t floor_seed) { // deterministic floor visuals from the same seed used for level generation
    uint8_t col_idx = (uint8_t)(floor_seed & 3u); // 0..3 -> D1..D4
    floor_column_off = (uint8_t)(TILE_COLUMN_1 + (uint8_t)(col_idx * 16u));
}

uint8_t floor_tile_sheet_offset(uint8_t x, uint8_t y) { // 255 = blank (font space / tile 0)
    if (x == player_spawn_x && y == player_spawn_y) {
        return TILE_STAIRS_UP_1;
    }
    uint16_t idx = TILE_IDX(x, y);
    if (BIT_GET(floor_blank_bits, idx)) return 255u;
    return TILE_GROUND_D;
}

uint8_t floor_tile_palette_xy(uint8_t x, uint8_t y) { // pal 0 = pal_default greyscale (spawn H1 + floor)
    (void)x;
    (void)y;
    return 0;
}

uint8_t wall_tile_sheet_offset(uint8_t x, uint8_t y) { // convert specific wall neighbour counts into this floor's column tile
    uint8_t n = 0;
    if (y > 0 && tile_at(x, (uint8_t)(y - 1u)) == TILE_WALL) n++;
    if (y < (uint8_t)(MAP_H - 1u) && tile_at(x, (uint8_t)(y + 1u)) == TILE_WALL) n++;
    if (x > 0 && tile_at((uint8_t)(x - 1u), y) == TILE_WALL) n++;
    if (x < (uint8_t)(MAP_W - 1u) && tile_at((uint8_t)(x + 1u), y) == TILE_WALL) n++;
    if (n == 0u || n == 2u || n == 3u) return floor_column_off; // alone / in 2s / in 3s
    return wall_tileset_index;
}

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

    for (y = 1; y < MAP_H-1 && num_nav_nodes < MAX_NAV_NODES; y++) { // skip map border (always wall)
        for (x = 1; x < MAP_W-1 && num_nav_nodes < MAX_NAV_NODES; x++) {
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
                if (ncx <= 0 || ncx >= MAP_W-1 || ncy <= 0 || ncy >= MAP_H-1) break; // stay off outer wall
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

uint8_t nearest_nav_node(uint8_t x, uint8_t y) { // map any tile to closest junction for routing
    uint8_t best = NAV_NO_LINK, best_dist = 255, i;
    for (i = 0; i < num_nav_nodes; i++) {
        uint8_t dx = (nav_nodes[i].x > x) ? nav_nodes[i].x - x : x - nav_nodes[i].x;
        uint8_t dy = (nav_nodes[i].y > y) ? nav_nodes[i].y - y : y - nav_nodes[i].y;
        uint8_t d  = dx + dy;
        if (d < best_dist) { best_dist = d; best = i; } // tie-break: earlier index wins
    }
    return best;
}

uint8_t nav_next_step(uint8_t from, uint8_t to) { // BFS on sparse graph; first hop toward `to`
    uint8_t visited[MAX_NAV_NODES];
    uint8_t parent[MAX_NAV_NODES];
    uint8_t queue[MAX_NAV_NODES];
    uint8_t qhead = 0, qtail = 0, i;

    if (from == NAV_NO_LINK || to == NAV_NO_LINK || from == to) return NAV_NO_LINK;

    for (i = 0; i < num_nav_nodes; i++) { visited[i] = 0; parent[i] = NAV_NO_LINK; }
    visited[from]   = 1;
    queue[qtail++]  = from;

    while (qhead < qtail) {
        uint8_t cur = queue[qhead++];
        if (cur == to) break;
        for (i = 0; i < 4; i++) {
            uint8_t nb = nav_nodes[cur].adj[i]; // corridor-adjacent node index
            if (nb == NAV_NO_LINK || visited[nb]) continue;
            visited[nb]    = 1;
            parent[nb]     = cur; // reconstruct path
            queue[qtail++] = nb;
            if (qtail >= MAX_NAV_NODES) goto bfs_done; // queue full → abort search safely
        }
    }
bfs_done:

    if (!visited[to]) return NAV_NO_LINK; // disconnected components

    uint8_t step = to; // walk parent pointers from goal toward start
    while (step != NAV_NO_LINK && parent[step] != from)
        step = parent[step];
    return step; // immediate neighbour of `from` on a shortest path, or NAV_NO_LINK
}

void generate_level(uint16_t floor_seed) { // full regen: clears map, walks, pits, then nav graph
    uint16_t i;
    uint32_t mix = (uint32_t)floor_seed * 2654435761u ^ (uint32_t)floor_seed << 13;
    mix ^= mix >> 17;
    mix *= 2246523629u;
    {
        uint8_t span_x = (uint8_t)(MAP_W - 2u), span_y = (uint8_t)(MAP_H - 2u);
        player_spawn_x = (uint8_t)(1u + (uint8_t)(mix % span_x));
        mix     = mix * 1597334677u + 1u;
        player_spawn_y = (uint8_t)(1u + (uint8_t)(mix % span_y));
    }

    uint8_t x = player_spawn_x, y = player_spawn_y; // drunkard starts at deterministic spawn

    for (i = 0; i < BITSET_BYTES; i++) { floor_bits[i] = 0; pit_bits[i] = 0; floor_blank_bits[i] = 0; } // all wall; no blank scatter yet

    set_floor(x, y); // ensure spawn is open
    for (i = 0; i < WALK_STEPS; i++) {
        uint8_t d  = rand() >> 6; // use top bits of rand(); low bits are weak on this LCG
        uint8_t nx = x, ny = y;
        if      (d == 0) ny = y > 1           ? y - 1 : y; // stay one tile inside border
        else if (d == 1) ny = y < MAP_H - 2   ? y + 1 : y;
        else if (d == 2) nx = x > 1           ? x - 1 : x;
        else             nx = x < MAP_W - 2   ? x + 1 : x;
        set_floor(nx, ny);
        x = nx; y = ny; // wander
    }

    uint8_t placed = 0;
    for (uint8_t attempts = 0; attempts < 200 && placed < NUM_PITS; attempts++) { // random floor tile, not spawn
        uint8_t tx = (uint8_t)(rand() & (MAP_W - 1));
        uint8_t ty = (uint8_t)(rand() & (MAP_H - 1));
        if ((tx != player_spawn_x || ty != player_spawn_y) // never ladder on spawn
                && BIT_GET(floor_bits, TILE_IDX(tx, ty))
                && !BIT_GET(pit_bits,  TILE_IDX(tx, ty))) {
            set_pit(tx, ty);
            placed++;
        }
    }
    if (placed < NUM_PITS) { // guarantee NUM_PITS ladders if RNG never hit an open tile
        uint8_t fx, fy;
        for (fy = 0; fy < MAP_H && placed < NUM_PITS; fy++) {
            for (fx = 0; fx < MAP_W && placed < NUM_PITS; fx++) {
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
        uint8_t bx, by; // ~10% of plain floor tiles → floor_blank_bits (same indices as floor_bits)
        for (by = 0; by < MAP_H; by++) {
            for (bx = 0; bx < MAP_W; bx++) {
                uint16_t idx = TILE_IDX(bx, by);
                if (!BIT_GET(floor_bits, idx)) continue;
                if (BIT_GET(pit_bits, idx)) continue;
                if (bx == player_spawn_x && by == player_spawn_y) continue; // keep spawn tile as stair graphic
                if ((uint8_t)(rand() % 10u) == 0u) BIT_SET(floor_blank_bits, idx);
            }
        }
    }

    build_nav_graph(); // enemies need graph after geometry is known
}

