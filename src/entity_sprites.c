#include "entity_sprites.h"
#include "enemy.h"
#include "lcd.h"
#include "defs.h"
#include <gb/cgb.h>
#include <string.h>

#define SP_PLAYER 0
#define SP_ENEMY_BASE 1

static int16_t player_override_wx = -1; // negative = use px*8
static int16_t player_override_wy = -1;

static int8_t pl_ofs_x, pl_ofs_y;                    // attack lunge (player)
static int8_t en_ofs_x[MAX_ENEMIES], en_ofs_y[MAX_ENEMIES]; // per-slot lunge

static uint8_t lunge_amt_for_frame(uint8_t t) { // 0 .. 4 .. 0 over ENTITY_LUNGE_FRAMES (keep FRAMES >= 4)
    uint8_t last = (uint8_t)(ENTITY_LUNGE_FRAMES - 1u);
    uint8_t mid = last >> 1;
    uint8_t p   = (t > mid) ? (uint8_t)(last - t) : t;
    return (uint8_t)((4u * p) / mid);
}

static void oam_hide(uint8_t sp) { move_sprite(sp, 0u, 0u); } // OAM Y=0: off visible lines

static void move_entity_oam(uint8_t sp, int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
    int16_t dx = wx - (int16_t)camera_px;
    int16_t dy = wy - (int16_t)camera_py;
    uint8_t sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (uint8_t)dx);
    uint8_t sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + 8u + (uint8_t)dy);
    sx = (uint8_t)((int16_t)sx + (int16_t)lcd_shake_x);
    sy = (uint8_t)((int16_t)sy + (int16_t)lcd_shake_y);
    set_sprite_tile(sp, tile);
    set_sprite_prop(sp, (uint8_t)(pal & 7u)); // CGB palette index matches BG slot 0..7
    move_sprite(sp, sx, sy);
}

void entity_sprites_init(void) {
    uint8_t i;
    memset(en_ofs_x, 0, sizeof en_ofs_x);
    memset(en_ofs_y, 0, sizeof en_ofs_y);
    pl_ofs_x = pl_ofs_y = 0;
    player_override_wx = -1;
    player_override_wy = -1;
    for (i = 0; i < 40u; i++) oam_hide(i);
    SHOW_SPRITES;
}

void entity_sprites_set_player_world(int16_t wx, int16_t wy) {
    player_override_wx = wx;
    player_override_wy = wy;
}

void entity_sprites_clear_player_world(void) {
    player_override_wx = -1;
    player_override_wy = -1;
}

void entity_sprites_refresh(uint8_t px, uint8_t py) {
    uint8_t i;
    int16_t pwx, pwy;

    if (player_override_wx >= 0 && player_override_wy >= 0) {
        pwx = player_override_wx;
        pwy = player_override_wy;
    } else {
        pwx = (int16_t)px * 8;
        pwy = (int16_t)py * 8;
    }
    pwx += pl_ofs_x;
    pwy += pl_ofs_y;

    move_entity_oam(SP_PLAYER, pwx, pwy,
            (uint8_t)(TILESET_VRAM_OFFSET + PLAYER_TILE_OFFSET), PAL_PLAYER);

    for (i = 0; i < num_enemies; i++) {
        uint8_t sp = (uint8_t)(SP_ENEMY_BASE + i);
        if (enemy_x[i] == ENEMY_DEAD) {
            oam_hide(sp);
            continue;
        }
        {
            const EnemyDef *def = &enemy_defs[enemy_type[i]];
            uint8_t off = enemy_anim_toggle ? def->tile_alt : def->tile;
            uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + off);
            int16_t ewx = (int16_t)enemy_x[i] * 8 + en_ofs_x[i];
            int16_t ewy = (int16_t)enemy_y[i] * 8 + en_ofs_y[i];
            if (enemy_x[i] < CAM_TX || enemy_x[i] >= (uint8_t)(CAM_TX + GRID_W)
                    || enemy_y[i] < CAM_TY || enemy_y[i] >= (uint8_t)(CAM_TY + GRID_H)) {
                oam_hide(sp);
                continue;
            }
            move_entity_oam(sp, ewx, ewy, tt, def->palette);
        }
    }
    for (i = (uint8_t)(SP_ENEMY_BASE + num_enemies); i < 40u; i++) oam_hide(i);
}

void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dx, int8_t dy) {
    uint8_t t;
    for (t = 0; t < ENTITY_LUNGE_FRAMES; t++) {
        uint8_t a = lunge_amt_for_frame(t);
        pl_ofs_x = (int8_t)((int16_t)dx * (int16_t)a);
        pl_ofs_y = (int8_t)((int16_t)dy * (int16_t)a);
        entity_sprites_refresh(px, py);
        wait_vbl_done();
    }
    pl_ofs_x = pl_ofs_y = 0;
    entity_sprites_refresh(px, py);
}

void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py,
                                     const uint8_t *old_ex, const uint8_t *old_ey) {
    uint8_t i, any = 0;
    for (i = 0; i < num_enemies; i++) {
        if (enemy_x[i] == ENEMY_DEAD || old_ex[i] == ENEMY_DEAD) {
            en_ofs_x[i] = 0; en_ofs_y[i] = 0;
            continue;
        }
        en_ofs_x[i] = (int8_t)(((int16_t)old_ex[i] - (int16_t)enemy_x[i]) * 8);
        en_ofs_y[i] = (int8_t)(((int16_t)old_ey[i] - (int16_t)enemy_y[i]) * 8);
        if (en_ofs_x[i] || en_ofs_y[i]) any = 1;
    }
    if (!any) return;
    while (1) {
        any = 0;
        for (i = 0; i < num_enemies; i++) {
            if (en_ofs_x[i] > 0) en_ofs_x[i] = (en_ofs_x[i] > (int8_t)SCROLL_SPEED) ? (int8_t)(en_ofs_x[i] - SCROLL_SPEED) : 0;
            else if (en_ofs_x[i] < 0) en_ofs_x[i] = (en_ofs_x[i] < -(int8_t)SCROLL_SPEED) ? (int8_t)(en_ofs_x[i] + SCROLL_SPEED) : 0;
            if (en_ofs_y[i] > 0) en_ofs_y[i] = (en_ofs_y[i] > (int8_t)SCROLL_SPEED) ? (int8_t)(en_ofs_y[i] - SCROLL_SPEED) : 0;
            else if (en_ofs_y[i] < 0) en_ofs_y[i] = (en_ofs_y[i] < -(int8_t)SCROLL_SPEED) ? (int8_t)(en_ofs_y[i] + SCROLL_SPEED) : 0;
            if (en_ofs_x[i] || en_ofs_y[i]) any = 1;
        }
        entity_sprites_refresh(px, py);
        wait_vbl_done();
        if (!any) break;
    }
    memset(en_ofs_x, 0, sizeof en_ofs_x);
    memset(en_ofs_y, 0, sizeof en_ofs_y);
}

void entity_sprites_run_enemy_lunge(uint8_t px, uint8_t py, uint8_t slot, uint8_t tgx, uint8_t tgy) {
    uint8_t t;
    int8_t sx = (int8_t)tgx - (int8_t)enemy_x[slot];
    int8_t sy = (int8_t)tgy - (int8_t)enemy_y[slot];
    if (sx > 1) sx = 1;
    if (sx < -1) sx = -1;
    if (sy > 1) sy = 1;
    if (sy < -1) sy = -1;
    for (t = 0; t < ENTITY_LUNGE_FRAMES; t++) {
        uint8_t a = lunge_amt_for_frame(t);
        en_ofs_x[slot] = (int8_t)((int16_t)sx * (int16_t)a);
        en_ofs_y[slot] = (int8_t)((int16_t)sy * (int16_t)a);
        entity_sprites_refresh(px, py);
        wait_vbl_done();
    }
    en_ofs_x[slot] = en_ofs_y[slot] = 0;
    entity_sprites_refresh(px, py);
}

void entity_sprites_run_enemy_lunges_batch(uint8_t px, uint8_t py,
                                            const uint8_t *slots, uint8_t count) { // all attackers lunge toward player simultaneously
    int8_t dir_x[MAX_ENEMIES], dir_y[MAX_ENEMIES];
    uint8_t i, t;
    for (i = 0; i < count; i++) {
        uint8_t s = slots[i];
        int8_t dx = (int8_t)px - (int8_t)enemy_x[s];
        int8_t dy = (int8_t)py - (int8_t)enemy_y[s];
        if (dx > 1) dx = 1; if (dx < -1) dx = -1;
        if (dy > 1) dy = 1; if (dy < -1) dy = -1;
        dir_x[i] = dx;
        dir_y[i] = dy;
    }
    for (t = 0; t < ENTITY_LUNGE_FRAMES; t++) {
        uint8_t a = lunge_amt_for_frame(t);
        for (i = 0; i < count; i++) {
            uint8_t s = slots[i];
            en_ofs_x[s] = (int8_t)((int16_t)dir_x[i] * (int16_t)a);
            en_ofs_y[s] = (int8_t)((int16_t)dir_y[i] * (int16_t)a);
        }
        entity_sprites_refresh(px, py);
        wait_vbl_done();
    }
    for (i = 0; i < count; i++) {
        uint8_t s = slots[i];
        en_ofs_x[s] = en_ofs_y[s] = 0;
    }
    entity_sprites_refresh(px, py);
}
