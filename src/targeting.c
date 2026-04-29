#include "targeting.h"
#include "globals.h"
#include "enemy.h"
#include "map.h"  // lighting_is_revealed

static uint8_t abs_u8_diff(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

uint8_t targeting_find_nearest_visible(uint8_t px, uint8_t py, uint8_t max_range,
                                       uint8_t *out_slot,
                                       uint8_t *out_tx, uint8_t *out_ty,
                                       uint8_t *out_too_far) {
    uint8_t best_dist = 255u;
    uint8_t i;
    *out_too_far = 0u;
    for (i = 0u; i < num_enemies; i++) {
        uint8_t ex, ey, dx, dy, dist;
        if (!enemy_alive[i]) continue;
        ex = enemy_x[i];
        ey = enemy_y[i];
        if (!lighting_is_revealed(ex, ey)) continue;
        dx = abs_u8_diff(ex, px);
        dy = abs_u8_diff(ey, py);
        dist = (dx > dy) ? dx : dy; // king-move distance — projectiles fly diagonals
        if (dist <= max_range) {
            if (dist < best_dist) {
                best_dist = dist;
                *out_slot = i;
                *out_tx = ex;
                *out_ty = ey;
            }
        } else {
            *out_too_far = 1u;
        }
    }
    return (best_dist != 255u) ? 1u : 0u;
}
