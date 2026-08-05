#pragma bank 29

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include "globals.h"
#include "map.h"
#include "dungeon.h"
#include "lcd.h" // lcd_note_bkg0 — panic flash restores the live slot-0 ramp
#include "entity_sprites.h" // entity_sprites_town_npc_glide_set — villager wander slide
#include "items.h" // town_barrel_try_drop_item — barrel loot roll (20%, same table an enemy kill uses)
#include "auto_explore.h" // auto_explore_active — never pop a modal it can't drive
#include "tileset_io.h"   // tileset_load_bkg_tiles — HOME shim; this bank can't SWITCH_ROM to the tileset itself
#include "game_state.h"   // next_state — a villager bump opens STATE_CONVERSATION straight from this bank
#include <gb/cgb.h>
#include <gbdk/platform.h>

BANKREF_EXTERN(town_barrel_try_drop_item)

// Town interior (floors TOWN_FLOOR_BASE + 0..2): a large safe zone entered from the hub town's
// door, sized 59..96 square by its building count (5..20). The map border is a pine ring with a
// brick town wall just inside it; a 2-tile-wide sand road cross runs through the centre and out
// through gaps at N/S/E/W (town_exit_at tests border+road, no stored table) — the south mouth
// doubles as the spawn (drawn as the stairs-up glyph via the player_spawn path). A 2x2 stone well
// (O8:P9 art) sits on the road junction: solid on all four cells, and standing on any of the 12
// cells ringing it restores full HP (overworld_step_feature). Buildings are
// brick rects spread between the roads, each with a signpost by its door (SIGN_KIND_BUILDING) and
// a roof that hides the interior until the player steps in: roof bits live in the fog buffer
// (SVBK2 0xD000, townroof_* in map.h — towns never read fog), town_state->inside_idx picks the one
// building drawn open. Villagers are real OAM sprites (entity_sprites.c refresh_town_npcs_oam) that
// wander a lazy random walk (town_npcs_tick) and warp home if they stray past TOWN_NPC_ROAM_RADIUS
// tiles (town_state->npc_home_*); collision is their current tile only (town_npc_blocks), same as a
// wall — no pathing, no per-NPC extra data beyond position. Fully lit — no fog, no braziers, no
// enemies, no items. Layout is deterministic from (run_seed, town_id).

BANKREF(biome_town_copy_defs)
void biome_town_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for FLOORKIND_TOWN anyway
}

// Same grass field / floor deco as the hub (biome_overworld.c), plus the sand accent for road
// cells. Set here at floor-gen time exactly like the hub's biome row does — apply_field_palette /
// apply_wall_palette only re-push after a menu stomps CRAM.
static const palette_color_t pal_town_field[] = {
    RGB(12, 23, 5), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
};
static const palette_color_t pal_town_floor_deco[] = {
    RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17),
};
static const palette_color_t pal_town_accent[] = {
    RGB(29, 24, 13), RGB(20, 15, 7), RGB(26, 21, 11), RGB(31, 29, 20),
};

BANKREF(biome_town_load_palettes)
void biome_town_load_palettes(void) {
    set_bkg_palette(0, 1u, pal_town_field);
    lcd_note_bkg0(pal_town_field);
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_town_floor_deco);
    set_bkg_palette(PAL_OW_ACCENT, 1u, pal_town_accent);
    // 2x2 well art (O8:P9) into the shared quadrant scratch — see TILE_WELL_* in defs.h. This bank
    // can't SWITCH_ROM to the tileset itself, so it goes through the HOME shim, which pages the
    // tileset bank and restores ours. biome_load_active calls us and THEN takes its non-hub else
    // branch, which only restores sprite slots — none of 194/195/196/198. The top pair is contiguous
    // in both the sheet and VRAM so it rides a single call; the bottom pair straddles slot 197 (the
    // stun icon, live on combat floors and deliberately not borrowed), so it goes one at a time.
    tileset_load_bkg_tiles(TILE_WELL_TL_VRAM, 2u, TILE_WELL_TL); // 194,195 <- O8,P8
    tileset_load_bkg_tiles(TILE_WELL_BL_VRAM, 1u, TILE_WELL_BL); // 196     <- O9
    tileset_load_bkg_tiles(TILE_WELL_BR_VRAM, 1u, TILE_WELL_BR); // 198     <- P9
}

static uint8_t tg_hash(uint8_t town_id, uint8_t salt) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 3571u) ^ (uint16_t)((uint16_t)salt * 149u));
    h ^= (uint16_t)(h >> 7);
    h ^= (uint16_t)(h >> 3);
    return (uint8_t)h;
}

// Tiny local LCG for layout sampling — independent of rand() so a town re-entry regenerates the
// identical layout no matter what other floor-gen code consumed from the shared stream.
static uint16_t tg_rng;
static uint8_t tg_rand(void) {
    tg_rng = (uint16_t)(tg_rng * 25173u + 13849u);
    return (uint8_t)(tg_rng >> 8);
}

// One building: brick wall ring with a hollow floor interior, and the four rect corners cut back
// to open grass. The cut is what the roof's F4 chamfer tiles sit on — the silhouette reads as an
// octagon rather than a hard rectangle (overworld_cell_render picks the corner art from the same
// dx/dy the cut is derived from here, so collision and art can't drift apart). Movement is
// 4-directional, so a diagonal gap is not a passage: a cut corner's orthogonal neighbours are both
// still wall.
static void tg_building_rect(uint8_t x0, uint8_t y0, uint8_t bw, uint8_t bh) {
    uint8_t x, y;
    uint8_t x1 = (uint8_t)(x0 + bw - 1u), y1 = (uint8_t)(y0 + bh - 1u);
    for (y = y0; y < (uint8_t)(y0 + bh); y++)
        for (x = x0; x < (uint8_t)(x0 + bw); x++)
            BIT_CLR(floor_bits, TILE_IDX(x, y));
    for (y = (uint8_t)(y0 + 1u); y < y1; y++)
        for (x = (uint8_t)(x0 + 1u); x < x1; x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));
    BIT_SET(floor_bits, TILE_IDX(x0, y0)); // the four cut corners: open grass, walkable
    BIT_SET(floor_bits, TILE_IDX(x1, y0));
    BIT_SET(floor_bits, TILE_IDX(x0, y1));
    BIT_SET(floor_bits, TILE_IDX(x1, y1));
}

// 1 if a candidate rect (inflated so buildings keep a ≥2-cell gap) touches a placed building.
static uint8_t tg_rects_clash(uint8_t x0, uint8_t y0, uint8_t bw, uint8_t bh) {
    uint8_t i;
    for (i = 0u; i < town_state->count; i++) {
        const TownBuilding *b = &town_state->buildings[i];
        if ((uint8_t)(x0 + bw + 2u) <= b->x || (uint8_t)(b->x + b->w + 2u) <= x0) continue;
        if ((uint8_t)(y0 + bh + 2u) <= b->y || (uint8_t)(b->y + b->h + 2u) <= y0) continue;
        return 1u;
    }
    return 0u;
}

BANKREF(town_exit_at)
uint8_t town_exit_at(uint8_t x, uint8_t y) BANKED { // 1 if (x,y) is a road mouth: any border cell the road reaches
    if (x != 0u && y != 0u && x != (uint8_t)(active_map_w - 1u) && y != (uint8_t)(active_map_h - 1u)) return 0u;
    return road_bit(TILE_IDX(x, y));
}

// 255 = not currently bumping anyone. Same de-dup idiom as state_gameplay.c's confirm_arm: a bump
// prints its line once, holding the direction against a stationary villager doesn't respam it, and
// moving off (or the villager wandering away) clears it so the next bump — even the same villager —
// greets again.
static uint8_t last_bump_npc = 255u;

// Villager whose greeting is currently "spent"; 255 = nobody in range, so the next one to come
// within TOWN_NPC_GREET_RADIUS greets. Chebyshev, matching the roam-radius test below.
#define TOWN_NPC_GREET_RADIUS 4u
static uint8_t last_greet_npc = 255u;

BANKREF(town_npc_blocks)
uint8_t town_npc_blocks(uint8_t x, uint8_t y) BANKED { // 1 if a villager's tile (its only collision — no head hitbox) sits at (x,y); bumping one starts a conversation instead of just blocking
    uint8_t i;
    for (i = 0u; i < town_state->npc_count; i++) {
        if (town_state->npc_x[i] == x && town_state->npc_y[i] == y) {
            if (last_bump_npc != i) {
                last_bump_npc = i;
                // Walking into a villager opens the conversation screen, the same gesture that
                // opens a melee turn against an enemy. next_state is set from here (bank 29)
                // rather than queued for state_gameplay to dispatch, because bank 2 has no room
                // left — this branch is a dead end (no move, no turn) and nothing in the turn tail
                // writes next_state after it, so it lands safely. Auto-explore just gets a wall:
                // it must never be interrupted by a modal.
                if (!auto_explore_active) {
                    pending_talk_npc = i;
                    next_state = STATE_CONVERSATION;
                }
            }
            return 1u;
        }
    }
    last_bump_npc = 255u;
    return 0u;
}

// Lazy random walk, one step per villager per player turn: ~1-in-4 chance to move, direction picked
// uniformly from N/S/W/E, sliding there like an enemy step (entity_sprites_town_npc_glide_set). No
// pathing and no data beyond the current tile — a rejected step (wall, player, another villager)
// just means the villager stands still that turn. A villager that ends up more than
// TOWN_NPC_ROAM_RADIUS tiles (Chebyshev) from its home building warps back instantly (no slide) —
// the glide-set call is skipped whenever the net move for the turn is more than one tile, which only
// happens on that warp.
BANKREF(town_npcs_tick)
void town_npcs_tick(uint8_t px, uint8_t py) BANKED {
    uint8_t i;
    for (i = 0u; i < town_state->npc_count; i++) {
        uint8_t old_x = town_state->npc_x[i], old_y = town_state->npc_y[i];
        if ((rand() & 3u) == 0u) {
            uint8_t dir = (uint8_t)(rand() & 3u); // 0=N 1=S 2=W 3=E
            int8_t dx = (dir == 2u) ? -1 : (dir == 3u) ? 1 : 0;
            int8_t dy = (dir == 0u) ? -1 : (dir == 1u) ? 1 : 0;
            int16_t nx16 = (int16_t)town_state->npc_x[i] + dx;
            int16_t ny16 = (int16_t)town_state->npc_y[i] + dy;
            if (nx16 >= 1 && ny16 >= 1 && nx16 < (int16_t)(active_map_w - 1u) && ny16 < (int16_t)(active_map_h - 1u)) {
                uint8_t nx = (uint8_t)nx16, ny = (uint8_t)ny16, k, occupied = 0u;
                if (nx == px && ny == py) occupied = 1u; // player is standing there
                for (k = 0u; !occupied && k < town_state->npc_count; k++)
                    if (k != i && town_state->npc_x[k] == nx && town_state->npc_y[k] == ny) occupied = 1u;
                if (!occupied && is_walkable(nx, ny)) {
                    town_state->npc_x[i] = nx;
                    town_state->npc_y[i] = ny;
                }
            }
        }
        {
            uint8_t adx = (town_state->npc_x[i] > town_state->npc_home_x[i])
                ? (uint8_t)(town_state->npc_x[i] - town_state->npc_home_x[i])
                : (uint8_t)(town_state->npc_home_x[i] - town_state->npc_x[i]);
            uint8_t ady = (town_state->npc_y[i] > town_state->npc_home_y[i])
                ? (uint8_t)(town_state->npc_y[i] - town_state->npc_home_y[i])
                : (uint8_t)(town_state->npc_home_y[i] - town_state->npc_y[i]);
            uint8_t dist = (adx > ady) ? adx : ady; // Chebyshev, matches the 4-directional step shape
            if (dist > TOWN_NPC_ROAM_RADIUS) {
                town_state->npc_x[i] = town_state->npc_home_x[i]; // simple snap-home — no pathing back either
                town_state->npc_y[i] = town_state->npc_home_y[i];
            }
        }
        { // slide only for a plain single-tile step; a warp-home jump (any larger delta) teleports
            int16_t ddx = (int16_t)old_x - (int16_t)town_state->npc_x[i];
            int16_t ddy = (int16_t)old_y - (int16_t)town_state->npc_y[i];
            if ((ddx || ddy) && ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1)
                entity_sprites_town_npc_glide_set(i, old_x, old_y);
        }
    }

    // Proximity greeting. Bumping a villager now opens the conversation screen, so the canned line
    // they used to say on the bump moved here: walk within TOWN_NPC_GREET_RADIUS and they call out
    // in the chat box. Same call the bump used to make (overworld_signpost_read, bank 22), so the
    // line text and the generated name come along unchanged. The latch is the last_bump_npc idiom:
    // greet once on entering range, re-arm when the player walks out of it or a different villager
    // becomes the one in range.
    {
        uint8_t near = 255u;
        for (i = 0u; i < town_state->npc_count; i++) {
            uint8_t gdx = (town_state->npc_x[i] > px) ? (uint8_t)(town_state->npc_x[i] - px)
                                                      : (uint8_t)(px - town_state->npc_x[i]);
            uint8_t gdy = (town_state->npc_y[i] > py) ? (uint8_t)(town_state->npc_y[i] - py)
                                                      : (uint8_t)(py - town_state->npc_y[i]);
            if (gdx <= TOWN_NPC_GREET_RADIUS && gdy <= TOWN_NPC_GREET_RADIUS) { near = i; break; }
        }
        if (near != last_greet_npc) {
            last_greet_npc = near;
            if (near != 255u) overworld_signpost_read((uint8_t)(SIGN_KIND_NPC | near));
        }
    }
}

// Barrels always break in one hit: no HP, just remove the feature (cell becomes floor again), roll
// loot at 30% (town_barrel_try_drop_item — a separate roll from the enemy-kill 10%, same table), and
// play the same grey death-poof art. Order matters: the feature must be gone and the tile walkable
// BEFORE the poof's ~370ms busy-wait, so a repaint mid-animation (e.g. a VBL-driven HUD update) never
// draws the broken barrel's ghost. Removal is swap-with-last — feature order is never meaningful
// elsewhere, every consumer just loops 0..ow_feature_count. Persistence: the barrel's ordinal (its
// placement order in town_generate_interior, stable across regens — see there) rode in .aux; marking
// its bit in town_barrels_broken means town_generate_interior simply won't place it again this run.
BANKREF(town_barrel_try_break)
uint8_t town_barrel_try_break(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_BARREL || ow_features[i].x != x || ow_features[i].y != y) continue;
        BIT_SET(floor_bits, TILE_IDX(x, y)); // barrel gone — cell walkable again
        {
            // Encounters reuse this function for their own barrels, but they are one-shot floors:
            // nothing there is meant to persist, town_barrels_broken is sized for TOWN_COUNT only,
            // and their barrels carry aux = 255. Guarded on both counts.
            uint8_t ord = ow_features[i].aux;
            if (floor_kind == FLOORKIND_TOWN && ord < MAX_TOWN_BARRELS) {
                uint8_t town_id = (uint8_t)(floor_num - TOWN_FLOOR_BASE);
                town_barrels_broken[(uint8_t)(town_id * 3u + (ord >> 3u))] |= (uint8_t)(1u << (ord & 7u));
            }
        }
        ow_feature_count--;
        ow_features[i] = ow_features[ow_feature_count];
        town_barrel_try_drop_item(x, y); // 30% — separate roll from an enemy kill's 10%, same weighted table
        entity_sprites_run_barrel_poof(x, y);
        return 1u;
    }
    return 0u;
}

BANKREF(town_roof_update)
uint8_t town_roof_update(uint8_t px, uint8_t py) BANKED { // 1 = inside-building state changed → repaint
    uint8_t i, inside = 255u;
    for (i = 0u; i < town_state->count; i++) {
        const TownBuilding *b = &town_state->buildings[i];
        if (px > b->x && px < (uint8_t)(b->x + b->w - 1u)
                && py > b->y && py < (uint8_t)(b->y + b->h - 1u)) { inside = i; break; }
    }
    if (inside == town_state->inside_idx) return 0u;
    town_state->inside_idx = inside;
    return 1u;
}

BANKREF(town_generate_interior)
void town_generate_interior(uint8_t town_id) BANKED {
    uint8_t x, y, i, n;
    const uint8_t target = (uint8_t)(5u + (uint8_t)(tg_hash(town_id, 0u) % 16u)); // 5..20 buildings
    uint8_t dims = (uint8_t)(44u + (uint8_t)(target * 3u)); // 59..104 → clamped below
    uint8_t w, h, cx, cy, rx0, rx1, ry0, ry1; // rx0/rx1, ry0/ry1: the 2-wide road's two columns/rows
    if (dims > MAP_W) dims = MAP_W;
    active_map_w = dims; // storage is the full MAP_W×MAP_H bitset; this floor only uses dims²
    active_map_h = dims;
    w = dims; h = dims;
    cx = (uint8_t)(w >> 1);
    cy = (uint8_t)(h >> 1);
    rx0 = cx; rx1 = (uint8_t)(cx + 1u);
    ry0 = cy; ry1 = (uint8_t)(cy + 1u);
    tg_rng = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)(town_id + 1u) * 40503u));

    // Open yard. Ring 0 (map edge) stays wall → drawn as the pine border; ring 1 stays wall →
    // the brick town wall (both in overworld_cell_render's town branch).
    for (y = 2u; y < (uint8_t)(h - 2u); y++)
        for (x = 2u; x < (uint8_t)(w - 2u); x++)
            BIT_SET(floor_bits, TILE_IDX(x, y));

    // Main road cross, 2 tiles wide: two full columns + two full rows. This also opens the N/S/E/W
    // mouths (each 2 tiles wide) through both border rings in the same pass — town_exit_at derives
    // them from border+road, no stored table. Every carved cell is a road cell (mask read by render).
    road_clear_all(); // hub-only place_overworld_roads never ran for this floor — mask is stale
    for (y = 0u; y < h; y++) {
        BIT_SET(floor_bits, TILE_IDX(rx0, y)); road_set(TILE_IDX(rx0, y));
        BIT_SET(floor_bits, TILE_IDX(rx1, y)); road_set(TILE_IDX(rx1, y));
    }
    for (x = 0u; x < w; x++) {
        BIT_SET(floor_bits, TILE_IDX(x, ry0)); road_set(TILE_IDX(x, ry0));
        BIT_SET(floor_bits, TILE_IDX(x, ry1)); road_set(TILE_IDX(x, ry1));
    }
    player_spawn_x = rx0;                    // south mouth = spawn = stairs glyph;
    player_spawn_y = (uint8_t)(h - 1u);      // every other mouth cell exits via town_exit_at

    townroof_clear_all(); // mandatory: lighting_reset skips towns, so the buffer still holds the last dungeon's fog
    town_state->inside_idx = 255u;
    town_state->count = 0u;
    town_state->npc_count = 0u;
    last_greet_npc = 255u; // slot indices mean a different villager in a different town — re-arm
    last_bump_npc  = 255u;
    uint8_t barrel_ord = 0u; // stable per-barrel id (placement order) — town_barrels_broken persistence keys off this

    { // Rejection-sample the building rects; landing short of `target` on a crowded roll is fine.
        uint16_t tries;
        // 6 is the floor, not 5: the roof's two-step F4 chamfer eats the top two rows at each end,
        // and a door midpoint has to stay clear of the cut corners. Bigger rects also land less
        // often against tg_rects_clash's ≥2-cell gap, hence the raised try budget.
        for (tries = 0u; tries < 400u && town_state->count < target; tries++) {
            uint8_t bw = (uint8_t)(6u + (uint8_t)(tg_rand() % 4u)); // 6..9
            uint8_t bh = (uint8_t)(6u + (uint8_t)(tg_rand() % 3u)); // 6..8
            uint8_t x0 = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(w - 6u - bw)));
            uint8_t y0 = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(h - 6u - bh)));
            uint8_t xlo = (uint8_t)(x0 - 1u), xhi = (uint8_t)(x0 + bw); // keep 1 clear cell off the
            uint8_t ylo = (uint8_t)(y0 - 1u), yhi = (uint8_t)(y0 + bh); // 2-wide main road bands
            if (rx1 >= xlo && rx0 <= xhi) continue;
            if (ry1 >= ylo && ry0 <= yhi) continue;
            // Keep off the well too. The road tests above are single-axis (a road column spans every
            // row, so overlapping it in x is enough to reject); the well is a bounded rect, so this
            // one needs BOTH axes to overlap. Its bounds are widened a further cell beyond the
            // already-margined xlo/xhi, which buys 2 clear cells — enough that a door's welcome mat
            // (1 cell outside the wall) can never land on the stonework.
            if ((uint8_t)(TOWN_WELL_X + 2u) >= xlo && (uint8_t)(TOWN_WELL_X - 1u) <= xhi
                    && (uint8_t)(TOWN_WELL_Y + 2u) >= ylo && (uint8_t)(TOWN_WELL_Y - 1u) <= yhi) continue;
            if (tg_rects_clash(x0, y0, bw, bh)) continue;
            tg_building_rect(x0, y0, bw, bh);
            town_state->buildings[town_state->count].x = x0;
            town_state->buildings[town_state->count].y = y0;
            town_state->buildings[town_state->count].w = bw;
            town_state->buildings[town_state->count].h = bh;
            town_state->count++;
        }
    }

    // The 2x2 stone well sits on the grass just NW of the road junction (TOWN_WELL_X/Y, map.h) — it
    // is NOT an ow_features entry, because the render hot loop's fast reject assumes every town
    // feature is 1 cell wide. All four cells are carved blocking, like a barrel or a pine, and this
    // runs BEFORE the signpost/barrel/pine passes so their `is this cell walkable` tests keep them
    // off it for free. Nothing is disconnected: the whole yard is open grass.
    BIT_CLR(floor_bits, TILE_IDX(TOWN_WELL_X,                 TOWN_WELL_Y));
    BIT_CLR(floor_bits, TILE_IDX((uint8_t)(TOWN_WELL_X + 1u), TOWN_WELL_Y));
    BIT_CLR(floor_bits, TILE_IDX(TOWN_WELL_X,                 (uint8_t)(TOWN_WELL_Y + 1u)));
    BIT_CLR(floor_bits, TILE_IDX((uint8_t)(TOWN_WELL_X + 1u), (uint8_t)(TOWN_WELL_Y + 1u)));

    n = 0u; // features (generate_level zeroed ow_feature_count)

    for (i = 0u; i < town_state->count; i++) {
        TownBuilding *b = &town_state->buildings[i];
        uint8_t bcx = (uint8_t)(b->x + (uint8_t)(b->w >> 1));
        uint8_t bcy = (uint8_t)(b->y + (uint8_t)(b->h >> 1));
        uint8_t adx = (cx > bcx) ? (uint8_t)(cx - bcx) : (uint8_t)(bcx - cx);
        uint8_t ady = (cy > bcy) ? (uint8_t)(cy - bcy) : (uint8_t)(bcy - cy);
        uint8_t door_x, door_y;
        uint8_t closed = (i >= MAX_TOWN_NPCS); // beyond the villager cap: decorative, closed door, no entry
        int8_t sx = 0, sy = 0; // door's outward direction (toward the facing road axis)
        // S / E / W faces only — never N. A north door would sit on the roof's far edge, where the
        // building's own roof hides both it and its signpost from a player approaching from below.
        // So: take the S face when the row road is the nearer axis AND it lies south of us;
        // everything else falls back to the E/W face nearest the column road.
        if (adx <= ady || cy <= bcy) { // door on the E or W wall
            door_y = bcy;
            if (cx > bcx) { door_x = (uint8_t)(b->x + b->w - 1u); sx = 1; }
            else          { door_x = b->x;                        sx = -1; }
        } else {          // row road is nearer and lies south → door on the S wall
            door_x = bcx;
            door_y = (uint8_t)(b->y + b->h - 1u); sy = 1;
        }
        b->door_x = door_x; b->door_y = door_y; b->closed = closed;
        b->mat_x = closed ? 255u : (uint8_t)((int8_t)door_x + sx); // 255 = no mat: a closed building
        b->mat_y = closed ? 255u : (uint8_t)((int8_t)door_y + sy); // has nothing to welcome you into
        if (!closed) BIT_SET(floor_bits, TILE_IDX(door_x, door_y)); // carve the gap; closed stays wall → G2 renders there

        { // Roof bits over the whole octagon EXCEPT the south facade row, which stays brick so the
          // building has a visible front. The top row is inset one cell at each end (that inset,
          // plus the full-width row under it, is the two-step F4 chamfer); every row below is full
          // width, so the E/W walls sit under the roof and a side door has to punch back through it
          // — see the door test in overworld_cell_render, which resolves before this mask.
          // Applied even to closed buildings: harmless (nobody can ever reach in) and one less branch.
            uint8_t rx, ry;
            uint8_t facade = (uint8_t)(b->y + b->h - 1u);
            for (rx = (uint8_t)(b->x + 1u); rx < (uint8_t)(b->x + b->w - 1u); rx++)
                townroof_set(TILE_IDX(rx, b->y));            // inset top row
            for (ry = (uint8_t)(b->y + 1u); ry < facade; ry++)
                for (rx = b->x; rx < (uint8_t)(b->x + b->w); rx++)
                    townroof_set(TILE_IDX(rx, ry));          // full-width body
        }

        if (!closed) { // cosmetic side road: straight run from outside the door toward the facing
          // axis; stops at the first wall (another building/pine placed later can't cut it — pines
          // skip roads) or on meeting an existing road cell. Roads are visual only — movement ignores
          // them. A closed building's door is never walked to, so it gets no approach road.
            // Starts two cells out, not one: door+outward is the welcome-mat cell (drawn by
            // overworld_cell_render straight off the door position), and a sand road cell under it
            // would clash with the mat art's grass background.
            uint8_t rx = (uint8_t)((int8_t)door_x + sx + sx);
            uint8_t ry = (uint8_t)((int8_t)door_y + sy + sy);
            while (rx > 0u && ry > 0u && rx < (uint8_t)(w - 1u) && ry < (uint8_t)(h - 1u)) {
                uint16_t idx = TILE_IDX(rx, ry);
                if (!BIT_GET(floor_bits, idx)) break;
                if (road_bit(idx)) break;
                road_set(idx);
                rx = (uint8_t)((int8_t)rx + sx);
                ry = (uint8_t)((int8_t)ry + sy);
            }
        }

        if (n < MAX_OW_FEATURES) { // signpost beside the welcome mat, one cell perpendicular to the
          // walk path. Side doors get the sign ABOVE the mat (the mat is at door+outward and the
          // road runs on past it), which is the arrangement the design mock uses; a south door has
          // no "above" free, so it takes the cell to the mat's east. Falls back to the opposite
          // perpendicular, then gives up — the mat still marks the entrance on its own.
            int8_t px = (sy != 0) ? (int8_t)1 : (int8_t)0;  // perpendicular offset from the mat cell
            int8_t py = (sx != 0) ? (int8_t)-1 : (int8_t)0;
            uint8_t sgx = (uint8_t)((int8_t)door_x + sx + px);
            uint8_t sgy = (uint8_t)((int8_t)door_y + sy + py);
            if (!BIT_GET(floor_bits, TILE_IDX(sgx, sgy))) { // blocked — try the other side of the path
                sgx = (uint8_t)((int8_t)door_x + sx - px);
                sgy = (uint8_t)((int8_t)door_y + sy - py);
            }
            if (BIT_GET(floor_bits, TILE_IDX(sgx, sgy))) {
                ow_features[n].x = sgx; ow_features[n].y = sgy;
                ow_features[n].type = OW_FEAT_SIGNPOST;
                ow_features[n].aux = (uint8_t)(SIGN_KIND_BUILDING | (uint8_t)(tg_rand() & 7u));
                n++;
            }
        }

        if (!closed) { // villager sprite, home = building centre (entity_sprites.c draws it) — closed
                        // buildings have no reachable interior, so no villager to place inside one
            town_state->npc_home_x[i] = bcx; town_state->npc_home_y[i] = bcy;
            town_state->npc_x[i]      = bcx; town_state->npc_y[i]      = bcy;
            town_state->npc_count++;
        }

        if (n < MAX_OW_FEATURES && (tg_rand() & 1u)) { // ~half the buildings get a barrel against an outer wall
            uint8_t edge = (uint8_t)(tg_rand() & 3u); // 0=N 1=S 2=W 3=E
            uint8_t brx, bry;
            if (edge < 2u) {
                brx = (uint8_t)(b->x + 1u + (uint8_t)(tg_rand() % (uint8_t)(b->w - 2u)));
                bry = (edge == 0u) ? (uint8_t)(b->y - 1u) : (uint8_t)(b->y + b->h);
            } else {
                bry = (uint8_t)(b->y + 1u + (uint8_t)(tg_rand() % (uint8_t)(b->h - 2u)));
                brx = (edge == 2u) ? (uint8_t)(b->x - 1u) : (uint8_t)(b->x + b->w);
            }
            // Keep off the welcome mat at door+outward — for an E/W door that cell is on this very
            // edge run, and a barrel there would block the only way in.
            if (BIT_GET(floor_bits, TILE_IDX(brx, bry)) && !road_bit(TILE_IDX(brx, bry))
                    && !(brx == (uint8_t)((int8_t)door_x + sx) && bry == (uint8_t)((int8_t)door_y + sy))) {
                uint8_t k, clash = 0u;
                for (k = 0u; k < n; k++)
                    if (ow_features[k].x == brx && ow_features[k].y == bry) { clash = 1u; break; }
                if (!clash) {
                    uint8_t ord = barrel_ord++; // assigned regardless of broken-state so it stays stable across regens
                    if (ord >= MAX_TOWN_BARRELS || !(town_barrels_broken[(uint8_t)(town_id * 3u + (ord >> 3u))]
                            & (uint8_t)(1u << (ord & 7u)))) {
                        BIT_CLR(floor_bits, TILE_IDX(brx, bry)); // barrel is a blocking wall cell, like a pine
                        ow_features[n].x = brx; ow_features[n].y = bry;
                        ow_features[n].type = OW_FEAT_BARREL; ow_features[n].aux = ord;
                        n++;
                    }
                }
            }
        }
    }

    // Deco pines come in groves of 3-4 rather than scattered singles: pick a grove anchor, then seat
    // that grove's trees within ±2 cells of it (open grass, off roads/aprons). Anchors sit in
    // [5, w-6] so the ±2 spread still lands in the [3, w-4] band the old scatter used.
    for (i = 0u; i < TOWN_PINE_GROVES && n < MAX_OW_FEATURES; i++) {
        uint8_t want = (uint8_t)(3u + (uint8_t)(tg_rand() & 1u)); // 3 or 4 trees in this grove
        uint8_t gx = (uint8_t)(5u + (uint8_t)(tg_rand() % (uint8_t)(w - 10u)));
        uint8_t gy = (uint8_t)(5u + (uint8_t)(tg_rand() % (uint8_t)(h - 10u)));
        uint8_t tries, placed = 0u;
        for (tries = 0u; tries < 12u && placed < want && n < MAX_OW_FEATURES; tries++) {
            uint8_t px = (uint8_t)(gx - 2u + (uint8_t)(tg_rand() % 5u));
            uint8_t py = (uint8_t)(gy - 2u + (uint8_t)(tg_rand() % 5u));
            uint16_t idx = TILE_IDX(px, py);                        // canopy cell
            uint16_t idx2 = TILE_IDX(px, (uint8_t)(py + 1u));       // trunk cell, one row down
            uint8_t k, clash = 0u;
            // The pine is 1×2, so every test has to pass for BOTH cells — a half-placed tree would
            // draw a floating canopy or leave the trunk walkable. py caps at h-4, so py+1 is in bounds.
            if (!BIT_GET(floor_bits, idx) || !BIT_GET(floor_bits, idx2)) continue; // wall / building
            if (road_bit(idx) || road_bit(idx2)) continue;          // keep every road lane clear
            for (k = 0u; k < town_state->count; k++) { // 1-cell apron around each building (door approaches)
                const TownBuilding *b = &town_state->buildings[k];
                if (px >= (uint8_t)(b->x - 1u) && px <= (uint8_t)(b->x + b->w)
                        && (uint8_t)(py + 1u) >= (uint8_t)(b->y - 1u)
                        && py <= (uint8_t)(b->y + b->h)) { clash = 1u; break; }
            }
            if (clash) continue;
            for (k = 0u; k < n; k++) { // town features are 1 wide; compare row spans (a pine is 2 tall)
                uint8_t fy = ow_features[k].y;
                if (ow_features[k].x != px) continue;
                if ((uint8_t)(py + 1u) >= fy
                        && py < (uint8_t)(fy + ow_prefab_defs[ow_features[k].type].h)) { clash = 1u; break; }
            }
            if (clash) continue;
            BIT_CLR(floor_bits, idx);  // both pine cells are blocking walls, drawn by the TREE feature
            BIT_CLR(floor_bits, idx2);
            ow_features[n].x = px; ow_features[n].y = py;
            ow_features[n].type = OW_FEAT_TREE; ow_features[n].aux = 0u;
            n++; placed++;
        }
    }

    for (i = 0u; i < 3u && n < MAX_OW_FEATURES; i++) { // rare stray barrel, fully random open spot
        uint8_t px = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(w - 6u)));
        uint8_t py = (uint8_t)(3u + (uint8_t)(tg_rand() % (uint8_t)(h - 6u)));
        uint16_t idx = TILE_IDX(px, py);
        uint8_t k, clash = 0u, ord;
        if (tg_rand() >= 32u) continue; // ~1/8 odds per attempt — genuinely rare, not a guaranteed 3rd/4th/5th barrel
        if (!BIT_GET(floor_bits, idx) || road_bit(idx)) continue;
        for (k = 0u; k < town_state->count; k++) { // same 1-cell apron the pines respect: a stray
          // barrel on a door or its welcome mat would wall off that building's only entrance
            const TownBuilding *b = &town_state->buildings[k];
            if (px >= (uint8_t)(b->x - 1u) && px <= (uint8_t)(b->x + b->w)
                    && py >= (uint8_t)(b->y - 1u) && py <= (uint8_t)(b->y + b->h)) { clash = 1u; break; }
        }
        if (clash) continue;
        for (k = 0u; k < n; k++)
            if (ow_features[k].x == px && ow_features[k].y == py) { clash = 1u; break; }
        if (clash) continue;
        ord = barrel_ord++; // assigned regardless of broken-state so it stays stable across regens
        if (ord < MAX_TOWN_BARRELS && (town_barrels_broken[(uint8_t)(town_id * 3u + (ord >> 3u))]
                & (uint8_t)(1u << (ord & 7u)))) continue;
        BIT_CLR(floor_bits, idx);
        ow_features[n].x = px; ow_features[n].y = py;
        ow_features[n].type = OW_FEAT_BARREL; ow_features[n].aux = ord;
        n++;
    }
    ow_feature_count = n;
}
