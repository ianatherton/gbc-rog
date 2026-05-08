#pragma bank 16

#include "scroll_root.h"
#include "globals.h"
#include "enemy.h"

#define ROOT_TURNS 12u

void scroll_root_use(AbilityResult *out) BANKED {
    uint8_t r = player_light_radius();
    uint16_t r2 = (uint16_t)r * r;
    uint8_t ei;
    out->consumed_turn = 1u;
    for (ei = 0u; ei < num_enemies; ei++) {
        if (!enemy_alive[ei]) continue;
        uint8_t dx = (enemy_x[ei] > g_player_x) ? (uint8_t)(enemy_x[ei] - g_player_x) : (uint8_t)(g_player_x - enemy_x[ei]);
        uint8_t dy = (enemy_y[ei] > g_player_y) ? (uint8_t)(enemy_y[ei] - g_player_y) : (uint8_t)(g_player_y - enemy_y[ei]);
        if ((uint16_t)dx * dx + (uint16_t)dy * dy <= r2) {
            enemy_status[ei] = ROOT_TURNS;
        }
    }
}
