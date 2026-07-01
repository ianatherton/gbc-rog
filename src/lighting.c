#include "map.h"
#include "globals.h"
#include "biome.h"

#define LIGHTING_DIRTY_MAX 80u // knight r=4 diamond ≤77; 80 covers full unexplored room entry

#if FEATURE_MAP_FOG
// ── explored bits live in CGB WRAM bank 2 (SVBK), 0xD000..0xD47F ──────────
// Frees 1,152 B of fixed WRAM for stack headroom (docs/BANKS.md). CGB-only: cart is -Wm-yC.
// Accessors are __naked asm so nothing touches the stack while SVBK != 1 — the stack lives at
// 0xDFxx, which is itself banked memory; a push while switched would land in the wrong bank.
// di/ei guard each switch so an ISR can't push onto the wrong bank either. Calling convention
// (__sdcccall(1), verified): 16-bit first arg in DE, 8-bit return in A.

static uint8_t exp2_test(uint16_t tile_idx) __naked { // returns 0/1 — BIT_GET equivalent
    tile_idx;
__asm
    ld   a, e
    and  #0x07
    ld   b, a          ; b = bit index
    srl  d
    rr   e
    srl  d
    rr   e
    srl  d
    rr   e             ; de = byte offset
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   hl, #0xD000
    add  hl, de
    ld   a, (hl)
    ld   e, a          ; stash byte before restoring the bank
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ld   a, e
    inc  b
10$:
    dec  b
    jr   Z, 11$
    srl  a
    jr   10$
11$:
    and  #0x01
    ret
__endasm;
}

static void exp2_set(uint16_t tile_idx) __naked { // BIT_SET equivalent (read-modify-write in one critical section)
    tile_idx;
__asm
    ld   a, e
    and  #0x07
    ld   b, a
    ld   a, #0x01
    inc  b
20$:
    dec  b
    jr   Z, 21$
    rlca
    jr   20$
21$:
    ld   c, a          ; c = bit mask
    srl  d
    rr   e
    srl  d
    rr   e
    srl  d
    rr   e
    ld   hl, #0xD000
    add  hl, de
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   a, (hl)
    or   c
    ld   (hl), a
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ret
__endasm;
}

static void exp2_clear_all(void) __naked { // zero all 1,152 bytes; ~5 ms with di held — only called during floor gen
__asm
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   hl, #0xD000
    ld   bc, #1152
30$:
    xor  a
    ld   (hl+), a
    dec  bc
    ld   a, b
    or   c
    jr   NZ, 30$
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ret
__endasm;
}
#endif // FEATURE_MAP_FOG

// ── hub water mask lives in CGB WRAM bank 2 at 0xD480..0xD8FF (1,152 B) ──────
// Independent of the fog bits (0xD000..0xD47F): the overworld never reads fog and dungeons never
// read this, so the two never alias. overworld_carve() fills it once; render.c's coast lookup reads
// it (4 bits per land cell) instead of re-evaluating ow_water(). Same __naked/SVBK discipline as the
// exp2_* accessors above (see docs/BANKS.md): nothing may touch the stack while SVBK != 1, di/ei
// guards each switch. Not fog-gated — this is a distinct dataset, but still CGB-only (cart is -Wm-yC).
uint8_t overworld_water_bit(uint16_t tile_idx) __naked { // 1 = water, 0 = land (BIT_GET at 0xD480)
    tile_idx;
__asm
    ld   a, e
    and  #0x07
    ld   b, a          ; b = bit index
    srl  d
    rr   e
    srl  d
    rr   e
    srl  d
    rr   e             ; de = byte offset
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   hl, #0xD480
    add  hl, de
    ld   a, (hl)
    ld   e, a          ; stash byte before restoring the bank
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ld   a, e
    inc  b
40$:
    dec  b
    jr   Z, 41$
    srl  a
    jr   40$
41$:
    and  #0x01
    ret
__endasm;
}

void overworld_water_set(uint16_t tile_idx) __naked { // mark cell as water (BIT_SET at 0xD480)
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
    rr   e
    ld   hl, #0xD480
    add  hl, de
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   a, (hl)
    or   c
    ld   (hl), a
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ret
__endasm;
}

void overworld_water_clear_all(void) __naked { // zero all 1,152 bytes — called once at hub carve
__asm
    di
    ld   a, #0x02
    ldh  (_SVBK_REG + 0), a
    ld   hl, #0xD480
    ld   bc, #1152
60$:
    xor  a
    ld   (hl+), a
    dec  bc
    ld   a, b
    or   c
    jr   NZ, 60$
    ld   a, #0x01
    ldh  (_SVBK_REG + 0), a
    ei
    ret
__endasm;
}

static uint8_t lighting_dirty_x[LIGHTING_DIRTY_MAX];
static uint8_t lighting_dirty_y[LIGHTING_DIRTY_MAX];
static uint8_t lighting_dirty_n;
static uint8_t lighting_dirty_ovf;

void lighting_dirty_clear(void) {
    lighting_dirty_n   = 0u;
    lighting_dirty_ovf = 0u;
}

uint8_t lighting_dirty_count(void) { return lighting_dirty_n; }

void lighting_dirty_tile(uint8_t i, uint8_t *x, uint8_t *y) {
    if (i < lighting_dirty_n) {
        *x = lighting_dirty_x[i];
        *y = lighting_dirty_y[i];
    } else {
        *x = 0u;
        *y = 0u;
    }
}

uint8_t lighting_dirty_overflow(void) { return lighting_dirty_ovf; }

void lighting_reset(void) {
    lighting_dirty_clear();
#if FEATURE_MAP_FOG
    if (floor_biome != BIOME_OVERWORLD) exp2_clear_all(); // hub never reads fog — skip the ~5 ms clear
#endif
}

void lighting_reveal_radius(uint8_t cx, uint8_t cy, uint8_t radius) {
#if FEATURE_MAP_FOG
    int16_t min_x = (int16_t)cx - (int16_t)radius;
    int16_t max_x = (int16_t)cx + (int16_t)radius;
    int16_t min_y = (int16_t)cy - (int16_t)radius;
    int16_t max_y = (int16_t)cy + (int16_t)radius;
    int16_t y;
    lighting_dirty_n   = 0u;
    lighting_dirty_ovf = 0u;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > (int16_t)(MAP_W - 1u)) max_x = (int16_t)(MAP_W - 1u);
    if (max_y > (int16_t)(MAP_H - 1u)) max_y = (int16_t)(MAP_H - 1u);
    {
        uint16_t row_base = (uint16_t)(uint8_t)min_y * MAP_W; // increment by MAP_W per row, no per-tile multiply
        for (y = min_y; y <= max_y; y++, row_base += MAP_W) {
            int16_t x;
            int16_t x_start = min_x;
            int16_t x_end = max_x;
            if (radius != 0u
                    && (y == (int16_t)((int16_t)cy - (int16_t)radius)
                        || y == (int16_t)((int16_t)cy + (int16_t)radius))) {
                x_start++;
                x_end--;
            }
            if (x_start > x_end) continue;
            for (x = x_start; x <= x_end; x++) {
                uint16_t idx = row_base + (uint16_t)x;
                if (!exp2_test(idx)) { // skip SET + dirty tracking for already-revealed tiles
                    exp2_set(idx);
                    if (lighting_dirty_n < LIGHTING_DIRTY_MAX) {
                        lighting_dirty_x[lighting_dirty_n] = (uint8_t)x;
                        lighting_dirty_y[lighting_dirty_n] = (uint8_t)y;
                        lighting_dirty_n++;
                    } else
                        lighting_dirty_ovf = 1u;
                }
            }
        }
    }
#else
    lighting_dirty_clear();
    cx = cx; cy = cy; radius = radius; // keep interface stable until fog is enabled
#endif
}

uint8_t lighting_is_revealed(uint8_t x, uint8_t y) {
    if (floor_biome == BIOME_OVERWORLD) return 1u;
#if FEATURE_MAP_FOG
    return exp2_test(TILE_IDX(x, y));
#else
    x = x; y = y;
    return 1u;
#endif
}
