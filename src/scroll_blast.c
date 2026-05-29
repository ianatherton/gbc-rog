#pragma bank 15

#include "scroll_blast.h"
#include "globals.h"
#include "enemy.h"
#include "combat.h"
#include "lcd.h"

BANKREF_EXTERN(combat_damage_enemy)

void scroll_blast_use(AbilityResult *out) BANKED {
    uint8_t cam_tx = (uint8_t)(camera_px >> 3);
    uint8_t cam_ty = (uint8_t)(camera_py >> 3);
    uint8_t ei, any = 0u;
    lcd_hp_panic_flash_trigger();
    for (ei = 0u; ei < num_enemies; ei++) {
        if (!enemy_alive[ei]) continue;
        if (enemy_x[ei] >= cam_tx && enemy_x[ei] < (uint8_t)(cam_tx + GRID_W) &&
            enemy_y[ei] >= cam_ty && enemy_y[ei] < (uint8_t)(cam_ty + GRID_H)) {
            if (combat_damage_enemy(ei, 50u, 0u)) any = 1u;
        }
    }
    if (any) out->did_kill = 1u;
}
