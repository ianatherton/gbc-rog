#pragma bank 30

#include <gbdk/platform.h>
#include <stdint.h>
#include "defs.h"
#include "dungeon.h"
#include "globals.h"
#include "map.h"
#include "enemy.h"
#include "items.h"
#include "ui.h"
#include "biome.h"
#include "render.h"
#include "auto_explore.h"

uint8_t auto_explore_active = 0u;

/* ── BFS scratch ─────────────────────────────────────────────────────────────
   CGB WRAM bank 3 (SVBK) holds two per-BFS maps — a fresh bank, no aliasing with
   fog/water/road in bank 2:
     0xD000..0xD47F  visited bitmap (1,152 B)
     0xD480..0xDD7F  parent-direction map, 2 bits/tile (2,304 B) — the direction of
                     the step that ENTERED each tile from its BFS parent. Never
                     cleared between runs: only tiles visited by the current run are
                     ever read back, so stale pairs are harmless (writes are RMW).
   Accessors are private __naked clones of lighting.c's exp2_* (same discipline,
   see docs/BANKS.md): nothing touches the stack while SVBK != 1, di/ei guard each
   switch, never called from an ISR.
   Queue is a fixed-WRAM ring: 128 × uint16 packed (x<<7)|y.

   The BFS floods once per TARGET, not per step: the full path is extracted via the
   parent map into ax_path (2-bit packed, 192 steps) and replayed at O(1) per step
   until it's exhausted or invalidated (revealed-item set changes, walk blocked).

   No fighting in this mode (DCSS-style): exploration STOPS when a revealed enemy
   is on screen ("Enemy sighted.") or when the player was struck last turn ("Under
   attack!"). Enemies already on screen when A is pressed are snapshotted as
   ignored, so pressing A again resumes past enemies the player chose to skip.
   Dropping combat also drops its per-step baggage: no enemy-chase BFS, no bump
   synthesis, and no on-screen enemy glide throttling the fast slide. */

#define AX_QCAP     128u
#define AX_PATH_MAX 192u // steps; 2-bit packed → 48 B

static uint16_t ax_queue[AX_QCAP];
static uint8_t  ax_qhead, ax_qtail, ax_qcount;

/* SDCC 4.x miscompiles dense variable-shift bit tests ((arr[i] >> (x&7)) & 1) —
   see the 2026-07-06 strip-classifier bug. Mask LUT / constant shifts dodge it. */
static const uint8_t ax_bitmask[8] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u};

static const uint8_t  ax_dir_j[4] = {J_LEFT, J_RIGHT, J_UP, J_DOWN};
static const int8_t   ax_dx[4]    = {-1, 1, 0, 0};
static const int8_t   ax_dy[4]    = {0, 0, -1, 1};
static const uint16_t ax_didx[4]  = {0xFFFFu, 1u, 0xFFA0u, 96u}; // tile-index delta per dir (two's complement −1/−96)

static uint8_t ax_item_tx[MAX_GROUND_ITEMS]; // revealed ground items, pre-copied for the target test
static uint8_t ax_item_ty[MAX_GROUND_ITEMS];
static uint8_t ax_item_cnt;
static uint8_t ax_item_occ;        // ground-item slot occupancy last seen; a change re-runs the revealed rescan
static uint8_t ax_target_was_item; // 1 = the cached path aims at an item, 0 = at a frontier tile
static uint8_t ax_floor_explored;  // exploration flood already came up empty — go straight to the ladder

static uint16_t ax_start_idx, ax_target_idx;
static uint16_t ax_goal_idx = 0xFFFFu; // explicit BFS destination (the down-ladder pit) — 0xFFFF outside the ladder flood

static uint8_t ax_path[AX_PATH_MAX / 4u]; // 2-bit dirs, replayed across steps
static uint8_t ax_path_len, ax_path_pos;
static uint8_t ax_path_valid;
static uint8_t ax_path_item_cnt; // revealed-item count when the path was computed — change invalidates

static uint8_t ax_prev_x, ax_prev_y;   // stuck guard: position after our last synthesized walk
static uint8_t ax_prev_walked;         // 1 = a walk was synthesized last step
static uint8_t ax_ignore_mask[3];      // enemy slots on screen when A was pressed — don't re-stop for them

/* Ghost detour: a phased-out (enemy_hidden) Ghost squatting on the next path tile is an invisible
   blocker — stopping on it reads as a bug ("Enemy sighted." with an empty screen), and because no
   turn passes while we're stopped, the ghost never moves and a restart re-floods into the same
   tile forever. Instead the blocked tile is fed back into one repair flood as a wall.
   ax_avoid_idx is one-shot (cleared as soon as a path is built — later floods see the real map,
   the ghost will have drifted by then); the streak cap breaks the zero-turn ping-pong two hidden
   ghosts astride the only two routes would otherwise sustain. */
static uint16_t ax_avoid_idx = 0xFFFFu;
static uint8_t  ax_detour_streak;

/* Zero the visited bitmap for the ACTIVE map only, one row per critical section.
   TILE_IDX is y*MAP_W + x with MAP_W = 96, so row y owns exactly bytes y*12..y*12+11 and the live
   columns of that row are the first ceil(active_map_w/8) of them. A 20x20 arena therefore needs
   20 x 3 = 60 bytes, not the flat 1,152 this used to wipe every flood — and bits outside the
   active bounds are never read (ax_visit rejects nx >= active_map_w / ny >= active_map_h before
   touching the bitmap), so leaving them stale is safe.
   `di` is also released between rows now: the old version held interrupts off for the entire
   1,152-byte wipe (~5.5 ms at double speed), long enough to make the VBL ISR miss a frame
   outright — music and lcd.c's SCX/SCY writes both live there.
   NOTHING may touch the stack while SVBK != 1 (the stack itself is at 0xDFxx, i.e. banked), so
   the inner loop uses only registers — no push/pop inside the critical section. */
static void av_clear(void) __naked {
__asm
    ld   a, (#_active_map_w)
    add  a, #7
    srl  a
    srl  a
    srl  a
    ld   c, a          ; c = live bytes per row (>= 3; active_map_w is never below 20)
    ld   a, #12
    sub  c
    ld   e, a          ; e = unused tail of the row, to skip
    ld   a, (#_active_map_h)
    or   a
    ret  Z
    ld   b, a          ; b = rows remaining
    ld   hl, #0xD000
40$:
    di
    ld   a, #0x03
    ldh  (_SVBK_REG + 0), a
    ld   d, c          ; d = inner byte counter
    xor  a
41$:
    ld   (hl+), a
    dec  d
    jr   NZ, 41$
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ld   a, l          ; hl += tail — outside the critical section, and register-only
    add  a, e
    ld   l, a
    jr   NC, 42$
    inc  h
42$:
    dec  b
    jr   NZ, 40$
    ret
__endasm;
}

static uint8_t av_test_set(uint16_t tile_idx) __naked { // test+set in one critical section; returns 0 = newly set, nonzero = was visited
    tile_idx;
__asm
    ld   a, e
    and  #0x07
    ld   b, a
    ld   a, #0x01
    inc  b
50$:
    dec  b
    jr   Z, 51$
    rlca
    jr   50$
51$:
    ld   c, a          ; c = bit mask
    srl  d
    rr   e
    srl  d
    rr   e
    srl  d
    rr   e             ; de = byte offset
    ld   hl, #0xD000
    add  hl, de
    di
    ld   a, #0x03
    ldh  (_SVBK_REG + 0), a
    ld   a, (hl)
    ld   e, a          ; stash original byte before restoring the bank
    or   c
    ld   (hl), a
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ld   a, e
    and  c             ; nonzero iff bit was already set
    ret
__endasm;
}

static void ap_set(uint16_t packed) __naked { // packed = (dir<<14) | tile_idx; RMW the 2-bit pair at 0xD480 + idx/4
    packed;
__asm
    ld   a, d
    rlca
    rlca
    and  #0x03
    ld   c, a          ; c = dir
    ld   a, d
    and  #0x3F
    ld   d, a          ; de = tile_idx (14 bits)
    ld   a, e
    and  #0x03
    add  a, a
    ld   b, a          ; b = bit shift (0/2/4/6)
    ld   a, c          ; a = value accumulator (dir)
    ld   c, #0x03      ; c = mask accumulator
    inc  b
60$:
    dec  b
    jr   Z, 61$
    add  a, a          ; value <<= 1
    sla  c             ; mask  <<= 1
    jr   60$
61$:
    ld   b, a          ; b = dir << shift
    ld   a, c
    cpl
    ld   c, a          ; c = ~(3 << shift)
    srl  d
    rr   e
    srl  d
    rr   e             ; de = byte offset (idx >> 2)
    ld   hl, #0xD480
    add  hl, de
    di
    ld   a, #0x03
    ldh  (_SVBK_REG + 0), a
    ld   a, (hl)
    and  c
    or   b
    ld   (hl), a
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ret
__endasm;
}

static uint8_t ap_get(uint16_t tile_idx) __naked { // returns the 2-bit entry direction stored for tile_idx
    tile_idx;
__asm
    ld   a, e
    and  #0x03
    add  a, a
    ld   b, a          ; b = bit shift (0/2/4/6)
    srl  d
    rr   e
    srl  d
    rr   e             ; de = byte offset
    ld   hl, #0xD480
    add  hl, de
    di
    ld   a, #0x03
    ldh  (_SVBK_REG + 0), a
    ld   a, (hl)
    ld   e, a
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ld   a, e
    inc  b
70$:
    dec  b
    jr   Z, 71$
    srl  a
    jr   70$
71$:
    and  #0x03
    ret
__endasm;
}

static void ax_log(const char *s) { // s = bank-30 literal (readable here); copy to RAM before the cross-bank push
    char buf[20];
    uint8_t i = 0u;
    while (s[i] && i < 19u) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    ui_combat_log_push(buf);
}

static void ax_log_and_redraw(const char *s) { // log lines pushed outside the normal turn flow need an explicit HUD redraw
    ax_log(s);
    wait_vbl_done();
    draw_gameplay_overlays_profiled_far(g_player_x, g_player_y);
}

static uint8_t ax_hp_low(void) { // stop threshold: HP at or below ~half max (48% — hp*100 would wrap uint16 above hp 655)
    return player_hp * 25u <= player_hp_max * 12u;
}

static void ax_push(uint8_t x, uint8_t y) {
    if (ax_qcount >= AX_QCAP) return; // drop: path may come out suboptimal, never corrupt
    ax_queue[ax_qtail] = (uint16_t)((uint16_t)x << 7) | y;
    ax_qtail = (uint8_t)((ax_qtail + 1u) & (AX_QCAP - 1u));
    ax_qcount++;
}

/* 2-bit path buffer accessors — constant shifts only (miscompile-safe). */
static void ax_path_set(uint8_t i, uint8_t d) { // buffer is pre-zeroed, OR is enough
    switch ((uint8_t)(i & 3u)) {
    case 1u: d = (uint8_t)(d << 2); break;
    case 2u: d = (uint8_t)(d << 4); break;
    case 3u: d = (uint8_t)(d << 6); break;
    default: break;
    }
    ax_path[i >> 2] |= d;
}

static uint8_t ax_path_get(uint8_t i) {
    uint8_t b = ax_path[i >> 2];
    switch ((uint8_t)(i & 3u)) {
    case 1u: b >>= 2; break;
    case 2u: b >>= 4; break;
    case 3u: b >>= 6; break;
    default: break;
    }
    return (uint8_t)(b & 3u);
}

/* Examine one BFS neighbor. Target = whichever comes first in breadth-first order, a revealed
   item or an unrevealed frontier tile — so the flood always stops at the NEAREST of the two and
   costs O(distance to it), never O(reachable floor).
   This used to early-exit on a frontier only when no items were revealed; with any item revealed
   it kept hunting and ran the flood to exhaustion over the whole revealed region before falling
   back to the frontier it had already found. That was the multi-frame "thinking" pause. Exiting
   on the first hit of either is both cheaper and better: BFS reaches nearer tiles first, so an
   item closer than the frontier still wins — it is simply found on an earlier layer.
   Returns 1 when (nx,ny) is the target (ax_target_idx set). */
static uint8_t ax_visit(uint8_t nx, uint8_t ny, uint8_t d) {
    uint16_t idx;
    uint8_t revealed;
    if (nx >= active_map_w || ny >= active_map_h) return 0u; // uint8 wrap (0-1 => 255) lands here too — keeps idx inside the bitsets
    idx = TILE_IDX(nx, ny);
    if (!(floor_bits[idx >> 3] & ax_bitmask[(uint8_t)idx & 7u])) return 0u; // wall
    if (idx == ax_avoid_idx) return 0u;                                     // phased Ghost squatting here — detour (see step)
    if (av_test_set(idx)) return 0u;                                        // already visited
    ap_set(idx | ((uint16_t)d << 14));                                      // record entry dir for path extraction
    if (idx == ax_goal_idx) { ax_target_idx = idx; return 1u; }             // ladder run: the pit itself is the destination
    if (pit_bits[idx >> 3] & ax_bitmask[(uint8_t)idx & 7u]) return 0u;      // otherwise never path onto pits/ladders
    revealed = lighting_is_revealed(nx, ny);
    if (!revealed) { ax_target_idx = idx; ax_target_was_item = 0u; return 1u; } // frontier — never expanded, always a target
    {
        uint8_t i;
        for (i = 0u; i < ax_item_cnt; i++)
            if (ax_item_tx[i] == nx && ax_item_ty[i] == ny) {
                ax_target_idx = idx;
                ax_target_was_item = 1u;
                return 1u;
            }
    }
    ax_push(nx, ny); // only expand seen terrain — don't path through tiles the player hasn't revealed
    return 0u;
}

static uint8_t ax_bfs(void) { // flood from the player; 1 = ax_target_idx set
    uint8_t d;
    av_clear();
    ax_qhead = ax_qtail = 0u;
    ax_qcount = 0u;
    ax_start_idx = TILE_IDX(g_player_x, g_player_y);
    av_test_set(ax_start_idx);
    for (d = 0u; d < 4u; d++) {
        if (ax_visit((uint8_t)(g_player_x + ax_dx[d]), (uint8_t)(g_player_y + ax_dy[d]), d))
            return 1u;
    }
    while (ax_qcount) {
        uint16_t e = ax_queue[ax_qhead];
        uint8_t x = (uint8_t)((e >> 7) & 0x7Fu);
        uint8_t y = (uint8_t)(e & 0x7Fu);
        ax_qhead = (uint8_t)((ax_qhead + 1u) & (AX_QCAP - 1u));
        ax_qcount--;
        for (d = 0u; d < 4u; d++) {
            if (ax_visit((uint8_t)(x + ax_dx[d]), (uint8_t)(y + ax_dy[d]), d))
                return 1u;
        }
    }
    return 0u; // queue drained with no frontier and no item reached — this floor is fully explored
}

static uint8_t ax_on_screen(uint8_t ex, uint8_t ey) { // within the camera view rect around the player
    uint8_t dx = (ex > g_player_x) ? (uint8_t)(ex - g_player_x) : (uint8_t)(g_player_x - ex);
    uint8_t dy = (ey > g_player_y) ? (uint8_t)(ey - g_player_y) : (uint8_t)(g_player_y - ey);
    return dx <= (GRID_W / 2u) && dy <= (GRID_H / 2u);
}

static uint8_t ax_build_path(void) { // backtrack ax_target_idx → ax_start_idx via the parent map; 1 = ax_path ready
    uint16_t t = ax_target_idx;
    uint16_t len = 0u;
    uint8_t i;
    while (t != ax_start_idx) { // first pass: measure
        t = (uint16_t)(t - ax_didx[ap_get(t)]);
        len++;
        if (len > 1024u) return 0u; // corrupt parent chain — bail rather than loop
    }
    ax_path_len = (len > AX_PATH_MAX) ? (uint8_t)AX_PATH_MAX : (uint8_t)len; // over-long path: keep the first 192 steps, recompute when exhausted
    for (i = 0u; i < (uint8_t)(AX_PATH_MAX / 4u); i++) ax_path[i] = 0u;
    t = ax_target_idx;
    while (t != ax_start_idx) { // second pass: store dirs (walk is target→player, so positions count down)
        uint8_t d = ap_get(t);
        t = (uint16_t)(t - ax_didx[d]);
        len--;
        if (len < AX_PATH_MAX) ax_path_set((uint8_t)len, d);
    }
    ax_path_pos = 0u;
    return 1u;
}

BANKREF(auto_explore_try_start)
void auto_explore_try_start(void) BANKED {
    if (floor_kind == FLOORKIND_HUB || floor_kind >= FLOORKIND_TOWN) { // fog is always-revealed on lit floors — nothing to explore
        ax_log_and_redraw("Can't explore here.");
        return;
    }
    if (ax_hp_low()) {
        ax_log_and_redraw("Too hurt to explore.");
        return;
    }
    auto_explore_active = 2u; // step() waits for the arming A press to release
    ax_prev_walked = 0u;
    ax_path_valid = 0u;
    ax_avoid_idx = 0xFFFFu;
    ax_detour_streak = 0u;
    ax_floor_explored = 0u;  // a floor change always passes through here before the next run
    ax_target_was_item = 0u;
    ax_item_occ = 0xFFu;     // force one full revealed-item rescan on the first step
    { // snapshot enemies already on screen — the player sees them and chose to explore anyway.
      // A phased-out Ghost is masked too, reveal or not (mid-wall its tile never reveals): it just
      // interrupted this run, so without the mask each restart-while-hidden buys one more stop the
      // moment it phases back in. Masking it makes hidden restarts match visible ones — the run
      // then ends the same way it would for a masked visible ghost, on its strike ("Under attack!").
        uint8_t i;
        ax_ignore_mask[0] = ax_ignore_mask[1] = ax_ignore_mask[2] = 0u;
        for (i = 0u; i < num_enemies; i++) {
            if (enemy_alive[i] && ax_on_screen(enemy_x[i], enemy_y[i])
                    && (enemy_hidden[i] || lighting_is_revealed(enemy_x[i], enemy_y[i])))
                ax_ignore_mask[i >> 3] |= ax_bitmask[i & 7u];
        }
    }
    enemy_attack_count = 0u; // stale strikes from the last manual turn must not trip "Under attack!"
    ax_log_and_redraw("Exploring...");
}

BANKREF(auto_explore_step)
uint8_t auto_explore_step(uint8_t j) BANKED {
    uint8_t d;

    if (auto_explore_active == 3u) {           // stopped — swallow input until everything releases
        if (j == 0u) auto_explore_active = 0u;
        return 0u;
    }
    if (auto_explore_active == 2u) {           // armed — wait for the arming A press to release
        if (j != 0u) return 0u;
        auto_explore_active = 1u;
    } else if (j != 0u) {                      // running + any user input = cancel
        auto_explore_active = 3u;
        ax_log_and_redraw("Stopped.");
        return 0u;
    }

    if (ax_hp_low()) { // deliberately no stop-on-hit — only the threshold (a player_hp < snapshot check here is the one-line option)
        auto_explore_active = 0u;
        ax_log_and_redraw("Hurt - stopping.");
        return 0u;
    }

    if (enemy_attack_count) { // an enemy struck (or swung at) us last turn — hand control back
        auto_explore_active = 0u;
        ax_log_and_redraw("Under attack!");
        return 0u;
    }

    { // DCSS-style stop-on-sight: a non-ignored revealed enemy inside the view rect ends the run
        uint8_t i;
        for (i = 0u; i < num_enemies; i++) {
            if (!enemy_alive[i] || enemy_hidden[i]) continue; // a phased-out Ghost doesn't trip stop-on-sight
            if (ax_ignore_mask[i >> 3] & ax_bitmask[i & 7u]) continue;
            if (ax_on_screen(enemy_x[i], enemy_y[i]) && lighting_is_revealed(enemy_x[i], enemy_y[i])) {
                auto_explore_active = 0u;
                ax_log_and_redraw("Enemy sighted.");
                return 0u;
            }
        }
    }

    if (ax_prev_walked && g_player_x == ax_prev_x && g_player_y == ax_prev_y) {
        auto_explore_active = 0u;              // last walk didn't move us (unmodeled blocker) — bail rather than loop
        ax_log_and_redraw("Stopped.");
        return 0u;
    }

    { // refresh the revealed-item list, but only when the ground-item set actually changed
        uint8_t i, occ = 0u;
        for (i = 0u; i < MAX_GROUND_ITEMS; i++)
            if (ground_item_kind[i] != ITEM_KIND_NONE) occ |= ax_bitmask[i];
        // The occupancy mask is 8 plain array reads; the full rescan below costs a banked-WRAM
        // round trip per live item (lighting_is_revealed), which is why it is gated on a change.
        // The `ax_path_pos >= ax_path_len` term is what makes the gate SAFE, and it is not optional:
        // an item that becomes revealed without the slot set changing does not move `occ`, so
        // without it the list stays stale — and because `ax_path_valid` is not cleared until the
        // check below, the very step that re-plans would flood against that stale list and never
        // see the item. If the floor finished exploring meanwhile, the run headed for the ladder
        // and the item was missed outright rather than merely deferred. Testing the exhaustion
        // condition HERE (rather than reading ax_path_valid, which is still 1 at this point) means
        // every flood is guaranteed a freshly scanned list.
        if (occ != ax_item_occ || !ax_path_valid || ax_path_pos >= ax_path_len) {
            ax_item_occ = occ;
            ax_item_cnt = 0u;
            for (i = 0u; i < MAX_GROUND_ITEMS; i++) {
                if ((occ & ax_bitmask[i])
                        && lighting_is_revealed(ground_item_x[i], ground_item_y[i])) {
                    ax_item_tx[ax_item_cnt] = ground_item_x[i];
                    ax_item_ty[ax_item_cnt] = ground_item_y[i];
                    ax_item_cnt++;
                }
            }
        }
    }
    // Only an item TARGET cares that the item set changed. Invalidating on every count change made
    // each auto-pickup force a fresh flood even though the rest of the path was still walkable.
    if (ax_path_valid && (ax_path_pos >= ax_path_len
                          || (ax_target_was_item && ax_item_cnt != ax_path_item_cnt)))
        ax_path_valid = 0u;
    if (!ax_path_valid) { // one flood per target; steps in between just replay ax_path
        uint8_t ok;
        if (ax_avoid_idx == 0xFFFFu) {
            // Pre-avoid a masked/hidden enemy on a neighboring tile. The chasing Ghost rides
            // adjacent for whole runs, so any fresh path that doubles back would take its tile as
            // the FIRST step and bounce: flood → blocked → detour → second flood. Feeding the
            // avoid tile into this flood up front halves the "thinking" pause at target switches.
            uint8_t nd;
            for (nd = 0u; nd < 4u; nd++) {
                uint8_t ax = (uint8_t)(g_player_x + ax_dx[nd]);
                uint8_t ay = (uint8_t)(g_player_y + ax_dy[nd]);
                uint8_t ei;
                if (ax >= active_map_w || ay >= active_map_h) continue;
                ei = enemy_at(ax, ay);
                if (ei != ENEMY_DEAD
                        && (enemy_hidden[ei] || (ax_ignore_mask[ei >> 3] & ax_bitmask[ei & 7u]))) {
                    ax_avoid_idx = TILE_IDX(ax, ay);
                    break;
                }
            }
        }
        // Once the exploration flood has come up empty the floor stays explored (reveal only ever
        // grows), so skip straight to the ladder flood on later re-plans. This used to run BOTH
        // floods — two full av_clears and two exhaustive sweeps — on every single re-plan.
        ok = ax_floor_explored ? 0u : ax_bfs();
        if (!ok && ax_avoid_idx != 0xFFFFu) { // the Ghost detour walled off the only route (1-wide corridor)
            ax_avoid_idx = 0xFFFFu;
            auto_explore_active = 0u;
            ax_log_and_redraw("Stopped."); // NOT "Floor explored." — the flood failed because of the avoid tile
            return 0u;
        }
        if (!ok) { // fully explored — walk to the down-ladder instead of just stopping
            uint8_t lx, ly;
            uint8_t first = (uint8_t)!ax_floor_explored; // announce the switch once, not on every re-plan
            ax_floor_explored = 1u;
            if (!boss_alive && map_pit_position(&lx, &ly)) {
                uint16_t pidx = TILE_IDX(lx, ly);
                if (TILE_IDX(g_player_x, g_player_y) == pidx) { // arrived — the walk armed the descend confirm; A drops
                    auto_explore_active = 0u;
                    return 0u;
                }
                ax_goal_idx = pidx;
                ok = ax_bfs();
                ax_goal_idx = 0xFFFFu;
                ax_target_was_item = 0u; // the ladder is the target; item churn must not invalidate this path
                if (ok && first) ax_log_and_redraw("Heading down.");
            }
        }
        if (!ok || !ax_build_path()) {
            auto_explore_active = 0u;
            ax_log_and_redraw("Floor explored.");
            return 0u;
        }
        ax_path_valid = 1u;
        ax_path_item_cnt = ax_item_cnt;
        ax_avoid_idx = 0xFFFFu; // one-shot: this path already detours; later floods see the real map
    }
    d = ax_path_get(ax_path_pos);
    { // never step INTO an enemy — that would be a bump-attack, and this mode doesn't fight
        uint8_t nx = (uint8_t)(g_player_x + ax_dx[d]);
        uint8_t ny = (uint8_t)(g_player_y + ax_dy[d]);
        uint16_t idx = TILE_IDX(nx, ny); // path tiles are always in bounds
        if (enemy_occ[idx >> 3] & ax_bitmask[(uint8_t)idx & 7u]) {
            // Detour rather than stop for a blocker that is hidden (phased Ghost — invisible, and
            // no turn passes while stopped, so it would never move: restart livelock) OR already
            // ignore-masked (the player chose to explore past it; stopping into it re-floods the
            // BFS per restart — the chasing Ghost sits adjacent/solid PERMANENTLY, so every
            // doubled-back path used to buy a stop + full-flood cycle, felt as "thinking" pauses).
            // An unmasked visible enemy still stops the run — that's stop-on-sight's contract.
            uint8_t ei = enemy_at(nx, ny);
            uint8_t dodge = (uint8_t)(ei != ENEMY_DEAD
                                      && (enemy_hidden[ei]
                                          || (ax_ignore_mask[ei >> 3] & ax_bitmask[ei & 7u])));
            if (dodge && ax_detour_streak < 2u) {
                ax_detour_streak++;    // reset on the next real step — see ax_avoid_idx comment
                ax_avoid_idx = idx;
                ax_path_valid = 0u;    // think for a frame; the next tick floods around the enemy
                ax_prev_walked = 0u;   // no walk synthesized — don't let the stuck guard trip
                return 0u;
            }
            auto_explore_active = 0u;
            // Detours exhausted on a masked/hidden blocker: a "sighting" would read as a bug
            // (nothing new is visible) — the generic stop is the honest line.
            ax_log_and_redraw(dodge ? "Stopped." : "Enemy sighted.");
            return 0u;
        }
    }
    ax_detour_streak = 0u;
    ax_path_pos++;
    ax_prev_x = g_player_x;
    ax_prev_y = g_player_y;
    ax_prev_walked = 1u;
    return ax_dir_j[d];
}

BANKREF(auto_explore_take_pending)
void auto_explore_take_pending(void) BANKED {
    uint8_t gi = pending_pickup_slot;
    pending_pickup_slot = 255u;
    if (gi >= MAX_GROUND_ITEMS || ground_item_kind[gi] == ITEM_KIND_NONE) return;
    if (!inventory_add(ground_item_kind[gi], ground_item_mod_level[gi])) {
        auto_explore_active = 0u;              // truly full (stacking already tried) — hand over to the normal modal
        pending_pickup_slot = gi;
        next_state = STATE_PICKUP;
        return;
    }
    {
        char log[20];
        char namebuf[16];
        uint8_t i = 0u, k = 0u;
        items_kind_display_name_copy(ground_item_kind[gi], ground_item_mod_level[gi], namebuf, sizeof namebuf);
        log[i++] = 'G'; log[i++] = 'o'; log[i++] = 't'; log[i++] = ' ';
        while (namebuf[k] && i < 19u) { log[i++] = namebuf[k++]; }
        log[i] = 0;
        ui_combat_log_push(log);
    }
    ground_item_kill(gi);
    wait_vbl_done();
    draw_gameplay_overlays_profiled_far(g_player_x, g_player_y);
}
