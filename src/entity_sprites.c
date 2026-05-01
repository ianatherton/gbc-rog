#include "entity_sprites.h"
#include "render.h"
#include "enemy.h"
#include "globals.h"
#include "lcd.h"
#include "defs.h"
#include "map.h"
#include "map.h"
#include <gb/cgb.h>
#include <string.h>

#define PLAYER_HURT_FLASH_DURATION_VBL 60u // 1 s at ~60 Hz VBlank
#define PLAYER_HURT_FLASH_TOGGLE_VBL    8u // red vs gold half-beat (~7.5 full cycles/s)

#define SP_LADDER_ARROW 36u
#define SP_BRAZIER_FIRE 37u
#define SP_BELT_SELECTOR 35u // fixed screen-space OAM; excluded from post-enemy hide sweep
#define BRAZIER_FIRE_TTL_VBL 12u

static uint8_t brazier_fire_active;
static uint8_t brazier_fire_ttl;
static int16_t brazier_fire_wx, brazier_fire_wy;
static int8_t brazier_fire_dx;
static uint8_t brazier_fire_source_cursor;
static uint8_t ladder_arrow_phase;
static uint8_t ladder_arrow_tick;
static uint8_t ladder_cache_valid; // 1 when pit coords below are usable (set during refresh, read by VBL)
static uint8_t ladder_cache_mx, ladder_cache_my;
static const int8_t ladder_arrow_bob12[12] = { 0, 1, 2, 2, 1, 0, -1, -2, -2, -1, 0, 0 };

static int16_t player_override_wx = -1; // negative = use px*8
static int16_t player_override_wy = -1;
static int16_t player_override_aura_wx = -1; // glide only — world pos for aura (no walk bob on Y)
static int16_t player_override_aura_wy = -1;
static uint8_t player_flip_x;
static uint8_t player_hurt_flash_ttl;
static uint8_t player_hurt_flash_restore_needed; // 1 after flash until OCP2 restored to gold
static uint8_t player_cache_tx, player_cache_ty; // last refresh tile — VBL hurt blink repaints without full refresh
static uint8_t player_aura_vbl_sub;               // counts VBL within one A/B hold window
static uint8_t player_aura_ab_idx;               // 0 = tile A, 1 = tile B — toggles each PLAYER_AURA_TOGGLE_VBL
static uint8_t projectile_overrides_aura;        // 1: slot 0 is bolt/fireball — skip aura VBL/refresh so FX draws above SP_PLAYER

static int8_t pl_ofs_x, pl_ofs_y;                    // attack lunge (player)
static int8_t en_ofs_x[MAX_ENEMIES], en_ofs_y[MAX_ENEMIES]; // per-slot lunge
static uint8_t enemy_poof_ttl[MAX_ENEMIES];          // dead slot: VBlanks left showing TILE_POOF_CLOUD (OCP0)
static uint8_t en_hit_flash_age[MAX_ENEMIES];       // 0 off; 1..ENEMY_HIT_FLASH_VBL — grey hit pulses in refresh_enemy_oam

#define ENEMY_POOF_DURATION_VBL 22u // ~370ms @60Hz — overlaps corpse then clears
#define ENEMY_HIT_FLASH_VBL     8u // two 2-VBL pulses OCP0 vs native — ages 1..8 then clear
#define PLAYER_AURA_TOGGLE_VBL  15u // ~0.25s @ ~60Hz — M15 ↔ M16 (was every VBL; too fast to read)

static uint8_t lunge_amt_for_frame(uint8_t t) { // 0 .. 4 .. 0 over ENTITY_LUNGE_FRAMES (keep FRAMES >= 4)
    uint8_t last = (uint8_t)(ENTITY_LUNGE_FRAMES - 1u);
    uint8_t mid = last >> 1;
    uint8_t p   = (t > mid) ? (uint8_t)(last - t) : t;
    return (uint8_t)((4u * p) / mid);
}

static void oam_hide(uint8_t sp) { move_sprite(sp, 0u, 0u); } // OAM Y=0: off visible lines

static void refresh_buff_icon_oam(void) { // top-right HUD slot — show I9 shield while knight buff is up, hide otherwise
    if (!lcd_gameplay_active || !knight_shield_active) {
        oam_hide(SP_BUFF_ICON);
        return;
    }
    set_sprite_tile(SP_BUFF_ICON, TILE_KNIGHT_SHIELD_VRAM);
    set_sprite_prop(SP_BUFF_ICON, (uint8_t)(PAL_LIFE_UI & 7u)); // active = warm/red ramp; future buffs can pick their own palette
    move_sprite(SP_BUFF_ICON, // OAM (160, 16) → on-screen (152, 0): top-right 8×8
        (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + 152u),
        (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + 0u));
}

static void refresh_belt_selector_oam(void) { // M5 arrow on dungeon row GRID_H-1 (above belt / window band)
    uint8_t s, sx, sy, tt;
    if (!lcd_gameplay_active) {
        oam_hide(SP_BELT_SELECTOR);
        return;
    }
    s = (uint8_t)(selected_belt_slot % BELT_SLOT_COUNT);
    tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_LADDER);
    sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + 7u + (uint16_t)(2u + (uint16_t)s * 2u) * 8u - 8u); // one tile left — over slot icon (M5 art leans right)
    sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint16_t)(GRID_H - 1u) * 8u + 2u);
    set_sprite_tile(SP_BELT_SELECTOR, tt);
    set_sprite_prop(SP_BELT_SELECTOR, (uint8_t)(PAL_XP_UI & 7u));
    move_sprite(SP_BELT_SELECTOR, sx, sy);
}

static uint8_t player_tile_offset_for_class(void) {
    if (player_class == 1u) return TILE_CLASS_SCOUNDREL; // rogue — B4
    if (player_class == 2u) return TILE_CLASS_WITCH; // B3
    if (player_class == 3u) return TILE_CLASS_BERSERKER; // Zerker — B2
    return TILE_CLASS_KNIGHT;
}

static uint8_t pick_next_visible_light_source(uint8_t *mx, uint8_t *my) {
    uint8_t i;
    if (brazier_count == 0u) return 0u;
    for (i = 0u; i < brazier_count; i++) {
        uint8_t idx = (uint8_t)((brazier_fire_source_cursor + i) % brazier_count);
        uint8_t x = brazier_x[idx], y = brazier_y[idx];
        if (x < CAM_TX || x >= (uint8_t)(CAM_TX + GRID_W)) continue;
        if (y < CAM_TY || y >= (uint8_t)(CAM_TY + GRID_H)) continue;
        *mx = x;
        *my = y;
        brazier_fire_source_cursor = (uint8_t)((idx + 1u) % brazier_count);
        return 1u;
    }
    return 0u;
}

static void brazier_fire_try_spawn(void) {
    uint8_t mx, my;
    if (!lcd_gameplay_active || !pick_next_visible_light_source(&mx, &my)) {
        brazier_fire_active = 0u;
        oam_hide(SP_BRAZIER_FIRE);
        return;
    }
    brazier_fire_active = 1u;
    brazier_fire_ttl = BRAZIER_FIRE_TTL_VBL;
    brazier_fire_wx = (int16_t)mx * 8 + 4;
    brazier_fire_wy = (int16_t)my * 8 - 2; // start just above the source tile top edge
    brazier_fire_dx = (int8_t)((DIV_REG & 1u) ? 1 : -1);
}

static void move_entity_oam(uint8_t sp, int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
    int16_t dx = wx - (int16_t)camera_px;
    int16_t dy = wy - (int16_t)camera_py;
    uint8_t sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (uint8_t)dx);
    uint8_t sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint8_t)dy);
    sx = (uint8_t)((int16_t)sx + (int16_t)lcd_shake_x);
    sy = (uint8_t)((int16_t)sy + (int16_t)lcd_shake_y);
    set_sprite_tile(sp, tile);
    {
        uint8_t prop = (uint8_t)(pal & 7u); // CGB palette index matches BG slot 0..7
        if (sp == SP_PLAYER && player_flip_x) prop |= S_FLIPX;
        if (sp == SP_PLAYER_AURA_OAM && player_flip_x && !projectile_overrides_aura) prop |= S_FLIPX;
        set_sprite_prop(sp, prop);
    }
    move_sprite(sp, sx, sy);
}

static void refresh_scoundrel_fox_oam(void) {
    uint8_t fx, fy;
    if (!lcd_gameplay_active || player_class != 1u || !scoundrel_fox_active) {
        oam_hide(SP_SCOUNDREL_FOX);
        return;
    }
    fx = scoundrel_fox_x;
    fy = scoundrel_fox_y;
    if (fx < CAM_TX || fx >= (uint8_t)(CAM_TX + GRID_W)
            || fy < CAM_TY || fy >= (uint8_t)(CAM_TY + GRID_H)
            || !lighting_is_revealed(fx, fy)) {
        oam_hide(SP_SCOUNDREL_FOX);
        return;
    }
    move_entity_oam(SP_SCOUNDREL_FOX, (int16_t)fx * 8, (int16_t)fy * 8,
            TILE_FOX_J9_VRAM, PAL_ENEMY_BAT);
}

void entity_sprites_poof_clear_all(void) {
    memset(enemy_poof_ttl, 0, sizeof enemy_poof_ttl);
    memset(en_hit_flash_age, 0, sizeof en_hit_flash_age);
}

void entity_sprites_enemy_poof_begin(uint8_t slot) {
    if (slot < MAX_ENEMIES) enemy_poof_ttl[slot] = ENEMY_POOF_DURATION_VBL;
}

void entity_sprites_init(void) {
    uint8_t i;
    entity_sprites_poof_clear_all();
    memset(en_ofs_x, 0, sizeof en_ofs_x);
    memset(en_ofs_y, 0, sizeof en_ofs_y);
    pl_ofs_x = pl_ofs_y = 0;
    player_override_wx = -1;
    player_override_wy = -1;
    player_override_aura_wx = -1;
    player_override_aura_wy = -1;
    player_flip_x = 0u;
    player_hurt_flash_ttl = 0u;
    player_hurt_flash_restore_needed = 0u;
    brazier_fire_active = 0u;
    brazier_fire_ttl = 0u;
    brazier_fire_source_cursor = 0u;
    ladder_arrow_phase = 0u;
    ladder_arrow_tick = 0u;
    ladder_cache_valid = 0u;
    player_aura_vbl_sub = 0u;
    player_aura_ab_idx = 0u;
    for (i = 0; i < 40u; i++) oam_hide(i);
    SHOW_SPRITES;
}

void entity_sprites_set_player_facing(int8_t dir_x) {
    if (dir_x < 0) player_flip_x = 1u;
    else if (dir_x > 0) player_flip_x = 0u;
}

void entity_sprites_player_hurt_flash(void) {
    player_hurt_flash_ttl = PLAYER_HURT_FLASH_DURATION_VBL;
}

static void player_world_xy_for_oam(int16_t *pwx, int16_t *pwy) {
    if (player_override_wx >= 0 && player_override_wy >= 0) {
        *pwx = player_override_wx;
        *pwy = player_override_wy;
    } else {
        *pwx = (int16_t)player_cache_tx * 8;
        *pwy = (int16_t)player_cache_ty * 8;
    }
    *pwx += pl_ofs_x;
    *pwy += pl_ofs_y;
}

/* Glide: aura uses bob-free track when set; else sprite override or tile — never pl_ofs (combat lunge only). */
static void player_aura_world_xy(int16_t *awx, int16_t *awy) {
    if (player_override_aura_wx >= 0 && player_override_aura_wy >= 0) {
        *awx = player_override_aura_wx;
        *awy = player_override_aura_wy;
    } else if (player_override_wx >= 0 && player_override_wy >= 0) {
        *awx = player_override_wx;
        *awy = player_override_wy;
    } else {
        *awx = (int16_t)player_cache_tx * 8;
        *awy = (int16_t)player_cache_ty * 8;
    }
}

static uint8_t player_aura_tile_vram(void) {
    return player_aura_ab_idx ? TILE_PLAYER_AURA_VRAM_B : TILE_PLAYER_AURA_VRAM_A;
}

static void refresh_player_aura_oam_vbl(void) { // position every VBL; M15/M16 swap on PLAYER_AURA_TOGGLE_VBL cadence
    int16_t ax, ay;
    if (!lcd_gameplay_active || projectile_overrides_aura) return;
    player_aura_world_xy(&ax, &ay);
    move_entity_oam(SP_PLAYER_AURA_OAM, ax, (int16_t)(ay + 3), player_aura_tile_vram(), PAL_XP_UI);
}

static void refresh_player_oam_from_cache(void) { // player only — same math as entity_sprites_refresh player block
    int16_t pwx, pwy;
    uint8_t player_pal = PAL_PLAYER;
    player_world_xy_for_oam(&pwx, &pwy);
    if (player_hurt_flash_ttl > 0u) {
        uint8_t blk = (uint8_t)((PLAYER_HURT_FLASH_DURATION_VBL - player_hurt_flash_ttl) / PLAYER_HURT_FLASH_TOGGLE_VBL);
        if ((blk & 1u) == 0u) render_sprite_palette_player_hurt();
        else                     render_sprite_palette_player_default();
        player_hurt_flash_restore_needed = 1u;
    } else if (player_hurt_flash_restore_needed) {
        render_sprite_palette_player_default();
        player_hurt_flash_restore_needed = 0u;
    }
    if (lcd_gameplay_active && !projectile_overrides_aura) {
        int16_t ax, ay;
        player_aura_world_xy(&ax, &ay);
        move_entity_oam(SP_PLAYER_AURA_OAM, ax, (int16_t)(ay + 3), player_aura_tile_vram(), PAL_XP_UI);
    } else if (!lcd_gameplay_active) {
        oam_hide(SP_PLAYER_AURA_OAM);
    }
    move_entity_oam(SP_PLAYER, pwx, pwy,
            (uint8_t)(TILESET_VRAM_OFFSET + player_tile_offset_for_class()), player_pal);
}

static void refresh_enemy_oam(uint8_t slot) {
    uint8_t sp = (uint8_t)(SP_ENEMY_BASE + slot);
    if (slot >= num_enemies) {
        oam_hide(sp);
        return;
    }
    if (!enemy_alive[slot]) {
        if (enemy_poof_ttl[slot] > 0u) {
            uint8_t mx = enemy_x[slot], my = enemy_y[slot];
            if (mx < CAM_TX || mx >= (uint8_t)(CAM_TX + GRID_W)
                    || my < CAM_TY || my >= (uint8_t)(CAM_TY + GRID_H)
                    || !lighting_is_revealed(mx, my)) {
                oam_hide(sp);
                return;
            }
            move_entity_oam(sp, (int16_t)mx * 8 + en_ofs_x[slot], (int16_t)my * 8 + en_ofs_y[slot],
                    (uint8_t)(TILESET_VRAM_OFFSET + TILE_POOF_CLOUD), 0u); // OCP0 = pal_default (light/white ramp)
            return;
        }
        oam_hide(sp);
        return;
    }
    {
        const EnemyDef *def = &enemy_defs[enemy_type[slot]];
        uint8_t off = enemy_anim_toggle ? def->tile_alt : def->tile;
        uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + off);
        int16_t ewx = (int16_t)enemy_x[slot] * 8 + en_ofs_x[slot];
        int16_t ewy = (int16_t)enemy_y[slot] * 8 + en_ofs_y[slot];
        if (enemy_x[slot] < CAM_TX || enemy_x[slot] >= (uint8_t)(CAM_TX + GRID_W)
                || enemy_y[slot] < CAM_TY || enemy_y[slot] >= (uint8_t)(CAM_TY + GRID_H)
                || !lighting_is_revealed(enemy_x[slot], enemy_y[slot])) {
            oam_hide(sp);
            return;
        }
        {
            uint8_t pal = def->palette;
            uint8_t h = en_hit_flash_age[slot];
            if (h > 0u && h <= ENEMY_HIT_FLASH_VBL) {
                uint8_t age0 = (uint8_t)(h - 1u); // 0..7 — two quick OCP0 bands: ages 1-2, 5-6
                if (((age0 >> 1) & 1u) == 0u) pal = 0u; // OCP0 grey ramp vs native enemy ramp
            }
            move_entity_oam(sp, ewx, ewy, tt, pal);
        }
    }
}

void entity_sprites_vbl_tick(void) {
    if (brazier_fire_active && brazier_fire_ttl > 0u) {
        brazier_fire_wy--;
        if ((brazier_fire_ttl & 1u) == 0u) brazier_fire_wx += brazier_fire_dx;
        brazier_fire_ttl--;
        move_entity_oam(SP_BRAZIER_FIRE, brazier_fire_wx, brazier_fire_wy,
                (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE), PAL_WALL_BG);
    } else {
        brazier_fire_try_spawn();
    }
    if (lcd_gameplay_active && ladder_cache_valid) {
        uint8_t mx = ladder_cache_mx, my = ladder_cache_my;
        if (my > 0u
                && mx >= CAM_TX && mx < (uint8_t)(CAM_TX + GRID_W)
                && (uint8_t)(my - 1u) >= CAM_TY && (uint8_t)(my - 1u) < (uint8_t)(CAM_TY + GRID_H)
                && lighting_is_revealed(mx, my)) {
            uint8_t bob;
            if (++ladder_arrow_tick >= 2u) {
                ladder_arrow_tick = 0u;
                ladder_arrow_phase = (uint8_t)((ladder_arrow_phase + 1u) % 12u);
            }
            bob = ladder_arrow_phase;
            {
                int16_t wx = (int16_t)mx * 8;
                int16_t wy = (int16_t)(my - 1u) * 8 + ladder_arrow_bob12[bob];
                move_entity_oam(SP_LADDER_ARROW, wx, wy, (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_LADDER), 0u);
            }
        } else {
            oam_hide(SP_LADDER_ARROW);
        }
    } else {
        oam_hide(SP_LADDER_ARROW);
    }
    if (player_hurt_flash_ttl > 0u) {
        refresh_player_oam_from_cache(); // palette + OAM before ttl tick so all 60 frames flash
        player_hurt_flash_ttl--;
    }
    if (lcd_gameplay_active) {
        uint8_t pi;
        if (++player_aura_vbl_sub >= PLAYER_AURA_TOGGLE_VBL) {
            player_aura_vbl_sub = 0u;
            player_aura_ab_idx ^= 1u;
        }
        refresh_player_aura_oam_vbl();
        for (pi = 0u; pi < num_enemies; pi++) {
            if (enemy_poof_ttl[pi] > 0u) {
                enemy_poof_ttl[pi]--;
                refresh_enemy_oam(pi);
            } else if (en_hit_flash_age[pi] > 0u && enemy_alive[pi]) {
                en_hit_flash_age[pi]++;
                if (en_hit_flash_age[pi] > ENEMY_HIT_FLASH_VBL) en_hit_flash_age[pi] = 0u;
                refresh_enemy_oam(pi);
            }
        }
    }
}

void entity_sprites_set_player_world(int16_t spr_wx, int16_t spr_wy, int16_t aura_wx, int16_t aura_wy) {
    player_override_wx = spr_wx;
    player_override_wy = spr_wy;
    player_override_aura_wx = aura_wx;
    player_override_aura_wy = aura_wy;
}

void entity_sprites_clear_player_world(void) {
    player_override_wx = -1;
    player_override_wy = -1;
    player_override_aura_wx = -1;
    player_override_aura_wy = -1;
}

void entity_sprites_refresh_player_only(uint8_t px, uint8_t py) {
    player_cache_tx = px;
    player_cache_ty = py;
    refresh_player_oam_from_cache();
}

void entity_sprites_refresh_enemy(uint8_t slot) {
    refresh_enemy_oam(slot);
}

void entity_sprites_refresh_oam_only(uint8_t px, uint8_t py) {
    uint8_t i;
    entity_sprites_refresh_player_only(px, py);
    for (i = 0; i < num_enemies; i++) refresh_enemy_oam(i);
    for (i = (uint8_t)(SP_ENEMY_BASE + num_enemies); i < 40u; i++)
        if (i != SP_BRAZIER_FIRE && i != SP_LADDER_ARROW && i != SP_BELT_SELECTOR && i != SP_PLAYER_AURA_OAM && i != SP_BUFF_ICON
                && i != SP_SCOUNDREL_FOX)
            oam_hide(i);
    if (!brazier_fire_active) oam_hide(SP_BRAZIER_FIRE); // keep slot hidden until first spawn
    refresh_belt_selector_oam();
    refresh_buff_icon_oam();
    refresh_scoundrel_fox_oam();
}

void entity_sprites_refresh_all(uint8_t px, uint8_t py) {
    ladder_cache_valid = map_pit_position(&ladder_cache_mx, &ladder_cache_my);
    entity_sprites_refresh_oam_only(px, py);
}

void entity_sprites_enemy_hit_flash_clear(uint8_t slot) {
    if (slot < MAX_ENEMIES) en_hit_flash_age[slot] = 0u;
}

void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dx, int8_t dy, uint8_t hit_enemy_slot) {
    uint8_t t;
    uint8_t mid_t = (uint8_t)(ENTITY_LUNGE_FRAMES >> 1); // strike read lands halfway through lunge arc
    for (t = 0; t < ENTITY_LUNGE_FRAMES; t++) {
        uint8_t a = lunge_amt_for_frame(t);
        pl_ofs_x = (int8_t)((int16_t)dx * (int16_t)a);
        pl_ofs_y = (int8_t)((int16_t)dy * (int16_t)a);
        entity_sprites_refresh_player_only(px, py);
        if (hit_enemy_slot < MAX_ENEMIES && enemy_alive[hit_enemy_slot] && t == mid_t) {
            en_hit_flash_age[hit_enemy_slot] = 1u;
            refresh_enemy_oam(hit_enemy_slot);
        }
        wait_vbl_done();
    }
    pl_ofs_x = pl_ofs_y = 0;
    entity_sprites_refresh_player_only(px, py);
}

void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py,
                                     const uint8_t *old_ex, const uint8_t *old_ey,
                                     const uint8_t *old_alive) {
    uint8_t i, any = 0, dirty_count = 0;
    uint8_t dirty_slots[MAX_ENEMIES];
    (void)px;
    (void)py;
    for (i = 0; i < num_enemies; i++) {
        if (!enemy_alive[i] || !old_alive[i]) {
            en_ofs_x[i] = 0; en_ofs_y[i] = 0;
            if (enemy_alive[i] != old_alive[i]) dirty_slots[dirty_count++] = i;
            continue;
        }
        en_ofs_x[i] = (int8_t)(((int16_t)old_ex[i] - (int16_t)enemy_x[i]) * 8);
        en_ofs_y[i] = (int8_t)(((int16_t)old_ey[i] - (int16_t)enemy_y[i]) * 8);
        if (old_ex[i] != enemy_x[i] || old_ey[i] != enemy_y[i]) dirty_slots[dirty_count++] = i;
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
        for (i = 0; i < dirty_count; i++) entity_sprites_refresh_enemy(dirty_slots[i]);
        wait_vbl_done();
        if (!any) break;
    }
    memset(en_ofs_x, 0, sizeof en_ofs_x);
    memset(en_ofs_y, 0, sizeof en_ofs_y);
    for (i = 0; i < dirty_count; i++) entity_sprites_refresh_enemy(dirty_slots[i]);
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
        entity_sprites_refresh_player_only(px, py);
        entity_sprites_refresh_enemy(slot);
        wait_vbl_done();
    }
    en_ofs_x[slot] = en_ofs_y[slot] = 0;
    entity_sprites_refresh_player_only(px, py);
    entity_sprites_refresh_enemy(slot);
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
        entity_sprites_refresh_player_only(px, py);
        for (i = 0; i < count; i++) entity_sprites_refresh_enemy(slots[i]);
        wait_vbl_done();
    }
    for (i = 0; i < count; i++) {
        uint8_t s = slots[i];
        en_ofs_x[s] = en_ofs_y[s] = 0;
    }
    entity_sprites_refresh_player_only(px, py);
    for (i = 0; i < count; i++) entity_sprites_refresh_enemy(slots[i]);
}

void entity_sprites_run_projectile(uint8_t sx, uint8_t sy, uint8_t tx, uint8_t ty, uint8_t tile_off, uint8_t pal) {
    uint8_t frame;
    const uint8_t frames = 10u;
    int16_t sxw = (int16_t)sx * 8;
    int16_t syw = (int16_t)sy * 8;
    int16_t txw = (int16_t)tx * 8;
    int16_t tyw = (int16_t)ty * 8;
    projectile_overrides_aura = 1u; // OAM slot 0 draws above SP_PLAYER (lower index = front)
    for (frame = 1u; frame <= frames; frame++) {
        int16_t wx = (int16_t)(sxw + ((int16_t)(txw - sxw) * frame) / frames);
        int16_t wy = (int16_t)(syw + ((int16_t)(tyw - syw) * frame) / frames);
        move_entity_oam(SP_PLAYER_AURA_OAM, wx, wy, (uint8_t)(TILESET_VRAM_OFFSET + tile_off), pal);
        wait_vbl_done();
        wait_vbl_done(); // keep projectile readable; effect-only flight, not tile stepping
    }
    projectile_overrides_aura = 0u;
    entity_sprites_refresh_player_only(g_player_x, g_player_y);
}
