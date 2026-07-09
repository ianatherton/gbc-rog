#pragma bank 17

#include "entity_sprites.h"
#include "render.h"
#include "enemy.h"
#include "globals.h"
#include "lcd.h"
#include "defs.h"
#include "map.h"
#include "biome.h"
#include "ally.h"
#include "debuff_icon.h"
#include "dungeon.h"
#include "equipment.h" // equipped_kind_in_slot — head/weapon lookup for the 2-tile hero + weapon pop-out
#include "items.h"     // items_kind_tile / items_kind_palette — equipped weapon graphic
#include "music.h"     // sfx_lunge_hit — impact sound fires at the swing's contact frame
#include <gb/cgb.h>
#include <string.h>

BANKREF_EXTERN(debuff_icon_next)
BANKREF_EXTERN(map_pit_position)

#define PLAYER_HURT_FLASH_DURATION_VBL 60u // 1 s at ~60 Hz VBlank
#define PLAYER_HURT_FLASH_TOGGLE_VBL    8u // red vs gold half-beat (~7.5 full cycles/s)

#define SP_LADDER_ARROW 36u
#define SP_BRAZIER_FIRE 37u
#define SP_BELT_SELECTOR 35u // fixed screen-space OAM; excluded from post-enemy hide sweep
#define SP_DEBUFF_ICON   3u  // shared root/stun indicator — below SP_ENEMY_BASE (4) so it draws on top of enemies
#define SP_GORGON_HEAD_R SP_INV_CURSOR // boss-floor-only 6th gorgon tile; shares slot 38 with the
                                       // inventory cursor + loading skulls, none of which ever render
                                       // while enemy OAM is being refreshed — see entity_sprites.h
#define BRAZIER_FIRE_TTL_VBL 12u

// Hub-only waypoint fx: a copy of the player's foot aura (same M15/M16 A/B flicker via
// player_aura_tile_vram()) hovers over each waypoint's two bottom tiles, recolored. Borrows
// the bottom of the idle enemy OAM run (floor 0 spawns no enemies; the guarded hide-sweep in
// refresh_oam_only clears the enemy run once on hub entry, this VBL tick repaints every frame).
// Off-hub these slots belong to enemies. PAL: hub OCP4 = blue flag ramp, distinct from the
// player aura's gold OCP7 (swap to PAL_ENEMY_RAT=red / PAL_ENEMY_GOBLIN=green to retint).
#define SP_WAYPOINT_FX_BASE    SP_ENEMY_BASE
#define MAX_WAYPOINT_FX        2u  // OW_FEATURE_PLAN places at most 2 OW_FEAT_WAYPOINT
#define WAYPOINT_FX_PAL        PAL_LADDER

static uint8_t brazier_fire_active;
static uint8_t brazier_fire_ttl;
static int16_t brazier_fire_wx, brazier_fire_wy;
static int8_t brazier_fire_dx;
static uint8_t brazier_fire_source_cursor;
static uint8_t ladder_arrow_phase;
static uint8_t ladder_arrow_tick;
static uint8_t inv_cursor_active;
static uint8_t inv_cursor_sx;
static uint8_t inv_cursor_sy;
static uint8_t inv_cursor_tick;
static uint8_t inv_cursor_phase;
static uint8_t ladder_cache_valid; // 1 when pit coords below are usable (set during refresh, read by VBL)
static uint8_t ladder_cache_mx, ladder_cache_my;
static const int8_t ladder_arrow_bob12[12] = { 0, 1, 2, 2, 1, 0, -1, -2, -2, -1, 0, 0 };

static int16_t player_override_wx = -1; // negative = use px*8
static int16_t player_override_wy = -1;
static int16_t player_override_aura_wx = -1; // glide only — world pos for aura (no walk bob on Y)
static int16_t player_override_aura_wy = -1;
static uint8_t player_flip_x;
static uint8_t player_walk_sub;   // advances each refresh while gliding; drives the k14/k15 walk gait (0 when idle)
static uint8_t player_hurt_flash_ttl;
static uint8_t player_hurt_flash_restore_needed; // 1 after flash until OCP2 restored to gold
static uint8_t player_cache_tx, player_cache_ty; // last refresh tile — VBL hurt blink repaints without full refresh
static uint8_t player_aura_vbl_sub;               // counts VBL within one A/B hold window
static uint8_t player_aura_ab_idx;               // 0 = tile A, 1 = tile B — toggles each PLAYER_AURA_TOGGLE_VBL
static uint8_t projectile_overrides_aura;        // 1: slot 0 is bolt/fireball — skip aura VBL/refresh so FX draws above SP_PLAYER
static uint8_t level_up_smile_ttl;               // >0: aura slot shows TILE_LEVELUP_SMILE_VRAM instead of M15/M16

static int8_t pl_ofs_x, pl_ofs_y;                    // attack lunge (player)
static int8_t en_ofs_x[MAX_ENEMIES], en_ofs_y[MAX_ENEMIES]; // per-slot lunge
static int8_t ally_ofs_x[MAX_ALLIES], ally_ofs_y[MAX_ALLIES]; // walk glide offsets (stepped during camera scroll)
static uint8_t enemy_poof_ttl[MAX_ENEMIES];          // dead slot: VBlanks left showing TILE_POOF_CLOUD (OCP0)
static uint8_t en_hit_flash_age[MAX_ENEMIES];       // 0 off; 1..ENEMY_HIT_FLASH_VBL — grey hit pulses in refresh_enemy_oam
static uint8_t skel_head_slot[MAX_ENEMIES];         // 255 = no head; 0..MAX_BIG_SKELL_HEADS-1 = index into SP_BIG_SKELL_HEAD_BASE pool
static uint8_t enemy_effects_count;                 // enemies with an active poof or hit-flash; 0 = skip VBL loop entirely

static uint8_t g_cam_tx, g_cam_ty, g_cam_tx_end, g_cam_ty_end; // cached tile bounds — set once per refresh pass, used in refresh_enemy_oam/allies/light
static uint8_t oam_enemy_hide_mark;                 // SP_ENEMY_BASE + num_enemies at last hide sweep; 255 = needs initial run

#define ENEMY_POOF_DURATION_VBL 22u // ~370ms @60Hz — overlaps corpse then clears
#define ENEMY_HIT_FLASH_VBL     8u // two 2-VBL pulses OCP0 vs native — ages 1..8 then clear
#define PLAYER_AURA_TOGGLE_VBL  15u // ~0.25s @ ~60Hz — M15 ↔ M16 (was every VBL; too fast to read)
#define LEVEL_UP_SMILE_DURATION_VBL 120u // 2s @~60Hz VBL — L10 smile replaces aura toggle while active

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

static uint8_t belt_slot_icon_col(uint8_t slot) { // map slot 0..7 to its WIN tile column on the belt row
    if (slot < BELT_SLOT_COUNT) return (uint8_t)(2u + slot * 2u);                          // after SPELL label
    return (uint8_t)(12u + (uint8_t)(slot - BELT_SLOT_COUNT) * 2u);                          // after ITEM label
}

static void refresh_belt_selector_oam(void) { // M3 arrow on dungeon row GRID_H-1 (above belt / window band)
    uint8_t s, sx, sy, tt, col;
    if (!lcd_gameplay_active || floor_num == 0u) { // hidden on title and on the hub (no belt shown there)
        oam_hide(SP_BELT_SELECTOR);
        return;
    }
    s = (uint8_t)(selected_belt_slot % BELT_TOTAL_SLOTS);
    tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_LADDER);
    col = belt_slot_icon_col(s);
    sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + 7u + (uint16_t)col * 8u - 8u); // one tile left — over slot icon
    sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint16_t)(GRID_H - 1u) * 8u + 2u);
    set_sprite_tile(SP_BELT_SELECTOR, tt);
    set_sprite_prop(SP_BELT_SELECTOR, (uint8_t)(PAL_XP_UI & 7u));
    move_sprite(SP_BELT_SELECTOR, sx, sy);
}

static uint8_t pick_next_visible_light_source(uint8_t *mx, uint8_t *my) {
    uint8_t i;
    if (brazier_count == 0u) return 0u;
    for (i = 0u; i < brazier_count; i++) {
        uint8_t idx = (uint8_t)((brazier_fire_source_cursor + i) % brazier_count);
        uint8_t x = brazier_x[idx], y = brazier_y[idx];
        if (x < g_cam_tx || x >= g_cam_tx_end) continue;
        if (y < g_cam_ty || y >= g_cam_ty_end) continue;
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
        if ((sp == SP_PLAYER || sp == SP_PLAYER_HEAD) && player_flip_x) prop |= S_FLIPX;
        if (sp == SP_PLAYER_AURA_OAM && player_flip_x && !projectile_overrides_aura) prop |= S_FLIPX;
        if (sp >= SP_ALLY_BASE && sp < (uint8_t)(SP_ALLY_BASE + MAX_ALLIES) && ally_flip_x[sp - SP_ALLY_BASE])
            prop |= S_FLIPX;
        set_sprite_prop(sp, prop);
    }
    move_sprite(sp, sx, sy);
}

static void refresh_allies_oam(void) {
    uint8_t i;
    for (i = 0u; i < MAX_ALLIES; i++) {
        uint8_t fx, fy;
        if (!ally_active[i]) {
            oam_hide((uint8_t)(SP_ALLY_BASE + i));
            continue;
        }
        switch (ally_type[i]) {
        case ALLY_TYPE_FOX:
            if (!lcd_gameplay_active || player_class != 1u) {
                oam_hide((uint8_t)(SP_ALLY_BASE + i));
                continue;
            }
            fx = ally_x[i];
            fy = ally_y[i];
            if (fx < g_cam_tx || fx >= g_cam_tx_end
                    || fy < g_cam_ty || fy >= g_cam_ty_end
                    || !lighting_is_revealed(fx, fy)) {
                oam_hide((uint8_t)(SP_ALLY_BASE + i));
                continue;
            }
            move_entity_oam((uint8_t)(SP_ALLY_BASE + i), (int16_t)fx * 8 + ally_ofs_x[i], (int16_t)fy * 8 + ally_ofs_y[i],
                    TILE_FOX_J9_VRAM, PAL_ENEMY_BAT);
            break;
        default:
            oam_hide((uint8_t)(SP_ALLY_BASE + i));
            break;
        }
    }
}

#define SP_TOWN_FLAG SP_INV_CURSOR // slot 38 — idle on the hub (inv cursor / gorgon head own it elsewhere)

static uint8_t town_flag_pal(void) { // seed-stable pick from the 4 enemy OBJ ramps
    static const uint8_t pals[4] = { PAL_LADDER, PAL_ENEMY_RAT, PAL_ENEMY_GOBLIN, PAL_XP_UI };
    return pals[(uint8_t)(run_seed & 3u)];
}

// Town flag: a 2-frame animated OBJ in the courtyard center of the hub's town. Hub-only — off the hub,
// slot 38 belongs to the inventory cursor / gorgon head, so we never touch it there. Frame swaps on the
// shared enemy_anim_toggle (this runs from draw_enemy_cells on each toggle, so the flag animates for free).
static void refresh_town_flag_oam(void) {
    uint8_t i;
    if (floor_biome != BIOME_OVERWORLD) return;
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_TOWN) continue;
        {
            uint8_t mx = (uint8_t)(ow_features[i].x + 1u); // 3x3 town → courtyard center cell
            uint8_t my = (uint8_t)(ow_features[i].y + 1u);
            if (mx < g_cam_tx || mx >= g_cam_tx_end || my < g_cam_ty || my >= g_cam_ty_end
                    || !lighting_is_revealed(mx, my))
                continue; // off-screen/unrevealed town — check the others (towns are ≥1 screen apart)
            move_entity_oam(SP_TOWN_FLAG, (int16_t)mx * 8, (int16_t)my * 8,
                    enemy_anim_toggle ? TILE_FLAG_F2_VRAM : TILE_FLAG_F1_VRAM, town_flag_pal());
            return;
        }
    }
    oam_hide(SP_TOWN_FLAG); // no town on-screen (only one fits at a time → single flag OAM slot suffices)
}

BANKREF(entity_sprites_poof_clear_all)
void entity_sprites_poof_clear_all(void) BANKED {
    memset(enemy_poof_ttl, 0, sizeof enemy_poof_ttl);
    memset(en_hit_flash_age, 0, sizeof en_hit_flash_age);
    memset(ally_ofs_x, 0, sizeof ally_ofs_x);
    memset(ally_ofs_y, 0, sizeof ally_ofs_y);
    enemy_effects_count = 0u;
}

BANKREF(entity_sprites_enemy_poof_begin)
void entity_sprites_enemy_poof_begin(uint8_t slot) BANKED {
    if (slot < MAX_ENEMIES) {
        if (!enemy_poof_ttl[slot]) enemy_effects_count++;
        enemy_poof_ttl[slot] = ENEMY_POOF_DURATION_VBL;
    }
}

BANKREF(entity_sprites_init)
void entity_sprites_init(void) BANKED {
    uint8_t i;
    entity_sprites_poof_clear_all();
    memset(skel_head_slot, 255, sizeof skel_head_slot);
    memset(en_ofs_x, 0, sizeof en_ofs_x);
    memset(en_ofs_y, 0, sizeof en_ofs_y);
    pl_ofs_x = pl_ofs_y = 0;
    player_override_wx = -1;
    player_override_wy = -1;
    player_override_aura_wx = -1;
    player_override_aura_wy = -1;
    player_flip_x = 0u;
    player_walk_sub = 0u;
    player_hurt_flash_ttl = 0u;
    player_hurt_flash_restore_needed = 0u;
    oam_enemy_hide_mark = 255u;
    brazier_fire_active = 0u;
    brazier_fire_ttl = 0u;
    brazier_fire_source_cursor = 0u;
    ladder_arrow_phase = 0u;
    ladder_arrow_tick = 0u;
    ladder_cache_valid = 0u;
    player_aura_vbl_sub = 0u;
    player_aura_ab_idx = 0u;
    level_up_smile_ttl = 0u;
    for (i = 0; i < 40u; i++) oam_hide(i);
    SHOW_SPRITES;
}

BANKREF(entity_sprites_set_player_facing)
void entity_sprites_set_player_facing(int8_t dir_x) BANKED {
    if (dir_x < 0) player_flip_x = 1u;
    else if (dir_x > 0) player_flip_x = 0u;
}

BANKREF(entity_sprites_level_up_fx_trigger)
void entity_sprites_level_up_fx_trigger(void) BANKED {
    level_up_smile_ttl = LEVEL_UP_SMILE_DURATION_VBL;
}

BANKREF(entity_sprites_player_hurt_flash)
void entity_sprites_player_hurt_flash(void) BANKED {
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

static uint8_t player_aura_oam_tile(void) { // level-up smile overrides gold flicker
    return (level_up_smile_ttl > 0u) ? (uint8_t)TILE_LEVELUP_SMILE_VRAM : player_aura_tile_vram();
}

static int16_t player_aura_oam_y(int16_t ay) { // +3 matches default gold aura; smile −8px = one tile higher
    if (level_up_smile_ttl > 0u) return (int16_t)(ay + 3 - 8);
    return (int16_t)(ay + 3);
}

static void refresh_player_aura_oam_vbl(void) { // position every VBL; M15/M16 swap on PLAYER_AURA_TOGGLE_VBL cadence
    int16_t ax, ay;
    if (!lcd_gameplay_active || projectile_overrides_aura) return;
    player_aura_world_xy(&ax, &ay);
    move_entity_oam(SP_PLAYER_AURA_OAM, ax, player_aura_oam_y(ay), player_aura_oam_tile(), PAL_XP_UI);
}

// Head tile: the worn HEAD-slot item picks the graphic (witch hat / helm), else the bare head.
static uint8_t player_head_tile_vram(void) {
    uint8_t hk = equipped_kind_in_slot(EQUIP_SLOT_HEAD);
    if (hk == ITEM_KIND_WITCH_HAT) return TILE_WITCH_HAT_VRAM;
    if (hk != ITEM_KIND_NONE) return TILE_PLAYER_HELMET_VRAM;
    return TILE_PLAYER_HEAD_VRAM;
}

// Body tile: alternate standing/mid-stride while the hero is gliding (a move step is in flight → override set);
// hold standing when idle. player_walk_sub advances one per refresh so a single tile step reads stand→stride.
static uint8_t player_body_tile_vram(void) {
    if (player_override_wx < 0) { player_walk_sub = 0u; return TILE_PLAYER_BODY_STAND_VRAM; }
    player_walk_sub++;
    return ((player_walk_sub >> 2) & 1u) ? TILE_PLAYER_BODY_STRIDE_VRAM : TILE_PLAYER_BODY_STAND_VRAM;
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
        move_entity_oam(SP_PLAYER_AURA_OAM, ax, player_aura_oam_y(ay), player_aura_oam_tile(), PAL_XP_UI);
    } else if (!lcd_gameplay_active) {
        oam_hide(SP_PLAYER_AURA_OAM);
    }
    move_entity_oam(SP_PLAYER, pwx, pwy, player_body_tile_vram(), player_pal);       // bottom tile
    move_entity_oam(SP_PLAYER_HEAD, pwx, (int16_t)(pwy - 8), player_head_tile_vram(), player_pal); // top tile, flush above the body
}

static void refresh_enemy_oam(uint8_t slot) {
    uint8_t sp = (uint8_t)(SP_ENEMY_BASE + slot);
    uint8_t hi = skel_head_slot[slot]; // 255 = no head assigned, else index into head pool
    uint8_t hsp = (hi < MAX_BIG_SKELL_HEADS) ? (uint8_t)(SP_BIG_SKELL_HEAD_BASE + hi) : 255u;
    if (slot >= num_enemies) {
        oam_hide(sp);
        if (hsp != 255u) oam_hide(hsp);
        return;
    }
    if (!enemy_alive[slot]) {
        if (enemy_type[slot] == ENEMY_GORGON) {
            // hide all extra Gorgon body slots on death/poof so only primary slot shows poof
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 3u));
            oam_hide(SP_GORGON_HEAD_R);
        }
        if (enemy_type[slot] == ENEMY_SLIME_BIG) {
            // hide the 3 borrowed quadrant slots on death/poof so only primary slot shows poof
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u));
        }
        if (enemy_type[slot] == ENEMY_SPHINX) {
            // hide the 9 extra sphinx tiles (OAM 4..12) on death/poof; primary slot 3 shows the poof
            uint8_t k;
            for (k = 1u; k <= 9u; k++) oam_hide((uint8_t)(SP_ENEMY_BASE + k));
        }
        if (enemy_poof_ttl[slot] > 0u) {
            uint8_t mx = enemy_x[slot], my = enemy_y[slot];
            if (mx < g_cam_tx || mx >= g_cam_tx_end
                    || my < g_cam_ty || my >= g_cam_ty_end
                    || !lighting_is_revealed(mx, my)) {
                oam_hide(sp);
                if (hsp != 255u) oam_hide(hsp);
                return;
            }
            move_entity_oam(sp, (int16_t)mx * 8 + en_ofs_x[slot], (int16_t)my * 8 + en_ofs_y[slot],
                    (uint8_t)(TILESET_VRAM_OFFSET + TILE_POOF_CLOUD), 0u); // OCP0 = pal_default (light/white ramp)
            if (hsp != 255u) oam_hide(hsp); // head gone when body poofs
            return;
        }
        oam_hide(sp);
        if (hsp != 255u) oam_hide(hsp);
        return;
    }
    // Custom 2×3 OAM render for ENEMY_GORGON (boss floor only).
    // Borrows SP_BIG_SKELL_HEAD_BASE+0..3 (slots 27-30, safe because no BIG_SKELL spawns
    // on floor 3) and SP_GORGON_HEAD_R (slot 38, shared with the inventory cursor /
    // loading skulls — never visible while enemy OAM is live) for the 5 extra body tiles.
    if (enemy_type[slot] == ENEMY_GORGON) {
        int16_t ewx = (int16_t)enemy_x[slot] * 8 + en_ofs_x[slot];
        int16_t ewy = (int16_t)enemy_y[slot] * 8 + en_ofs_y[slot];
        if (enemy_x[slot] < g_cam_tx || enemy_x[slot] >= g_cam_tx_end
                || enemy_y[slot] < g_cam_ty || enemy_y[slot] >= g_cam_ty_end
                || !lighting_is_revealed(enemy_x[slot], enemy_y[slot])) {
            oam_hide(sp);
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 3u));
            oam_hide(SP_GORGON_HEAD_R);
            return;
        }
        {
            uint8_t pal_head = PAL_ENEMY_SNAKE;
            uint8_t pal_body = PAL_GORGON_BODY;
            uint8_t h = en_hit_flash_age[slot];
            if (h > 0u && h <= ENEMY_HIT_FLASH_VBL) {
                uint8_t age0 = (uint8_t)(h - 1u);
                if (((age0 >> 1) & 1u) == 0u) { pal_head = 0u; pal_body = 0u; }
            }
            // feet row: primary slot = feet-left, slot 27 = feet-right
            move_entity_oam(sp, ewx, ewy,
                (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_FEET_L_OFF), pal_body);
            move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u),
                ewx + 8, ewy,
                (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_FEET_R_OFF), pal_body);
            // body row: slot 28 = body-left, slot 29 = body-right
            if (enemy_y[slot] > 0u) {
                move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u),
                    ewx, (int16_t)(ewy - 8),
                    (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_BODY_L_OFF), pal_body);
                move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u),
                    ewx + 8, (int16_t)(ewy - 8),
                    (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_BODY_R_OFF), pal_body);
            } else {
                oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u));
                oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u));
            }
            // head row: slot 30 = head-left, SP_GORGON_HEAD_R = head-right; flip both on anim_toggle
            if (enemy_y[slot] > 1u) {
                uint8_t head_prop = (uint8_t)(pal_head & 7u);
                uint8_t head_l_tile, head_r_tile;
                if (enemy_anim_toggle) {
                    head_prop  |= S_FLIPX;
                    head_l_tile = (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_HEAD_R_OFF);
                    head_r_tile = (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_HEAD_L_OFF);
                } else {
                    head_l_tile = (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_HEAD_L_OFF);
                    head_r_tile = (uint8_t)(TILESET_VRAM_OFFSET + TILE_GORGON_HEAD_R_OFF);
                }
                move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 3u),
                    ewx, (int16_t)(ewy - 16), head_l_tile, pal_head);
                set_sprite_prop((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 3u), head_prop);
                move_entity_oam(SP_GORGON_HEAD_R,
                    ewx + 8, (int16_t)(ewy - 16), head_r_tile, pal_head);
                set_sprite_prop(SP_GORGON_HEAD_R, head_prop);
            } else {
                oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 3u));
                oam_hide(SP_GORGON_HEAD_R);
            }
        }
        return;
    }
    // Custom 10-tile OAM render for ENEMY_SPHINX (BIOME_BOSS2 only). The boss is the floor's only
    // enemy (num_enemies==1), so the idle enemy-run slots are free: primary slot 3 + the contiguous
    // 4..12 (the hide sweep's lower bound is raised to 13 on this floor). 3×2 body in slots 7..12,
    // 2×2 wings in the first 4 enemy-run slots (lower OAM index = drawn on top). Frame animation is a pure VRAM pixel
    // swap (sphinx_anim_tick re-uploads the body/wing tiles), so the OAM layout here is fixed.
    if (enemy_type[slot] == ENEMY_SPHINX) {
        int16_t ewx = (int16_t)enemy_x[slot] * 8 + en_ofs_x[slot];
        int16_t ewy = (int16_t)enemy_y[slot] * 8 + en_ofs_y[slot];
        if (enemy_x[slot] < g_cam_tx || enemy_x[slot] >= g_cam_tx_end
                || enemy_y[slot] < g_cam_ty || enemy_y[slot] >= g_cam_ty_end
                || !lighting_is_revealed(enemy_x[slot], enemy_y[slot])) {
            uint8_t k;
            for (k = 0u; k <= 9u; k++) oam_hide((uint8_t)(SP_ENEMY_BASE + k)); // primary + 9 extra
            return;
        }
        {
            uint8_t pal = PAL_SPHINX_BODY;
            uint8_t h = en_hit_flash_age[slot];
            if (h > 0u && h <= ENEMY_HIT_FLASH_VBL) {
                uint8_t age0 = (uint8_t)(h - 1u);
                if (((age0 >> 1) & 1u) == 0u) pal = 0u; // OCP0 grey hit pulse
            }
            // body 3×2: top row at ewy-8, legs row (footprint) at ewy; cols ewx, +8, +16
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 4u), ewx,                (int16_t)(ewy - 8), TILE_SPHINX_B0_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 5u), (int16_t)(ewx + 8), (int16_t)(ewy - 8), TILE_SPHINX_B1_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 6u), (int16_t)(ewx + 16),(int16_t)(ewy - 8), TILE_SPHINX_B2_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 7u), ewx,                ewy,                TILE_SPHINX_B3_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 8u), (int16_t)(ewx + 8), ewy,                TILE_SPHINX_B4_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 9u), (int16_t)(ewx + 16),ewy,                TILE_SPHINX_B5_VRAM, pal);
            // wings 2×2 attached as a child of body tile B1 (top-middle, first enemy-run slots = on top):
            // the wing's bottom-left corner is pinned to B1's bottom-left corner (ewx+8, ewy) —
            // the "b1/b3" corner — so the wing extends up & right over the upper-center/right body.
            int16_t wing_bl_x = (int16_t)(ewx + 8); // B1 bottom-left corner X
            int16_t wing_bl_y = ewy;                // B1 bottom-left corner Y
            move_entity_oam(sp,                            wing_bl_x,                (int16_t)(wing_bl_y - 16), TILE_SPHINX_W0_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 1u), (int16_t)(wing_bl_x + 8), (int16_t)(wing_bl_y - 16), TILE_SPHINX_W1_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 2u), wing_bl_x,                (int16_t)(wing_bl_y - 8),  TILE_SPHINX_W2_VRAM, pal);
            move_entity_oam((uint8_t)(SP_ENEMY_BASE + 3u), (int16_t)(wing_bl_x + 8), (int16_t)(wing_bl_y - 8),  TILE_SPHINX_W3_VRAM, pal);
        }
        return;
    }
    // Custom 2×2 OAM render for ENEMY_SLIME_BIG — the miniboss-floor elite: 4 quadrants of
    // a nearest-neighbor 2x upscale of its base fodder sprite (elite_base_type), built at
    // floor gen by dungeon_elite_load_art (bank 28). Centered on the enemy's primary logical
    // tile (occupies 2 tiles for collision/attack — see ENEMY_GORGON-style checks in
    // enemy.c/combat.c/map.c). Frame 1 lives in the dead cells 194-198; frame 2 borrows the
    // Gorgon slots 225-229 (no Gorgon on miniboss floors; restored on every other floor).
    // Borrows SP_BIG_SKELL_HEAD_BASE+0..2 (slots 27-29) — spawn_enemies rerolls BIG_SKELL
    // fodder on miniboss floors so those OAM slots stay free.
    if (enemy_type[slot] == ENEMY_SLIME_BIG) {
        int16_t ewx = (int16_t)enemy_x[slot] * 8 + en_ofs_x[slot];
        int16_t ewy = (int16_t)enemy_y[slot] * 8 + en_ofs_y[slot];
        if (enemy_x[slot] < g_cam_tx || enemy_x[slot] >= g_cam_tx_end
                || enemy_y[slot] < g_cam_ty || enemy_y[slot] >= g_cam_ty_end
                || !lighting_is_revealed(enemy_x[slot], enemy_y[slot])) {
            oam_hide(sp);
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u));
            oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u));
            return;
        }
        {
            uint8_t pal = enemy_defs[ENEMY_SLIME_BIG].palette; // inherited from the elite's base type
            uint8_t h = en_hit_flash_age[slot];
            uint8_t tl, tr, bl, br;
            if (h > 0u && h <= ENEMY_HIT_FLASH_VBL) {
                uint8_t age0 = (uint8_t)(h - 1u);
                if (((age0 >> 1) & 1u) == 0u) pal = 0u; // OCP0 grey ramp vs native enemy ramp
            }
            // centered on the logical tile: ±4px from tile origin per quadrant
            tl = enemy_anim_toggle ? TILE_GORGON_HEAD_L_OFF : TILE_SLIMEBIG_TL_OFF;
            tr = enemy_anim_toggle ? TILE_GORGON_HEAD_R_OFF : TILE_SLIMEBIG_TR_OFF;
            bl = enemy_anim_toggle ? TILE_GORGON_BODY_L_OFF : TILE_SLIMEBIG_BL_OFF;
            br = enemy_anim_toggle ? TILE_GORGON_BODY_R_OFF : TILE_SLIMEBIG_BR_OFF;
            move_entity_oam(sp, ewx - 4, ewy - 4,
                (uint8_t)(TILESET_VRAM_OFFSET + tl), pal);
            move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 0u), ewx + 4, ewy - 4,
                (uint8_t)(TILESET_VRAM_OFFSET + tr), pal);
            move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 1u), ewx - 4, ewy + 4,
                (uint8_t)(TILESET_VRAM_OFFSET + bl), pal);
            move_entity_oam((uint8_t)(SP_BIG_SKELL_HEAD_BASE + 2u), ewx + 4, ewy + 4,
                (uint8_t)(TILESET_VRAM_OFFSET + br), pal);
        }
        return;
    }
    {
        const EnemyDef *def = &enemy_defs[enemy_type[slot]];
        uint8_t off = enemy_anim_toggle ? def->tile_alt : def->tile;
        uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + off);
        int16_t ewx = (int16_t)enemy_x[slot] * 8 + en_ofs_x[slot];
        int16_t ewy = (int16_t)enemy_y[slot] * 8 + en_ofs_y[slot];
        if (enemy_x[slot] < g_cam_tx || enemy_x[slot] >= g_cam_tx_end
                || enemy_y[slot] < g_cam_ty || enemy_y[slot] >= g_cam_ty_end
                || !lighting_is_revealed(enemy_x[slot], enemy_y[slot])) {
            oam_hide(sp);
            if (hsp != 255u) oam_hide(hsp);
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
            if (def->tile == def->tile_alt && enemy_anim_toggle)
                set_sprite_prop(sp, (uint8_t)((pal & 7u) | S_FLIPX));
            // skeleton head — one tile above body (J7); hidden if skeleton is at world row 0
            if (hsp != 255u) {
                if (enemy_y[slot] > 0u) {
                    move_entity_oam(hsp, ewx, (int16_t)(ewy - 8),
                                    (uint8_t)(TILESET_VRAM_OFFSET + TILE_BIG_SKELL_HEAD), pal);
                    if (enemy_anim_toggle)
                        set_sprite_prop(hsp, (uint8_t)((pal & 7u) | S_FLIPX));
                } else {
                    oam_hide(hsp);
                }
            }
        }
    }
}

BANKREF(entity_sprites_vbl_tick)
void entity_sprites_vbl_tick(void) BANKED {
    if (brazier_fire_active && brazier_fire_ttl > 0u) {
        brazier_fire_wy--;
        if ((brazier_fire_ttl & 1u) == 0u) brazier_fire_wx += brazier_fire_dx;
        brazier_fire_ttl--;
        move_entity_oam(SP_BRAZIER_FIRE, brazier_fire_wx, brazier_fire_wy,
                (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE), PAL_WALL_BG);
    } else {
        brazier_fire_try_spawn();
    }
    if (lcd_gameplay_active && ladder_cache_valid
            && !boss_alive) {
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
    if (inv_cursor_active) {
        uint8_t bob;
        if (++inv_cursor_tick >= 2u) {
            inv_cursor_tick = 0u;
            inv_cursor_phase = (uint8_t)((inv_cursor_phase + 1u) % 12u);
        }
        bob = inv_cursor_phase;
        move_sprite(SP_INV_CURSOR,
                    (uint8_t)((int16_t)inv_cursor_sx + (int16_t)ladder_arrow_bob12[bob]),
                    inv_cursor_sy);
    }
    if (player_hurt_flash_ttl > 0u) {
        refresh_player_oam_from_cache(); // palette + OAM before ttl tick so all 60 frames flash
        player_hurt_flash_ttl--;
    }
    if (lcd_gameplay_active) {
        uint8_t pi;
        if (level_up_smile_ttl > 0u) level_up_smile_ttl--;
        if (++player_aura_vbl_sub >= PLAYER_AURA_TOGGLE_VBL) {
            player_aura_vbl_sub = 0u;
            player_aura_ab_idx ^= 1u;
        }
        refresh_player_aura_oam_vbl();
        if (enemy_effects_count) {
            g_cam_tx     = (uint8_t)(camera_px >> 3);
            g_cam_ty     = (uint8_t)(camera_py >> 3);
            g_cam_tx_end = (uint8_t)(g_cam_tx + GRID_W);
            g_cam_ty_end = (uint8_t)(g_cam_ty + GRID_H);
            for (pi = 0u; pi < num_enemies; pi++) {
                if (enemy_poof_ttl[pi] > 0u) {
                    enemy_poof_ttl[pi]--;
                    if (!enemy_poof_ttl[pi] && enemy_effects_count > 0u) enemy_effects_count--;
                    refresh_enemy_oam(pi);
                } else if (en_hit_flash_age[pi] > 0u && enemy_alive[pi]) {
                    en_hit_flash_age[pi]++;
                    if (en_hit_flash_age[pi] > ENEMY_HIT_FLASH_VBL) {
                        en_hit_flash_age[pi] = 0u;
                        if (enemy_effects_count > 0u) enemy_effects_count--;
                    }
                    refresh_enemy_oam(pi);
                }
            }
        }
        // Debuff icon (root/stun) — delegate cycle/search to banked debuff_icon_next to save HOME
        // space; shares one OAM slot since all 40 are already spoken for.
        {
            uint8_t rex, rey, rtile;
            if (debuff_icon_next(&rex, &rey, &rtile)) {
                // Mace stun glyph rides the torch/brazier fire ramp: OCP PAL_WALL_BG is the gameplay
                // fire-particle tint (pal_ladder = brazier fire tone = PAL_XP_UI_BG xp-gold). Root keeps OCP7 gold.
                uint8_t pal = (rtile == TILE_STUN_ICON_VRAM) ? PAL_WALL_BG : PAL_XP_UI;
                move_entity_oam(SP_DEBUFF_ICON, (int16_t)rex * 8, (int16_t)rey * 8, rtile, pal);
            } else {
                oam_hide(SP_DEBUFF_ICON);
            }
        }
        // Hub waypoint fx: a recolored copy of the player's foot aura centered on the seam of
        // each waypoint's two bottom tiles. Floor-0 only — off-hub SP_WAYPOINT_FX_BASE.. are live
        // enemy slots, leave them. The A/B flicker rides player_aura_ab_idx (updated just above),
        // so it animates in sync with the hero's aura.
        if (floor_num == 0u) {
            uint8_t fi, p = 0u;
            uint8_t tile = player_aura_tile_vram();
            for (fi = 0u; fi < ow_feature_count && p < MAX_WAYPOINT_FX; fi++) {
                uint8_t fx, fy;
                if (ow_features[fi].type != OW_FEAT_WAYPOINT) continue;
                fx = ow_features[fi].x;
                fy = (uint8_t)(ow_features[fi].y + 1u); // bottom row
                if (fx >= CAM_TX && fx < (uint8_t)(CAM_TX + GRID_W)
                        && fy >= CAM_TY && fy < (uint8_t)(CAM_TY + GRID_H)) {
                    move_entity_oam((uint8_t)(SP_WAYPOINT_FX_BASE + p),
                                    (int16_t)fx * 8 + 4, (int16_t)fy * 8 - 3,
                                    tile, WAYPOINT_FX_PAL);
                } else {
                    oam_hide((uint8_t)(SP_WAYPOINT_FX_BASE + p));
                }
                p++;
            }
            for (; p < MAX_WAYPOINT_FX; p++) oam_hide((uint8_t)(SP_WAYPOINT_FX_BASE + p));
        }
    }
}

BANKREF(entity_sprites_set_player_world)
void entity_sprites_set_player_world(int16_t spr_wx, int16_t spr_wy, int16_t aura_wx, int16_t aura_wy) BANKED {
    player_override_wx = spr_wx;
    player_override_wy = spr_wy;
    player_override_aura_wx = aura_wx;
    player_override_aura_wy = aura_wy;
}

BANKREF(entity_sprites_clear_player_world)
void entity_sprites_clear_player_world(void) BANKED {
    player_override_wx = -1;
    player_override_wy = -1;
    player_override_aura_wx = -1;
    player_override_aura_wy = -1;
}

BANKREF(entity_sprites_refresh_player_only)
void entity_sprites_refresh_player_only(uint8_t px, uint8_t py) BANKED {
    player_cache_tx = px;
    player_cache_ty = py;
    refresh_player_oam_from_cache();
}

BANKREF(entity_sprites_refresh_enemy)
void entity_sprites_refresh_enemy(uint8_t slot) BANKED {
    g_cam_tx     = (uint8_t)(camera_px >> 3);
    g_cam_ty     = (uint8_t)(camera_py >> 3);
    g_cam_tx_end = (uint8_t)(g_cam_tx + GRID_W);
    g_cam_ty_end = (uint8_t)(g_cam_ty + GRID_H);
    refresh_enemy_oam(slot);
}

BANKREF(entity_sprites_refresh_oam_only)
void entity_sprites_refresh_oam_only(uint8_t px, uint8_t py) BANKED {
    uint8_t i;
    // Cache camera tile bounds once — used by refresh_enemy_oam and refresh_allies_oam
    g_cam_tx     = (uint8_t)(camera_px >> 3);
    g_cam_ty     = (uint8_t)(camera_py >> 3);
    g_cam_tx_end = (uint8_t)(g_cam_tx + GRID_W);
    g_cam_ty_end = (uint8_t)(g_cam_ty + GRID_H);
    entity_sprites_refresh_player_only(px, py);
    refresh_allies_oam();
    refresh_belt_selector_oam();
    refresh_buff_icon_oam();
    refresh_town_flag_oam();
    // Clear all head OAM slots so stale heads from dead skeletons don't linger
    {
        uint8_t hi;
        for (hi = 0; hi < MAX_BIG_SKELL_HEADS; hi++) oam_hide((uint8_t)(SP_BIG_SKELL_HEAD_BASE + hi));
    }
    // Single pass: assign head slots to alive big skells AND refresh every enemy OAM slot
    {
        uint8_t next_head = 0;
        memset(skel_head_slot, 255, sizeof skel_head_slot);
        for (i = 0; i < num_enemies; i++) {
            if (enemy_alive[i] && enemy_type[i] == ENEMY_BIG_SKELL && next_head < MAX_BIG_SKELL_HEADS)
                skel_head_slot[i] = next_head++;
            refresh_enemy_oam(i);
        }
    }
    // Hide unused enemy body slots — guarded so this only runs when num_enemies changes (once per floor)
    {
        uint8_t new_mark = (uint8_t)(SP_ENEMY_BASE + num_enemies);
        // The Sphinx boss draws into the first 10 idle enemy-run slots; keep the sweep above them.
        if (floor_kind == FLOORKIND_BOSS && floor_boss_type == ENEMY_SPHINX && new_mark < (uint8_t)(SP_ENEMY_BASE + 10u))
            new_mark = (uint8_t)(SP_ENEMY_BASE + 10u);
        if (oam_enemy_hide_mark != new_mark) {
            // Sweep only the enemy run; slots past it (skell heads etc.) have their own owners.
            for (i = new_mark; i < (uint8_t)(SP_ENEMY_BASE + MAX_ENEMIES); i++) oam_hide(i);
            oam_enemy_hide_mark = new_mark;
        }
    }
    if (!brazier_fire_active) oam_hide(SP_BRAZIER_FIRE); // keep slot hidden until first spawn
}

BANKREF(entity_sprites_refresh_all)
void entity_sprites_refresh_all(uint8_t px, uint8_t py) BANKED {
    ladder_cache_valid = map_pit_position(&ladder_cache_mx, &ladder_cache_my);
    entity_sprites_refresh_oam_only(px, py);
}

BANKREF(entity_sprites_enemy_hit_flash_clear)
void entity_sprites_enemy_hit_flash_clear(uint8_t slot) BANKED {
    if (slot < MAX_ENEMIES && en_hit_flash_age[slot] > 0u) {
        en_hit_flash_age[slot] = 0u;
        if (enemy_effects_count > 0u) enemy_effects_count--;
    }
}

// OBJ and BKG palettes are separate CRAM — items_kind_palette() picks colors tuned for the BKG
// inventory/pickup icons, which land on the wrong OBJ ramp when reused for a sprite (e.g. a BKG-gold
// item would show skeleton-violet). Default every item/weapon pop-out to the neutral grey ramp;
// only the health potions (Potion, BigHeal/Key) get the red-rose ramp, matching their red BKG
// life-UI color on both sides.
static uint8_t item_popout_obj_palette(uint8_t kind) {
    return (kind == ITEM_KIND_POTION || kind == ITEM_KIND_KEY) ? PAL_LIFE_UI : PAL_CORPSE;
}

BANKREF(entity_sprites_run_player_lunge)
#define WEAPON_LUNGE_SIDE_PX 6 // held weapon pops out this many px to the facing side of the hero
#define WEAPON_SWING_FRAMES 20u // ~333ms at 60Hz — weapon flies to the target and back

void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dx, int8_t dy, uint8_t hit_enemy_slot) BANKED {
    uint8_t t;
    uint8_t swing_last = (uint8_t)(WEAPON_SWING_FRAMES - 1u);
    uint8_t mid_t = (uint8_t)(swing_last >> 1); // strike read lands halfway through the swing
    // Equipped weapon "pops out" beside the hero and tweens out to the attacked tile and back,
    // borrowing the fx/aura slot (0). Bow keeps its own shoot/arrow animation, so it draws nothing here.
    uint8_t wk = equipped_kind_in_slot(EQUIP_SLOT_WEAPON);
    uint8_t show_weapon = (uint8_t)(wk != ITEM_KIND_NONE && wk != ITEM_KIND_BOW);
    uint8_t wtile = 0u, wpal = 0u;
    int16_t wsx = 0, wsy = 0, wtx = 0, wty = 0; // weapon tween endpoints: draw position <-> attacked tile
    if (show_weapon) {
        wtile = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(wk));
        wpal  = item_popout_obj_palette(wk);
        projectile_overrides_aura = 1u; // suppress the foot aura so slot 0 is free for the weapon
        wsx = (int16_t)((int16_t)px * 8 + (player_flip_x ? -WEAPON_LUNGE_SIDE_PX : WEAPON_LUNGE_SIDE_PX));
        wsy = (int16_t)((int16_t)py * 8);
        wtx = (int16_t)((int16_t)(px + dx) * 8); // melee only triggers on the adjacent tile
        wty = (int16_t)((int16_t)(py + dy) * 8);
    }
    for (t = 0; t < WEAPON_SWING_FRAMES; t++) {
        uint8_t p = (t > mid_t) ? (uint8_t)(swing_last - t) : t; // 0..mid..0 triangle ramp
        uint8_t a = (uint8_t)((4u * p) / mid_t);                 // body arc peaks with weapon impact
        pl_ofs_x = (int8_t)((int16_t)dx * (int16_t)a);
        pl_ofs_y = (int8_t)((int16_t)dy * (int16_t)a);
        entity_sprites_refresh_player_only(px, py);
        if (show_weapon) { // lerp between the hero's side and the attacked tile
            int16_t wwx = (int16_t)(wsx + (int16_t)((wtx - wsx) * (int16_t)p) / (int16_t)mid_t);
            int16_t wwy = (int16_t)(wsy + (int16_t)((wty - wsy) * (int16_t)p) / (int16_t)mid_t);
            move_entity_oam(SP_PLAYER_AURA_OAM, wwx, wwy, wtile, wpal);
            if (player_flip_x) set_sprite_prop(SP_PLAYER_AURA_OAM, (uint8_t)((wpal & 7u) | S_FLIPX));
        }
        if (hit_enemy_slot < MAX_ENEMIES && enemy_alive[hit_enemy_slot] && t == mid_t) {
            if (!en_hit_flash_age[hit_enemy_slot]) enemy_effects_count++;
            en_hit_flash_age[hit_enemy_slot] = 1u;
            refresh_enemy_oam(hit_enemy_slot);
            sfx_lunge_hit(); // sound on contact, not after the swing settles
        }
        wait_vbl_done();
    }
    pl_ofs_x = pl_ofs_y = 0;
    if (show_weapon) projectile_overrides_aura = 0u; // release slot 0; the refresh below repaints the aura over the weapon
    entity_sprites_refresh_player_only(px, py);
}

#define ITEM_POPOUT_FRAMES 30u // 500ms at 60Hz — item icon holds beside the hero

BANKREF(entity_sprites_run_item_popout)
void entity_sprites_run_item_popout(uint8_t kind) BANKED {
    uint8_t t;
    uint8_t tile = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(kind));
    uint8_t pal  = item_popout_obj_palette(kind);
    int16_t wx = (int16_t)((int16_t)g_player_x * 8 + (player_flip_x ? -WEAPON_LUNGE_SIDE_PX : WEAPON_LUNGE_SIDE_PX));
    int16_t wy = (int16_t)((int16_t)g_player_y * 8);
    projectile_overrides_aura = 1u; // suppress the foot aura so slot 0 is free for the item icon
    for (t = 0; t < ITEM_POPOUT_FRAMES; t++) {
        move_entity_oam(SP_PLAYER_AURA_OAM, wx, wy, tile, pal);
        if (player_flip_x) set_sprite_prop(SP_PLAYER_AURA_OAM, (uint8_t)((pal & 7u) | S_FLIPX));
        wait_vbl_done();
    }
    projectile_overrides_aura = 0u; // release slot 0; refresh repaints the aura over the item
    entity_sprites_refresh_player_only(g_player_x, g_player_y);
}

BANKREF(entity_sprites_enemy_glide_begin)
void entity_sprites_enemy_glide_begin(const uint8_t *old_ex, const uint8_t *old_ey,
                                       const uint8_t *old_alive) BANKED {
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (!enemy_alive[i] || !old_alive[i]) {
            en_ofs_x[i] = 0; en_ofs_y[i] = 0;
        } else {
            en_ofs_x[i] = (int8_t)(((int16_t)old_ex[i] - (int16_t)enemy_x[i]) * 8);
            en_ofs_y[i] = (int8_t)(((int16_t)old_ey[i] - (int16_t)enemy_y[i]) * 8);
        }
    }
}

BANKREF(entity_sprites_enemy_glide_step)
void entity_sprites_enemy_glide_step(void) BANKED {
    uint8_t i;
    for (i = 0; i < num_enemies; i++) {
        if (en_ofs_x[i] > 0) en_ofs_x[i] = (en_ofs_x[i] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[i] - ENEMY_GLIDE_SPEED) : 0;
        else if (en_ofs_x[i] < 0) en_ofs_x[i] = (en_ofs_x[i] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[i] + ENEMY_GLIDE_SPEED) : 0;
        if (en_ofs_y[i] > 0) en_ofs_y[i] = (en_ofs_y[i] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[i] - ENEMY_GLIDE_SPEED) : 0;
        else if (en_ofs_y[i] < 0) en_ofs_y[i] = (en_ofs_y[i] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[i] + ENEMY_GLIDE_SPEED) : 0;
    }
    for (i = 0; i < MAX_ALLIES; i++) {
        if (ally_ofs_x[i] > 0) ally_ofs_x[i] = (ally_ofs_x[i] > (int8_t)ALLY_GLIDE_SPEED) ? (int8_t)(ally_ofs_x[i] - ALLY_GLIDE_SPEED) : 0;
        else if (ally_ofs_x[i] < 0) ally_ofs_x[i] = (ally_ofs_x[i] < -(int8_t)ALLY_GLIDE_SPEED) ? (int8_t)(ally_ofs_x[i] + ALLY_GLIDE_SPEED) : 0;
        if (ally_ofs_y[i] > 0) ally_ofs_y[i] = (ally_ofs_y[i] > (int8_t)ALLY_GLIDE_SPEED) ? (int8_t)(ally_ofs_y[i] - ALLY_GLIDE_SPEED) : 0;
        else if (ally_ofs_y[i] < 0) ally_ofs_y[i] = (ally_ofs_y[i] < -(int8_t)ALLY_GLIDE_SPEED) ? (int8_t)(ally_ofs_y[i] + ALLY_GLIDE_SPEED) : 0;
    }
}

BANKREF(entity_sprites_ally_glide_begin)
void entity_sprites_ally_glide_begin(const uint8_t *old_ax, const uint8_t *old_ay,
                                      const uint8_t *old_aa) BANKED {
    uint8_t i;
    for (i = 0; i < MAX_ALLIES; i++) {
        if (!ally_active[i] || !old_aa[i]) {
            ally_ofs_x[i] = 0; ally_ofs_y[i] = 0;
        } else {
            int16_t dx = ((int16_t)old_ax[i] - (int16_t)ally_x[i]) * 8;
            int16_t dy = ((int16_t)old_ay[i] - (int16_t)ally_y[i]) * 8;
            if (dx < -120 || dx > 120 || dy < -120 || dy > 120) {
                ally_ofs_x[i] = 0; ally_ofs_y[i] = 0; // teleport — appear instantly
            } else {
                // Cap to ±8px (one tile) so glide always converges within the 8-frame scroll window
                ally_ofs_x[i] = (int8_t)(dx < -8 ? -8 : dx > 8 ? 8 : dx);
                ally_ofs_y[i] = (int8_t)(dy < -8 ? -8 : dy > 8 ? 8 : dy);
            }
        }
    }
}

BANKREF(entity_sprites_run_enemy_glide)
void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py,
                                     const uint8_t *old_ex, const uint8_t *old_ey,
                                     const uint8_t *old_alive) BANKED {
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
        for (i = 0; i < dirty_count; i++) {
            uint8_t s = dirty_slots[i];
            if (en_ofs_x[s] > 0) en_ofs_x[s] = (en_ofs_x[s] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[s] - ENEMY_GLIDE_SPEED) : 0;
            else if (en_ofs_x[s] < 0) en_ofs_x[s] = (en_ofs_x[s] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[s] + ENEMY_GLIDE_SPEED) : 0;
            if (en_ofs_y[s] > 0) en_ofs_y[s] = (en_ofs_y[s] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[s] - ENEMY_GLIDE_SPEED) : 0;
            else if (en_ofs_y[s] < 0) en_ofs_y[s] = (en_ofs_y[s] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[s] + ENEMY_GLIDE_SPEED) : 0;
            if (en_ofs_x[s] || en_ofs_y[s]) any = 1;
        }
        for (i = 0; i < dirty_count; i++) refresh_enemy_oam(dirty_slots[i]);
        wait_vbl_done();
        if (!any) break;
    }
}

BANKREF(entity_sprites_run_enemy_glide_finish)
void entity_sprites_run_enemy_glide_finish(const uint8_t *old_alive) BANKED {
    // Drains any glide offsets left after enemy_glide_begin + camera scroll have already stepped them.
    // Also handles enemies that died during the AI turn (offset=0, alive state changed).
    uint8_t i, any = 0, dirty_count = 0;
    uint8_t dirty_slots[MAX_ENEMIES];
    for (i = 0; i < num_enemies; i++) {
        if (en_ofs_x[i] || en_ofs_y[i]) {
            dirty_slots[dirty_count++] = i;
            any = 1;
        } else if (enemy_alive[i] != old_alive[i]) {
            dirty_slots[dirty_count++] = i; // just died — refresh once to show poof/corpse
        }
    }
    if (!any && dirty_count == 0) return;
    while (1) {
        any = 0;
        for (i = 0; i < dirty_count; i++) {
            uint8_t s = dirty_slots[i];
            if (en_ofs_x[s] > 0) en_ofs_x[s] = (en_ofs_x[s] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[s] - ENEMY_GLIDE_SPEED) : 0;
            else if (en_ofs_x[s] < 0) en_ofs_x[s] = (en_ofs_x[s] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_x[s] + ENEMY_GLIDE_SPEED) : 0;
            if (en_ofs_y[s] > 0) en_ofs_y[s] = (en_ofs_y[s] > (int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[s] - ENEMY_GLIDE_SPEED) : 0;
            else if (en_ofs_y[s] < 0) en_ofs_y[s] = (en_ofs_y[s] < -(int8_t)ENEMY_GLIDE_SPEED) ? (int8_t)(en_ofs_y[s] + ENEMY_GLIDE_SPEED) : 0;
            if (en_ofs_x[s] || en_ofs_y[s]) any = 1;
        }
        for (i = 0; i < dirty_count; i++) refresh_enemy_oam(dirty_slots[i]);
        wait_vbl_done();
        if (!any) break;
    }
}

BANKREF(entity_sprites_run_enemy_lunge)
void entity_sprites_run_enemy_lunge(uint8_t px, uint8_t py, uint8_t slot, uint8_t tgx, uint8_t tgy) BANKED {
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

BANKREF(entity_sprites_run_enemy_lunges_batch)
void entity_sprites_run_enemy_lunges_batch(uint8_t px, uint8_t py,
                                            const uint8_t *slots, uint8_t count) BANKED { // all attackers lunge toward player simultaneously
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

BANKREF(entity_sprites_run_ally_lunge)
void entity_sprites_run_ally_lunge(uint8_t px, uint8_t py, uint8_t ally_slot, uint8_t tgx, uint8_t tgy, uint8_t hit_enemy_slot) BANKED {
    uint8_t t;
    uint8_t mid_t = (uint8_t)(ENTITY_LUNGE_FRAMES >> 1);
    (void)px; (void)py; // kept for API compatibility; camera hasn't scrolled yet so we render from cache
    int8_t dx = (int8_t)tgx - (int8_t)ally_x[ally_slot];
    int8_t dy = (int8_t)tgy - (int8_t)ally_y[ally_slot];
    if (dx > 1) dx = 1; if (dx < -1) dx = -1;
    if (dy > 1) dy = 1; if (dy < -1) dy = -1;
    for (t = 0; t < ENTITY_LUNGE_FRAMES; t++) {
        uint8_t a = lunge_amt_for_frame(t);
        ally_ofs_x[ally_slot] = (int8_t)((int16_t)dx * (int16_t)a);
        ally_ofs_y[ally_slot] = (int8_t)((int16_t)dy * (int16_t)a);
        refresh_player_oam_from_cache(); // don't update cache — camera hasn't scrolled yet when player moved this turn
        refresh_allies_oam();
        if (hit_enemy_slot < MAX_ENEMIES && enemy_alive[hit_enemy_slot] && t == mid_t) {
            if (!en_hit_flash_age[hit_enemy_slot]) enemy_effects_count++;
            en_hit_flash_age[hit_enemy_slot] = 1u;
            refresh_enemy_oam(hit_enemy_slot);
        }
        wait_vbl_done();
    }
    ally_ofs_x[ally_slot] = ally_ofs_y[ally_slot] = 0;
    refresh_player_oam_from_cache();
    refresh_allies_oam();
}

BANKREF(entity_sprites_inv_cursor_show)
void entity_sprites_inv_cursor_show(uint8_t cx, uint8_t cy) BANKED {
    inv_cursor_sx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (uint16_t)cx * 8u);
    inv_cursor_sy = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint16_t)(cy + 1u) * 8u);
    inv_cursor_tick = 0u;
    inv_cursor_phase = 0u;
    inv_cursor_active = 1u;
    set_sprite_tile(SP_INV_CURSOR, (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SW));
    set_sprite_prop(SP_INV_CURSOR, S_FLIPY);
}

BANKREF(entity_sprites_inv_cursor_hide)
void entity_sprites_inv_cursor_hide(void) BANKED {
    inv_cursor_active = 0u;
    oam_hide(SP_INV_CURSOR);
}

BANKREF(entity_sprites_equip_marks_hide)
void entity_sprites_equip_marks_hide(void) BANKED {
    uint8_t i;
    for (i = SP_ENEMY_BASE; i < (uint8_t)(SP_ENEMY_BASE + 8u); i++) oam_hide(i);
    oam_enemy_hide_mark = 255u;
}

BANKREF(entity_sprites_run_projectile)
void entity_sprites_run_projectile(uint8_t sx, uint8_t sy, uint8_t tx, uint8_t ty, uint8_t tile_off, uint8_t pal) BANKED {
    uint8_t frame;
    const uint8_t frames = 8u; // was 10 × 2-wait = 20 VBL; now 8 × 1-wait = 8 VBL (~133ms)
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
    }
    projectile_overrides_aura = 0u;
    entity_sprites_refresh_player_only(g_player_x, g_player_y);
}
