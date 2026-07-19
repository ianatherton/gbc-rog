#pragma bank 2

#include "camera.h"
#include "defs.h" // camera_px, camera_py, GRID_W/H, MAP_W/H, SCROLL_SPEED
#include "enemy.h"
#include "entity_sprites.h"
#include "lcd.h"
#include "render.h"
#include "perf.h"
#include "auto_explore.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(entity_sprites_set_player_world)
BANKREF_EXTERN(entity_sprites_clear_player_world)
BANKREF_EXTERN(entity_sprites_refresh_oam_only)
BANKREF_EXTERN(entity_sprites_refresh_all)
BANKREF_EXTERN(entity_sprites_enemy_glide_step)

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
    uint8_t spd = auto_explore_active ? AUTO_SCROLL_SPEED : SCROLL_SPEED;
    while (wx != ex || wy != ey) {
        if (wx < ex) { wx += spd; if (wx > ex) wx = ex; }
        else if (wx > ex) { if ((wx - ex) <= spd) wx = ex; else wx -= spd; }
        if (wy < ey) { wy += spd; if (wy > ey) wy = ey; }
        else if (wy > ey) { if ((wy - ey) <= spd) wy = ey; else wy -= spd; }
        entity_sprites_set_player_world(wx, (int16_t)(wy + player_bob_table[bob_i]), wx, wy);
        bob_i = (uint8_t)((bob_i + 1u) & 7u);
        entity_sprites_enemy_glide_step(); // step enemy glide offsets while player sprite eases
        entity_sprites_refresh_oam_only(px, py);
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
    uint8_t spd = auto_explore_active ? AUTO_SCROLL_SPEED : SCROLL_SPEED;

    if (camera_px == target_px && camera_py == target_py && (opx != px || opy != py)) {
        player_glide_when_camera_idle(opx, opy, px, py);
        perf_record(PERF_CAMERA_SCROLL, perf_stamp_elapsed(&perf_stamp));
        return;
    }

    // Pre-classify (CPU only, into render_strip_*) the strip this move will reveal, BEFORE the glide, so
    // the tile-cross frame only has to BLIT (a VBlank-safe VRAM write) — no per-cell classification on
    // any scroll frame, and even pacing across all directions. Only the pure 1-tile axis move (the
    // common case) is prepped; diagonal / multi-tile scrolls fall back to classify+blit at the cross.
    uint8_t prep_kind = 0u; // 0 = none, 1 = column, 2 = row
    uint8_t prep_idx  = 0u; // map column/row index the buffer was classified for
    {
        int8_t dtx = (int8_t)((int16_t)target_tx - (int16_t)(uint8_t)(camera_px >> 3));
        int8_t dty = (int8_t)((int16_t)target_ty - (int16_t)(uint8_t)(camera_py >> 3));
        if (dty == 0 && (dtx == 1 || dtx == -1)) {
            prep_kind = 1u;
            prep_idx  = (dtx == 1) ? (uint8_t)(target_tx + GRID_W) : target_tx;
            classify_col_strip(prep_idx, target_ty);
        } else if (dtx == 0 && (dty == 1 || dty == -1)) {
            prep_kind = 2u;
            prep_idx  = (dty == 1) ? (uint8_t)(target_ty + GRID_H) : target_ty;
            classify_row_strip(prep_idx, target_tx);
        }
    }

    while (camera_px != target_px || camera_py != target_py) {
        if (++guard_steps > 2048u) { camera_px = target_px; camera_py = target_py; entity_sprites_clear_player_world(); entity_sprites_refresh_all(px, py); break; }
        uint8_t old_ctx = (uint8_t)(camera_px >> 3);
        uint8_t old_cty = (uint8_t)(camera_py >> 3);

        if (camera_px < target_px) { camera_px += spd; if (camera_px > target_px) camera_px = target_px; }
        else if (camera_px > target_px) {
            if ((camera_px - target_px) <= spd) camera_px = target_px;
            else camera_px -= spd;
        }
        if (camera_py < target_py) { camera_py += spd; if (camera_py > target_py) camera_py = target_py; }
        else if (camera_py > target_py) {
            if ((camera_py - target_py) <= spd) camera_py = target_py;
            else camera_py -= spd;
        }

        if (pwx < target_pwx) { pwx += spd; if (pwx > target_pwx) pwx = target_pwx; }
        else if (pwx > target_pwx) { pwx -= spd; if (pwx < target_pwx) pwx = target_pwx; }
        if (pwy < target_pwy) { pwy += spd; if (pwy > target_pwy) pwy = target_pwy; }
        else if (pwy > target_pwy) { pwy -= spd; if (pwy < target_pwy) pwy = target_pwy; }
        entity_sprites_set_player_world(pwx, (int16_t)(pwy + player_bob_table[bob_i]), pwx, pwy);
        bob_i = (uint8_t)((bob_i + 1u) & 7u);
        entity_sprites_enemy_glide_step(); // step enemy glide offsets alongside camera pan
        entity_sprites_refresh_oam_only(px, py); // shadow OAM (WRAM) — safe before VBL; DMA picks it up this frame

        wait_vbl_done(); // VBlank budget now free for VRAM-only strip draws

        {
            uint8_t new_ctx = (uint8_t)(camera_px >> 3);
            uint8_t new_cty = (uint8_t)(camera_py >> 3);
            uint8_t drew_strip = 0u;
            if (new_ctx != old_ctx) {
                uint8_t col = new_ctx > old_ctx ? (uint8_t)(new_ctx + GRID_W) : new_ctx;
                if (prep_kind == 1u && col == prep_idx) { // pre-classified at entry — blit only
                    render_blit_strip_col((uint8_t)(col & 31u), (uint8_t)(CAM_TY & 31u), (uint8_t)(GRID_H + 1u));
                    prep_kind = 0u;
                } else draw_col_strip(col);
                drew_strip = 1u;
            }
            if (new_cty != old_cty) {
                uint8_t row = new_cty > old_cty ? (uint8_t)(new_cty + GRID_H) : new_cty;
                if (prep_kind == 2u && row == prep_idx) {
                    render_blit_strip_row((uint8_t)(row & 31u), (uint8_t)(CAM_TX & 31u), (uint8_t)(GRID_W + 1u));
                    prep_kind = 0u;
                } else draw_row_strip(row);
                drew_strip = 1u;
            }
            if (drew_strip) draw_ui_rows();
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
