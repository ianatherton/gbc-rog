#include "map.h"

#define LIGHTING_DIRTY_MAX 48u // knight r=4 diamond ≤41; clipped map stays within this

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
    uint16_t i;
    lighting_dirty_clear();
#if FEATURE_MAP_FOG
    for (i = 0u; i < BITSET_BYTES; i++) explored_bits[i] = 0u;
#else
    i = 0u; // quiet -Wunused-but-set-variable when fog is disabled
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
    for (y = min_y; y <= max_y; y++) {
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
            uint16_t idx = TILE_IDX((uint8_t)x, (uint8_t)y);
            if (!BIT_GET(explored_bits, idx)) {
                if (lighting_dirty_n < LIGHTING_DIRTY_MAX) {
                    lighting_dirty_x[lighting_dirty_n] = (uint8_t)x;
                    lighting_dirty_y[lighting_dirty_n] = (uint8_t)y;
                    lighting_dirty_n++;
                } else
                    lighting_dirty_ovf = 1u;
            }
            BIT_SET(explored_bits, idx);
        }
    }
#else
    lighting_dirty_clear();
    cx = cx; cy = cy; radius = radius; // keep interface stable until fog is enabled
#endif
}

uint8_t lighting_is_revealed(uint8_t x, uint8_t y) {
#if FEATURE_MAP_FOG
    return BIT_GET(explored_bits, TILE_IDX(x, y));
#else
    x = x; y = y; // keep renderer branch-free when fog is disabled
    return 1u;
#endif
}
