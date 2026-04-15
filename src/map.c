#pragma bank 2

#include "map.h"
#include "globals.h"
#include "enemy.h"
#include "render.h"
#include "camera.h"
#include "ui.h"
#include "lcd.h"
#include "wall_palettes.h"
#include <gbdk/platform.h>
#include <rand.h>

uint8_t floor_bits[BITSET_BYTES]; // 1 = open tile (floor or pit); 0 = wall
uint8_t pit_bits[BITSET_BYTES];   // subset of floor: 1 = pit hazard
uint8_t explored_bits[BITSET_BYTES]; // fog scaffold: 1 = tile was revealed to player
NavNode nav_nodes[MAX_NAV_NODES]; // junction graph for enemy pathing
uint8_t num_nav_nodes;            // how many nodes after build_nav_graph
uint8_t wall_tileset_index = TILE_WALL_FIRST; // offset within sheet for TILE_WALL (debug cycle)
uint8_t wall_palette_index = 0;           // bulk walls → PAL_WALL_BG
uint8_t pillar_palette_index = 0;       // column deco walls → PAL_PILLAR_BG
uint8_t active_map_w = MAP_W;
uint8_t active_map_h = MAP_H;
uint8_t player_spawn_x = MAP_W / 2;       // overwritten each generate_level(floor_seed)
uint8_t player_spawn_y = MAP_H / 2;
uint8_t floor_column_off = TILE_COLUMN_1; // D-column art; floor_ground_init
static uint16_t floor_visual_seed;        // deterministic seed for floor blank-scatter hash
uint8_t brazier_count;
uint8_t brazier_x[MAX_BRAZIERS];
uint8_t brazier_y[MAX_BRAZIERS];
uint8_t brazier_type[MAX_BRAZIERS]; // 0=brazier C3/C4, 1=torch C1/C2
static uint8_t pit_x, pit_y, pit_present;

uint8_t wall_ortho_wall_count_xy(uint8_t x, uint8_t y) {
    uint8_t n = 0u;
    if (x > 0u && !BIT_GET(floor_bits, TILE_IDX((uint8_t)(x - 1u), y))) n++;
    if (x < (uint8_t)(MAP_W - 1u) && !BIT_GET(floor_bits, TILE_IDX((uint8_t)(x + 1u), y))) n++;
    if (y > 0u && !BIT_GET(floor_bits, TILE_IDX(x, (uint8_t)(y - 1u)))) n++;
    if (y < (uint8_t)(MAP_H - 1u) && !BIT_GET(floor_bits, TILE_IDX(x, (uint8_t)(y + 1u)))) n++;
    return n;
}

static uint8_t floor_tile_is_blank(uint8_t x, uint8_t y) {
    uint16_t h = (uint16_t)x * 2971u ^ (uint16_t)y * 1619u ^ floor_visual_seed;
    h ^= (uint16_t)(h >> 5);
    return (uint8_t)((h & 7u) == 0u);
}


static const int8_t NAV_DX[4] = {  0,  0, -1,  1 }; // 0=up 1=down 2=left 3=right: Δx per step
static const int8_t NAV_DY[4] = { -1,  1,  0,  0 }; // Δy per step along corridor trace

uint8_t tile_at(uint8_t x, uint8_t y) { // decode logical tile from two bitsets
    uint16_t idx = TILE_IDX(x, y); // flat index for BIT_* macros
    if (!BIT_GET(floor_bits, idx)) return TILE_WALL; // not carved → solid
    if ( BIT_GET(pit_bits,   idx)) return TILE_PIT;  // carved + pit flag
    return TILE_FLOOR;
}

static uint8_t brazier_index_at(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0u; i < brazier_count; i++)
        if (brazier_x[i] == x && brazier_y[i] == y) return i;
    return 255u;
}

void set_floor(uint8_t x, uint8_t y) { // carve walkable (clears wall for this tile)
    BIT_SET(floor_bits, TILE_IDX(x, y));
}

void set_pit(uint8_t x, uint8_t y) { // walkable hole; player falls to next floor
    uint16_t idx = TILE_IDX(x, y);
    BIT_SET(floor_bits, idx); // must remain walkable for generator and enemies until they fall
    BIT_SET(pit_bits,   idx);
    pit_x = x;
    pit_y = y;
    pit_present = 1u;
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
    floor_visual_seed = (uint16_t)(floor_seed ^ 0x6d2bu);
}

uint8_t floor_tile_sheet_offset(uint8_t x, uint8_t y) { // 255 = blank; else random E3/E4 on black field
    if (x == player_spawn_x && y == player_spawn_y) {
        return TILE_STAIRS_UP_1;
    }
    {
        uint8_t bi = brazier_index_at(x, y);
        if (bi != 255u) {
            if (brazier_type[bi] == 0u)
                return (uint8_t)((((uint8_t)(DIV_REG >> 3) + x + y) & 1u) ? TILE_LIGHT_4 : TILE_LIGHT_3); // C3/C4
            return (uint8_t)((((uint8_t)(DIV_REG >> 3) + x + y) & 1u) ? TILE_LIGHT_2 : TILE_LIGHT_1); // C1/C2
        }
    }
    if (floor_tile_is_blank(x, y)) return 255u;
    {
        static const uint8_t ground_e34[2] = { TILE_GROUND_C, TILE_GROUND_D }; // sheet E3, E4
        uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)floor_num * 131u));
        h ^= (uint16_t)((uint16_t)x * 911u ^ (uint16_t)y * 357u);
        {
            uint8_t mix = (uint8_t)h ^ (uint8_t)(h >> 8);
            return ground_e34[(uint8_t)(mix & 1u)];
        }
    }
}

uint8_t floor_tile_palette_xy(uint8_t x, uint8_t y) { // stairs + blank = B&W pal 0; E3/E4 deco = dark grey PAL_FLOOR_BG
    if (x == player_spawn_x && y == player_spawn_y) return 0u;
    if (brazier_index_at(x, y) != 255u) return (uint8_t)PAL_LADDER; // reuse fire-toned gameplay palette slot
    if (floor_tile_is_blank(x, y)) return 0u;
    return (uint8_t)PAL_FLOOR_BG;
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

BANKREF(level_generate_and_spawn)
void level_generate_and_spawn(uint8_t *px, uint8_t *py) BANKED {
    uint16_t floor_seed = (uint16_t)(run_seed * 2053u)
                        ^ (uint16_t)(floor_num * 6364u)
                        ^ 0xACE1u;
    if (!floor_seed) floor_seed = 0xACE1u;

    floor_ground_init(floor_seed);

    num_corpses       = 0;
    enemy_grids_init();
    dead_enemy_pool_count = 0;
    {
        uint8_t q;
        for (q = 0; q < MAX_ENEMIES; q++) enemy_alive[q] = 0;
    }
    enemy_anim_toggle = 0;
    enemy_anim_reset();
    wall_tileset_index = TILE_WALL_FIRST;
    {
        uint32_t m = (uint32_t)floor_seed * 2246523629u ^ (uint32_t)floor_seed << 11; // no rand() before generate_level
        uint16_t h;
        m ^= m >> 15;
        h = (uint16_t)m ^ (uint16_t)(m >> 16);
        wall_palette_index = (uint8_t)(h % NUM_WALL_PALETTES); // 16-bit % — NUM_WALL_PALETTES not Po2 (40)
        m = m * 1597334677u + 1u;
        h = (uint16_t)m ^ (uint16_t)(m >> 16);
        pillar_palette_index = (uint8_t)(h % NUM_WALL_PALETTES);
    }
    initrand(floor_seed);
    generate_level(floor_seed);
    lighting_reset();
    if (pit_present) lighting_reveal_radius(pit_x, pit_y, LIGHT_RADIUS_LADDER_DOWN);
    {
        uint8_t i;
        for (i = 0u; i < brazier_count; i++) {
            uint8_t r = (brazier_type[i] == 0u) ? LIGHT_RADIUS_BRAZIER : LIGHT_RADIUS_TORCH;
            lighting_reveal_radius(brazier_x[i], brazier_y[i], r);
        }
    }
    spawn_enemies();
    *px = player_spawn_x;
    *py = player_spawn_y;
    lighting_reveal_radius(*px, *py, LIGHT_RADIUS_STAIRS_UP);
    lighting_reveal_radius(*px, *py,
        (player_class == 1u) ? LIGHT_RADIUS_ROGUE
        : (player_class == 2u) ? LIGHT_RADIUS_MAGE
        : LIGHT_RADIUS_KNIGHT);
    {
        int16_t cx = (int16_t)*px - GRID_W / 2;
        int16_t cy = (int16_t)*py - GRID_H / 2;
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > (int16_t)(active_map_w - GRID_W)) cx = (int16_t)(active_map_w - GRID_W);
        if (cy > (int16_t)(active_map_h - GRID_H)) cy = (int16_t)(active_map_h - GRID_H);
        camera_init((uint8_t)cx, (uint8_t)cy);
    }
    ui_loading_screen_end();
    lcd_gameplay_active = 1u;
    window_ui_show();
    ui_panel_show_combat();
    wait_vbl_done();
    draw_screen(*px, *py);
}

