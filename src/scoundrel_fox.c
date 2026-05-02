#pragma bank 3

#include "scoundrel_fox.h"
#include "globals.h"
#include "defs.h"
#include "map.h"
#include "enemy.h"
#include "combat.h"
#include "lcd.h"
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/platform.h>
#include <rand.h>

#define FOX_BLINK_RANGE 3u
#define FOX_LEASH_CHEB  5u
#define FOX_AGGRO_CHEB  4u

static uint8_t cheb_dist(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by) {
    uint8_t dx = (ax > bx) ? (uint8_t)(ax - bx) : (uint8_t)(bx - ax);
    uint8_t dy = (ay > by) ? (uint8_t)(ay - by) : (uint8_t)(by - ay);
    return (dx > dy) ? dx : dy;
}

static void fox_step_random(uint8_t sx, uint8_t sy, uint8_t *nx, uint8_t *ny) {
    uint8_t attempt;
    for (attempt = 0; attempt < 4; attempt++) {
        uint8_t d  = rand() >> 6;
        uint8_t cx = sx, cy = sy;
        if      (d == 0 && sy > 0)         cy = sy - 1;
        else if (d == 1 && sy < MAP_H - 1) cy = sy + 1;
        else if (d == 2 && sx > 0)         cx = sx - 1;
        else if (d == 3 && sx < MAP_W - 1) cx = sx + 1;
        else continue;
        if (is_walkable(cx, cy)) { *nx = cx; *ny = cy; return; }
    }
    *nx = sx;
    *ny = sy;
}

static void fox_blink_toward(uint8_t sx, uint8_t sy, uint8_t tx, uint8_t ty, uint8_t max_range,
                             uint8_t *nx, uint8_t *ny) {
    uint8_t apx = (tx > sx) ? (uint8_t)(tx - sx) : (uint8_t)(sx - tx);
    uint8_t apy = (ty > sy) ? (uint8_t)(ty - sy) : (uint8_t)(sy - ty);
    uint8_t cheb_t = (apx > apy) ? apx : apy;
    int8_t ox, oy;
    uint8_t best_x = sx, best_y = sy;
    uint8_t best_d = 0xFFu;

    if (cheb_t <= 1u) { *nx = sx; *ny = sy; return; }

    for (oy = -1; oy <= 1; oy++) {
        for (ox = -1; ox <= 1; ox++) {
            if (ox == 0 && oy == 0) continue;
            { int16_t ux = (int16_t)tx + ox;
              int16_t uy = (int16_t)ty + oy;
              if (ux < 0 || ux >= MAP_W || uy < 0 || uy >= MAP_H) continue;
              { uint8_t utx = (uint8_t)ux, uty = (uint8_t)uy;
                uint8_t bx  = (utx > sx) ? (uint8_t)(utx - sx) : (uint8_t)(sx - utx);
                uint8_t by  = (uty > sy) ? (uint8_t)(uty - sy) : (uint8_t)(sy - uty);
                uint8_t cd  = (bx > by) ? bx : by;
                if (cd == 0u || cd > max_range)             continue;
                if (!is_walkable(utx, uty))                 continue;
                if (tile_at(utx, uty) == TILE_PIT)          continue;
                if (enemy_at(utx, uty) != ENEMY_DEAD)       continue;
                if (cd < best_d) { best_d = cd; best_x = utx; best_y = uty; }
              }
            }
        }
    }

    if (best_d != 0xFFu) { *nx = best_x; *ny = best_y; return; }
    fox_step_random(sx, sy, nx, ny);
}

BANKREF(scoundrel_fox_clear)
void scoundrel_fox_clear(void) BANKED {
    scoundrel_fox_active    = 0u;
    scoundrel_fox_chase_ei  = ENEMY_DEAD;
}

static uint8_t fox_pick_aggro_target(uint8_t px, uint8_t py) {
    uint8_t best = ENEMY_DEAD;
    uint8_t best_d = 255u;
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        uint8_t d;
        if (!enemy_alive[i]) continue;
        d = cheb_dist(enemy_x[i], enemy_y[i], px, py);
        if (d > FOX_AGGRO_CHEB) continue;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

BANKREF(scoundrel_fox_summon)
void scoundrel_fox_summon(uint8_t px, uint8_t py) BANKED {
    static const int8_t OX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int8_t OY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    uint8_t start = (uint8_t)(rand() & 7u);
    uint8_t t;
    for (t = 0; t < 8u; t++) {
        uint8_t i = (uint8_t)((start + t) & 7u);
        int16_t x = (int16_t)px + OX[i];
        int16_t y = (int16_t)py + OY[i];
        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
        { uint8_t ux = (uint8_t)x, uy = (uint8_t)y;
          if (!is_walkable(ux, uy)) continue;
          if (tile_at(ux, uy) == TILE_PIT) continue;
          if (enemy_at(ux, uy) != ENEMY_DEAD) continue;
          scoundrel_fox_x = ux;
          scoundrel_fox_y = uy;
          scoundrel_fox_chase_ei = ENEMY_DEAD;
          scoundrel_fox_active = 1u;
          return;
        }
    }
    scoundrel_fox_x = px;
    scoundrel_fox_y = py;
    scoundrel_fox_chase_ei = ENEMY_DEAD;
    scoundrel_fox_active = 1u;
}

BANKREF(scoundrel_fox_run_glide)
void scoundrel_fox_run_glide(uint8_t old_fx, uint8_t old_fy) BANKED { // direct OAM during ease; final OAM resync happens in next entity_sprites_refresh_oam_only
    int8_t gx, gy;
    if (!scoundrel_fox_active) return;
    gx = (int8_t)(((int16_t)old_fx - (int16_t)scoundrel_fox_x) * 8);
    gy = (int8_t)(((int16_t)old_fy - (int16_t)scoundrel_fox_y) * 8);
    if (!gx && !gy) return;
    while (1) {
        uint8_t any = 0u;
        if (gx > 0) gx = (gx > (int8_t)SCROLL_SPEED) ? (int8_t)(gx - SCROLL_SPEED) : 0;
        else if (gx < 0) gx = (gx < -(int8_t)SCROLL_SPEED) ? (int8_t)(gx + SCROLL_SPEED) : 0;
        if (gy > 0) gy = (gy > (int8_t)SCROLL_SPEED) ? (int8_t)(gy - SCROLL_SPEED) : 0;
        else if (gy < 0) gy = (gy < -(int8_t)SCROLL_SPEED) ? (int8_t)(gy + SCROLL_SPEED) : 0;
        if (gx || gy) any = 1u;
        {
            int16_t wx = (int16_t)scoundrel_fox_x * 8 + gx;
            int16_t wy = (int16_t)scoundrel_fox_y * 8 + gy;
            int16_t dx = wx - (int16_t)camera_px;
            int16_t dy = wy - (int16_t)camera_py;
            uint8_t sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (uint8_t)dx + (int16_t)lcd_shake_x);
            uint8_t sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint8_t)dy + (int16_t)lcd_shake_y);
            move_sprite(SP_SCOUNDREL_FOX, sx, sy); // tile + palette already set by prior refresh — just slide
        }
        wait_vbl_done();
        if (!any) break;
    }
}

BANKREF(scoundrel_fox_turn_tick)
uint8_t scoundrel_fox_turn_tick(uint8_t px, uint8_t py) BANKED {
    uint8_t nx, ny;
    uint8_t ei;
    uint8_t dmg;
    if (!scoundrel_fox_active || player_class != 1u) return 0u;

    if (cheb_dist(scoundrel_fox_x, scoundrel_fox_y, px, py) > FOX_LEASH_CHEB)
        scoundrel_fox_chase_ei = ENEMY_DEAD;

    ei = scoundrel_fox_chase_ei;
    if (ei != ENEMY_DEAD) {
        if (ei >= num_enemies || !enemy_alive[ei]
                || cheb_dist(enemy_x[ei], enemy_y[ei], px, py) > FOX_AGGRO_CHEB)
            scoundrel_fox_chase_ei = ENEMY_DEAD;
    }

    if (scoundrel_fox_chase_ei == ENEMY_DEAD)
        scoundrel_fox_chase_ei = fox_pick_aggro_target(px, py);

    ei = scoundrel_fox_chase_ei;
    if (ei != ENEMY_DEAD && ei < num_enemies && enemy_alive[ei]) {
        uint8_t ex = enemy_x[ei], ey = enemy_y[ei];
        if (cheb_dist(scoundrel_fox_x, scoundrel_fox_y, ex, ey) <= 1u) {
            dmg = player_damage ? player_damage : 1u;
            return combat_damage_enemy(ei, dmg, 0u);
        }
        fox_blink_toward(scoundrel_fox_x, scoundrel_fox_y, ex, ey, FOX_BLINK_RANGE, &nx, &ny);
    } else {
        fox_blink_toward(scoundrel_fox_x, scoundrel_fox_y, px, py, FOX_BLINK_RANGE, &nx, &ny);
    }

    if (nx != scoundrel_fox_x || ny != scoundrel_fox_y) {
        if (tile_at(nx, ny) == TILE_PIT) {
            scoundrel_fox_clear();
            return 0u;
        }
        scoundrel_fox_x = nx;
        scoundrel_fox_y = ny;
    }
    return 0u;
}
