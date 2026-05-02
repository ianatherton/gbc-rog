#pragma bank 2

#include "camera.h"
#include "defs.h" // camera_px, camera_py, GRID_W/H, MAP_W/H, SCROLL_SPEED
#include "enemy.h"
#include "entity_sprites.h"
#include "lcd.h"
#include "render.h"
#include "perf.h"
#include <gbdk/platform.h>

BANKREF(camera_init)

uint16_t camera_px;
uint16_t camera_py;

static const int8_t player_bob_table[8] = { 0, -1, -2, -2, -1, 0, 1, 0 };

void camera_init(uint8_t top_tx, uint8_t top_ty) BANKED {
    camera_px = (uint16_t)top_tx * 8u;
    camera_py = (uint16_t)top_ty * 8u;
}

static void player_glide_when_camera_idle(uint8_t opx, uint8_t opy, uint8_t px, uint8_t py) { // map edge: camera fixed, sprite still eases one tile
    int16_t wx = (int16_t)opx * 8, wy = (int16_t)opy * 8;
    int16_t ex = (int16_t)px * 8, ey = (int16_t)py * 8;
    uint8_t bob_i = 0u;
    while (wx != ex || wy != ey) {
        if (wx < ex) { wx += SCROLL_SPEED; if (wx > ex) wx = ex; }
        else if (wx > ex) { if ((wx - ex) <= SCROLL_SPEED) wx = ex; else wx -= SCROLL_SPEED; }
        if (wy < ey) { wy += SCROLL_SPEED; if (wy > ey) wy = ey; }
        else if (wy > ey) { if ((wy - ey) <= SCROLL_SPEED) wy = ey; else wy -= SCROLL_SPEED; }
        entity_sprites_set_player_world(wx, (int16_t)(wy + player_bob_table[bob_i]), wx, wy);
        bob_i = (uint8_t)((bob_i + 1u) & 7u);
        entity_sprites_refresh_player_only(px, py);
        {
            uint8_t i;
            for (i = 0; i < num_enemies; i++) entity_sprites_refresh_enemy(i);
        }
        wait_vbl_done();
    }
    entity_sprites_clear_player_world();
    entity_sprites_refresh_all(px, py);
}

void camera_scroll_to(uint8_t target_tx, uint8_t target_ty,
                      uint8_t opx, uint8_t opy, uint8_t px, uint8_t py) BANKED { // smooth pan; sprite steps from old tile with camera (16-bit only)
    uint8_t perf_stamp = perf_stamp_now();
    uint16_t target_px = (uint16_t)target_tx * 8u;
    uint16_t target_py = (uint16_t)target_ty * 8u;
    uint16_t guard_steps = 0u;
    int16_t pwx = (int16_t)opx * 8, pwy = (int16_t)opy * 8; // player sprite world pos, stepped toward target
    int16_t target_pwx = (int16_t)px * 8, target_pwy = (int16_t)py * 8;
    uint8_t bob_i = 0u;

    if (camera_px == target_px && camera_py == target_py && (opx != px || opy != py)) {
        player_glide_when_camera_idle(opx, opy, px, py);
        perf_record(PERF_CAMERA_SCROLL, perf_stamp_elapsed(&perf_stamp));
        return;
    }

    while (camera_px != target_px || camera_py != target_py) {
        if (++guard_steps > 2048u) { camera_px = target_px; camera_py = target_py; entity_sprites_clear_player_world(); entity_sprites_refresh_all(px, py); break; }
        uint8_t old_ctx = (uint8_t)(camera_px >> 3);
        uint8_t old_cty = (uint8_t)(camera_py >> 3);

        if (camera_px < target_px) { camera_px += SCROLL_SPEED; if (camera_px > target_px) camera_px = target_px; }
        else if (camera_px > target_px) {
            if ((camera_px - target_px) <= SCROLL_SPEED) camera_px = target_px;
            else camera_px -= SCROLL_SPEED;
        }
        if (camera_py < target_py) { camera_py += SCROLL_SPEED; if (camera_py > target_py) camera_py = target_py; }
        else if (camera_py > target_py) {
            if ((camera_py - target_py) <= SCROLL_SPEED) camera_py = target_py;
            else camera_py -= SCROLL_SPEED;
        }

        if (pwx < target_pwx) { pwx += SCROLL_SPEED; if (pwx > target_pwx) pwx = target_pwx; }
        else if (pwx > target_pwx) { pwx -= SCROLL_SPEED; if (pwx < target_pwx) pwx = target_pwx; }
        if (pwy < target_pwy) { pwy += SCROLL_SPEED; if (pwy > target_pwy) pwy = target_pwy; }
        else if (pwy > target_pwy) { pwy -= SCROLL_SPEED; if (pwy < target_pwy) pwy = target_pwy; }
        entity_sprites_set_player_world(pwx, (int16_t)(pwy + player_bob_table[bob_i]), pwx, pwy);
        bob_i = (uint8_t)((bob_i + 1u) & 7u);

        wait_vbl_done();

        {
            uint8_t new_ctx = (uint8_t)(camera_px >> 3);
            uint8_t new_cty = (uint8_t)(camera_py >> 3);
            if (new_ctx != old_ctx)
                draw_col_strip(new_ctx > old_ctx ? (uint8_t)(new_ctx + GRID_W) : new_ctx);
            if (new_cty != old_cty)
                draw_row_strip(new_cty > old_cty ? (uint8_t)(new_cty + GRID_H) : new_cty);
        }
        draw_ui_rows();
        entity_sprites_refresh_player_only(px, py);
        {
            uint8_t i;
            for (i = 0; i < num_enemies; i++) entity_sprites_refresh_enemy(i);
        }
    }
    entity_sprites_clear_player_world();
    entity_sprites_refresh_all(px, py);
    perf_record(PERF_CAMERA_SCROLL, perf_stamp_elapsed(&perf_stamp));
}

void camera_shake(void) BANKED { // jitter in VBL scroll path — whole dungeon including top row
    uint8_t f;
    const int8_t off[] = { 2, -2, -1, 1, -2, 1, -1 }; // one frame shorter than old 8 for faster hit feedback
    for (f = 0; f < sizeof off; f++) {
        lcd_shake_x = off[f];
        lcd_shake_y = off[(f + 2u) % sizeof off];
        wait_vbl_done();
    }
    lcd_shake_x = 0;
    lcd_shake_y = 0;
}
