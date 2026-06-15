#pragma bank 2

#include "map.h"
#include "globals.h"
#include "enemy.h"
#include "render.h"
#include "camera.h"
#include "ui.h"
#include "lcd.h"
#include "entity_sprites.h"
#include "wall_palettes.h"
#include "biome.h"
#include "ally.h"
#include "items.h"
#include <gbdk/platform.h>
#include <rand.h>

BANKREF_EXTERN(ally_clear_all)
BANKREF_EXTERN(entity_sprites_poof_clear_all)
BANKREF_EXTERN(generate_level)

uint8_t floor_bits[BITSET_BYTES]; // 1 = open tile (floor or pit); 0 = wall
uint8_t pit_bits[BITSET_BYTES];   // subset of floor: 1 = pit hazard
// explored (fog) bits live in CGB WRAM bank 2 — access only through lighting.c (exp2_* helpers)
NavNode nav_nodes[MAX_NAV_NODES]; // junction graph for enemy pathing
uint8_t num_nav_nodes;            // how many nodes after build_nav_graph
uint8_t wall_tileset_index; // level_generate_and_spawn assigns TILE_* before any tile decode
uint8_t wall_palette_index; // set from RNG in level_generate_and_spawn before render
uint8_t pillar_palette_index;
uint8_t active_map_w; // generate_level sets before use (never read beforehand)
uint8_t active_map_h;
uint8_t player_spawn_x;
uint8_t player_spawn_y;
uint8_t floor_column_off; // floor_ground_init sets before floor_tile_sheet_offset reads column art
static uint16_t floor_visual_seed;        // deterministic seed for floor blank-scatter hash
uint8_t brazier_count;
uint8_t brazier_x[MAX_BRAZIERS];
uint8_t brazier_y[MAX_BRAZIERS];
uint8_t brazier_type[MAX_BRAZIERS]; // 0=brazier C3/C4, 1=torch C1/C2
uint8_t pit_x, pit_y, pit_present; // written by map_gen.c (set_pit); read here by map_pit_position

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


BANKREF(tile_at)
uint8_t tile_at(uint8_t x, uint8_t y) BANKED { // decode logical tile from two bitsets
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

BANKREF(map_pit_position)
uint8_t map_pit_position(uint8_t *x, uint8_t *y) BANKED {
    if (!pit_present) return 0u;
    *x = pit_x;
    *y = pit_y;
    return 1u;
}

BANKREF(ground_item_index_at)
uint8_t ground_item_index_at(uint8_t x, uint8_t y) BANKED { // linear scan; pool is tiny (8 max)
    uint8_t i;
    for (i = 0u; i < MAX_GROUND_ITEMS; i++) {
        if (ground_item_kind[i] != ITEM_KIND_NONE
                && ground_item_x[i] == x && ground_item_y[i] == y) return i;
    }
    return 255u;
}

BANKREF(ground_item_kill)
void ground_item_kill(uint8_t slot) BANKED {
    if (slot < MAX_GROUND_ITEMS) {
        ground_item_kind[slot] = ITEM_KIND_NONE;
        floor_items_picked[floor_num - 1u] |= (uint8_t)(1u << slot);
    }
}

static void ground_items_clear(void) {
    uint8_t i;
    for (i = 0u; i < MAX_GROUND_ITEMS; i++) ground_item_kind[i] = ITEM_KIND_NONE;
}

BANKREF(is_walkable)
uint8_t is_walkable(uint8_t x, uint8_t y) BANKED { // used by AI and pit checks
    return BIT_GET(floor_bits, TILE_IDX(x, y)); // pits count as walkable until movement resolves
}

char tile_char(uint8_t t) { // ASCII fallback when not using custom tile in VRAM
    if (t == TILE_WALL) return '#';
    if (t == TILE_PIT)  return '0'; // digit zero reads as pit marker in font
    return '.';                     // plain floor
}

uint8_t tile_vram_index(uint8_t t) { // non-zero → set_bkg_tiles uses ROM tile data
    if (t == TILE_WALL) return (uint8_t)(TILESET_VRAM_OFFSET + wall_tileset_index);
    if (t == TILE_PIT) {
        if (floor_biome == BIOME_BOSS && boss_alive) return 0u; // hidden until boss dies
        return (uint8_t)(TILESET_VRAM_OFFSET + TILE_LADDER_DOWN);
    }
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
        if (floor_num > 0u && (floor_biome != BIOME_BOSS || !boss_alive))
            return TILE_STAIRS_UP_1;
        // floor 0 hub (nothing above) or boss alive: fall through to normal floor rendering
    }
    {
        uint8_t bi = brazier_index_at(x, y);
        if (bi != 255u) {
            if (brazier_type[bi] == 0u)
                return (uint8_t)((((uint8_t)(DIV_REG >> 3) + x + y) & 1u) ? TILE_LIGHT_4 : TILE_LIGHT_3); // C3/C4
            return (uint8_t)((((uint8_t)(DIV_REG >> 3) + x + y) & 1u) ? TILE_LIGHT_2 : TILE_LIGHT_1); // C1/C2
        }
    }
    if (ground_item_index_at(x, y) != 255u) return TILE_ITEM_4; // mystery icon — true kind revealed in pickup dialog
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


uint8_t nearest_nav_node(uint8_t x, uint8_t y) { // map any tile to closest junction for routing
    uint8_t best = NAV_NO_LINK, best_dist = 255, i;
    for (i = 0; i < num_nav_nodes; i++) {
        uint8_t dx = (nav_nodes[i].x > x) ? nav_nodes[i].x - x : x - nav_nodes[i].x;
        uint8_t dy = (nav_nodes[i].y > y) ? nav_nodes[i].y - y : y - nav_nodes[i].y;
        uint8_t d  = dx + dy;
        if (d == 0) return i; // exact match — chasing enemies often sit on junctions
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

void nav_fill_hops_from(uint8_t player_node, uint8_t *hop_out) {
    // Single BFS from player_node. hop_out[n] = which neighbour to visit from n to reach player_node.
    // Replaces per-enemy nav_next_step calls: one pass fills the hop table for all nav nodes.
    uint8_t visited[MAX_NAV_NODES];
    uint8_t queue[MAX_NAV_NODES];
    uint8_t qhead = 0, qtail = 0, i;
    for (i = 0; i < MAX_NAV_NODES; i++) { visited[i] = 0; hop_out[i] = NAV_NO_LINK; }
    if (player_node == NAV_NO_LINK) return;
    visited[player_node] = 1;
    queue[qtail++] = player_node;
    while (qhead < qtail) {
        uint8_t cur = queue[qhead++];
        for (i = 0; i < 4; i++) {
            uint8_t nb = nav_nodes[cur].adj[i];
            if (nb == NAV_NO_LINK || visited[nb]) continue;
            visited[nb] = 1;
            hop_out[nb] = cur; // from nb, step to cur to move toward player_node
            queue[qtail++] = nb;
            if (qtail >= MAX_NAV_NODES) goto bfs_done;
        }
    }
bfs_done:;
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
    entity_sprites_poof_clear_all();
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
    knight_shield_active = 0u; // floor-scoped buff — clear on every regen so it doesn't leak across stairs
    player_light_bonus     = 0u; // candles consumed per-floor; re-apply durable equipment bonuses below
    {
        uint8_t _i;
        for (_i = 0u; _i < INVENTORY_MAX_SLOTS; _i++) {
            if (inventory_equipped[_i] && inventory_kind[_i] == ITEM_KIND_BOOTS) {
                uint16_t nb = (uint16_t)player_light_bonus + 2u;
                player_light_bonus = (nb > 255u) ? 255u : (uint8_t)nb;
            }
        }
    }
    ally_clear_all();
    biome_load_active(biome_pick_for_floor(floor_num, run_seed)); // fills HOME enemy_defs[] from coral bank before spawn
    if (floor_biome == BIOME_CAVERN) wall_tileset_index = TILE_WALL_F;
    if (floor_biome == BIOME_OVERWORLD) {
        // Hub uses c10 for both bulk walls and isolated pillars. Future areas would also
        // place extra transition tiles here and route them via a destination lookup.
        wall_tileset_index = TILE_OVERWORLD_WALL_OFF;
        floor_column_off   = TILE_OVERWORLD_WALL_OFF;
    }
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
    ground_items_clear();
    if (floor_biome != BIOME_OVERWORLD) { // hub has no items (also avoids floor_items_picked[floor_num-1] underflow at floor 0)
        uint8_t target = (uint8_t)(2u + (uint8_t)(rand() & 3u)); // 2..5 items per floor
        uint8_t placed = 0u;
        uint16_t attempts = 0u;
        if (target > MAX_GROUND_ITEMS) target = MAX_GROUND_ITEMS;
        while (placed < target && attempts < 200u) {
            uint8_t tx = (uint8_t)(rand() % active_map_w);
            uint8_t ty = (uint8_t)(rand() % active_map_h);
            uint16_t idx = TILE_IDX(tx, ty);
            attempts++;
            if ((tx == player_spawn_x && ty == player_spawn_y)
                    || !BIT_GET(floor_bits, idx)
                    || BIT_GET(pit_bits, idx)
                    || brazier_index_at(tx, ty) != 255u
                    || ground_item_index_at(tx, ty) != 255u) continue;
            ground_item_x[placed] = tx;
            ground_item_y[placed] = ty;
            ground_item_kind[placed] = (uint8_t)(rand() % ITEM_KIND_COUNT);
            placed++;
        }
        if (!level_is_revisit) {
            floor_items_picked[floor_num - 1u] = 0u;
        } else {
            uint8_t _gi;
            for (_gi = 0u; _gi < placed; _gi++) {
                if (floor_items_picked[floor_num - 1u] & (uint8_t)(1u << _gi))
                    ground_item_kind[_gi] = ITEM_KIND_NONE;
            }
        }
    }
    if (level_is_revisit) {
        uint8_t _ei;
        for (_ei = 0u; _ei < num_enemies; _ei++) {
            if (floor_enemy_dead[(floor_num - 1u) * 3u + (_ei >> 3u)]
                    & (uint8_t)(1u << (_ei & 7u))) {
                enemy_alive[_ei] = 0u;
                enemy_clear_slot(enemy_x[_ei], enemy_y[_ei]);
                if (enemy_type[_ei] == ENEMY_GORGON)
                    enemy_clear_slot((uint8_t)(enemy_x[_ei] + 1u), enemy_y[_ei]);
                if (enemy_force_active[_ei])
                    boss_alive = 0u;
                if (num_corpses < MAX_CORPSES
                        && ground_item_index_at(enemy_x[_ei], enemy_y[_ei]) == 255u) {
                    corpse_x[num_corpses] = enemy_x[_ei];
                    corpse_y[num_corpses] = enemy_y[_ei];
                    corpse_tile[num_corpses] = corpse_deco_random();
                    corpse_place_slot(num_corpses, enemy_x[_ei], enemy_y[_ei]);
                    num_corpses++;
                }
            }
        }
    }
    pending_pickup_slot = 255u; // fresh floor — no carryover prompt
    if (entered_from_below && map_pit_position(px, py)) {
        lighting_reveal_radius(*px, *py, LIGHT_RADIUS_LADDER_DOWN);
    } else {
        *px = player_spawn_x;
        *py = player_spawn_y;
        lighting_reveal_radius(*px, *py, LIGHT_RADIUS_STAIRS_UP);
    }
    lighting_reveal_radius(*px, *py, player_light_radius());
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
    lighting_dirty_clear(); // full repaint consumed initial reveals — avoid stale dirty list
}

