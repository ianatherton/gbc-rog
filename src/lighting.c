#include "map.h"

void lighting_reset(void) {
    uint16_t i;
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
        for (x = x_start; x <= x_end; x++) BIT_SET(explored_bits, TILE_IDX((uint8_t)x, (uint8_t)y));
    }
#else
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
