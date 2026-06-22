#pragma bank 16

#include "debuff_icon.h"
#include "globals.h"
#include "enemy.h"

#define DEBUFF_ICON_PERIOD 16u // VBL frames per cycle (show + dark phase)
#define DEBUFF_ICON_ON     10u // frames the icon is visible within each cycle

static uint8_t debuff_icon_tick;
static uint8_t debuff_icon_cursor;

static uint8_t is_afflicted(uint8_t i) {
    return enemy_alive[i] && (enemy_status[i] > 0u || enemy_stun[i] > 0u);
}

uint8_t debuff_icon_next(uint8_t *out_x, uint8_t *out_y, uint8_t *out_tile) BANKED {
    uint8_t t;
    if (++debuff_icon_tick >= DEBUFF_ICON_PERIOD) {
        debuff_icon_tick = 0u;
        for (t = 0u; t < num_enemies; t++) {
            if (++debuff_icon_cursor >= num_enemies) debuff_icon_cursor = 0u;
            if (is_afflicted(debuff_icon_cursor)) break;
        }
    }
    if (debuff_icon_tick < DEBUFF_ICON_ON
            && debuff_icon_cursor < num_enemies
            && is_afflicted(debuff_icon_cursor)
            && enemy_x[debuff_icon_cursor] >= CAM_TX
            && enemy_x[debuff_icon_cursor] < (uint8_t)(CAM_TX + GRID_W)
            && enemy_y[debuff_icon_cursor] >= CAM_TY
            && enemy_y[debuff_icon_cursor] < (uint8_t)(CAM_TY + GRID_H)) {
        *out_x = enemy_x[debuff_icon_cursor];
        *out_y = enemy_y[debuff_icon_cursor];
        *out_tile = (enemy_stun[debuff_icon_cursor] > 0u) ? TILE_STUN_ICON_VRAM : TILE_ROOT_ICON_VRAM;
        return 1u;
    }
    return 0u;
}
