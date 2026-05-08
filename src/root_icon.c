#pragma bank 16

#include "root_icon.h"
#include "globals.h"
#include "enemy.h"

#define ROOT_ICON_PERIOD 16u // VBL frames per cycle (show + dark phase)
#define ROOT_ICON_ON     10u // frames the icon is visible within each cycle

static uint8_t root_icon_tick;
static uint8_t root_icon_cursor;

uint8_t root_icon_next(uint8_t *out_x, uint8_t *out_y) BANKED {
    uint8_t t;
    if (++root_icon_tick >= ROOT_ICON_PERIOD) {
        root_icon_tick = 0u;
        for (t = 0u; t < num_enemies; t++) {
            if (++root_icon_cursor >= num_enemies) root_icon_cursor = 0u;
            if (enemy_alive[root_icon_cursor] && enemy_status[root_icon_cursor] > 0u)
                break;
        }
    }
    if (root_icon_tick < ROOT_ICON_ON
            && root_icon_cursor < num_enemies
            && enemy_alive[root_icon_cursor]
            && enemy_status[root_icon_cursor] > 0u
            && enemy_x[root_icon_cursor] >= CAM_TX
            && enemy_x[root_icon_cursor] < (uint8_t)(CAM_TX + GRID_W)
            && enemy_y[root_icon_cursor] >= CAM_TY
            && enemy_y[root_icon_cursor] < (uint8_t)(CAM_TY + GRID_H)) {
        *out_x = enemy_x[root_icon_cursor];
        *out_y = enemy_y[root_icon_cursor];
        return 1u;
    }
    return 0u;
}
