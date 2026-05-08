#pragma bank 16

#include "scroll_root.h"
#include "globals.h"
#include "enemy.h"

#define ROOT_TURNS 12u

void scroll_root_use(AbilityResult *out) BANKED {
    uint8_t cam_tx = (uint8_t)(camera_px >> 3);
    uint8_t cam_ty = (uint8_t)(camera_py >> 3);
    uint8_t ei;
    out->consumed_turn = 1u;
    for (ei = 0u; ei < num_enemies; ei++) {
        if (!enemy_alive[ei]) continue;
        if (enemy_x[ei] >= cam_tx && enemy_x[ei] < (uint8_t)(cam_tx + GRID_W) &&
            enemy_y[ei] >= cam_ty && enemy_y[ei] < (uint8_t)(cam_ty + GRID_H)) {
            enemy_status[ei] = ROOT_TURNS;
        }
    }
}
