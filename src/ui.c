#pragma bank 5
#include <gbdk/platform.h>

#include "ui.h"

BANKREF(ui)
#include "debug_bank.h"
#include "globals.h"
#include "defs.h"
#include "lcd.h"         // lcd_gameplay_active for title vs play raster
#include "music.h"       // mute BGM + footfalls during floor generation
#include "render.h"      // load_palettes — restore sprite CRAM after title fire uses OCP7
#include "perf.h"
#include "title_logo.h"  // title_logo_* — tileset ROM read in HOME (not bank 5) to avoid MBC mismatch crashes

BANKREF_EXTERN(load_palettes)
#include "seed_entropy.h" // deterministic-ish random seed from hardware jitter
#include "enemy.h"       // inspect: enemy_defs, enemy_hp, enemy_level, trait/name
#include <gb/cgb.h>
#include <gb/gb.h>
#include <gb/hardware.h> // DEVICE_SPRITE_PX_OFFSET_* — same convention as entity_sprites OAM X

// SEED_WORDS_N in ui.h — shared with state_game_over.c
#define COMBAT_LOG_LINES 3u
#define COMBAT_LOG_LEN   20u

#define UI_TITLE_TORCH_OAM_L  20u // one 8×8 sprite per torch (no 2×2 upscale)
#define UI_TITLE_TORCH_OAM_R  21u
#define UI_TITLE_FIRE_FIRST   22u
#define UI_TITLE_FIRE_COUNT   8u
#define UI_TITLE_RISE_FIRST   30u // below SP_BELT_SELECTOR 35 — falling border-skull wisps (title + seed menus)
#define UI_TITLE_RISE_COUNT   5u
#define PAL_TITLE_FIRE        7u  // OCP7: orange flame; gameplay restores via load_palettes in ui_title_style_end
#define PAL_TITLE_RISE_SKULL  3u  // OCP3: duplicate of BGP 0 border ramp (ui_title_bkg_pal) for TILE_LOADING_SKULL wisps
#define UI_TITLE_TORCH_PAD_L_TITLE 24u // OAM X = DEVICE_SPRITE_PX_OFFSET_X + this (seed menu uses 8u = 16px further left)

static const palette_color_t ui_title_bkg_pal[] = { // BKG pal 0: dark red field + light text (font pen 3 / paper 0)
    RGB(5, 0, 1),
    RGB(12, 0, 4),
    RGB(20, 6, 8),
    RGB(30, 28, 26),
};
static const palette_color_t ui_default_bkg_pal0[] = { RGB(0, 0, 0), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31) };
static const palette_color_t ui_title_fire_pal[] = { // OCP PAL_TITLE_FIRE — duplicate of render.c pal_ladder (brazier fire sprite in play)
    RGB(0, 0, 0), RGB(6, 8, 12), RGB(31, 16, 2), RGB(31, 26, 8),
};
static const palette_color_t ui_title_torch_sprite_pal[] = { // OCP PAL_PLAYER on menus only — duplicate of pal_ladder (floor brazier BKG in play)
    RGB(0, 0, 0), RGB(6, 8, 12), RGB(31, 16, 2), RGB(31, 26, 8),
};

static uint8_t ui_title_torch_lx, ui_title_torch_rx, ui_title_torch_ty; // fire spawns from torch tops
static uint8_t ui_title_fire_y[UI_TITLE_FIRE_COUNT];
static uint8_t ui_title_fire_x[UI_TITLE_FIRE_COUNT];
static uint8_t ui_title_fire_ttl[UI_TITLE_FIRE_COUNT]; // 0 = slot free
static uint8_t ui_title_rise_y[UI_TITLE_RISE_COUNT];
static uint8_t ui_title_rise_x[UI_TITLE_RISE_COUNT];
static uint8_t ui_title_rise_ttl[UI_TITLE_RISE_COUNT];
static int8_t ui_title_rise_vx[UI_TITLE_RISE_COUNT];

static void ui_title_torch_hide(void) {
    uint8_t i;
    move_sprite(UI_TITLE_TORCH_OAM_L, 0u, 0u);
    move_sprite(UI_TITLE_TORCH_OAM_R, 0u, 0u);
    for (i = UI_TITLE_FIRE_FIRST; i < UI_TITLE_FIRE_FIRST + UI_TITLE_FIRE_COUNT; i++) move_sprite(i, 0u, 0u);
    for (i = UI_TITLE_RISE_FIRST; i < UI_TITLE_RISE_FIRST + UI_TITLE_RISE_COUNT; i++) move_sprite(i, 0u, 0u);
}

static void ui_title_fire_init(void) {
    uint8_t i;
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) ui_title_fire_ttl[i] = 0u;
    for (i = 0u; i < UI_TITLE_RISE_COUNT; i++) ui_title_rise_ttl[i] = 0u;
}

static void ui_title_menu_border_put(uint8_t x, uint8_t y, uint16_t *seq) { // J7/L4 alternate on BKG pal 0 (title ramp)
    uint8_t off = ((*seq) & 1u) ? TILE_FLOOR_DECO_4 : TILE_LOADING_SKULL;
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + off);
    (*seq)++;
    gotoxy(x, y);
    set_bkg_tiles(x, y, 1, 1, &v);
    set_bkg_attribute_xy(x, y, 0u);
    VBK_REG = VBK_TILES;
}

static void ui_title_menu_border_draw(void) { // 20×18 view: one tile deep; clockwise from top-left
    uint16_t seq = 0u;
    uint8_t x;
    int8_t y;
    for (x = 0u; x < 20u; x++) ui_title_menu_border_put(x, 0u, &seq);
    for (y = 1; y <= 16; y++) ui_title_menu_border_put(19u, (uint8_t)y, &seq);
    for (x = 19u;; x--) {
        ui_title_menu_border_put(x, 17u, &seq);
        if (x == 0u) break;
    }
    for (y = 16; y >= 1; y--) ui_title_menu_border_put(0u, (uint8_t)y, &seq);
}

static void ui_title_torch_place(uint8_t bkg_text_row, uint8_t left_pad_px) {
    uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_LIGHT_3); // C3 torch art
    uint8_t ty = (uint8_t)((uint16_t)bkg_text_row * 8u + 4u + 16u); // same baseline as original single-torch title
    uint8_t lx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + left_pad_px);
    uint8_t rx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (160u - 8u - 24u)); // mirror: 24px margin from right visible edge
    uint8_t p = (uint8_t)(PAL_PLAYER & 7u);
    ui_title_torch_lx = lx;
    ui_title_torch_rx = rx;
    ui_title_torch_ty = ty;
    set_sprite_tile(UI_TITLE_TORCH_OAM_L, tt);
    set_sprite_tile(UI_TITLE_TORCH_OAM_R, tt);
    set_sprite_prop(UI_TITLE_TORCH_OAM_L, p);
    set_sprite_prop(UI_TITLE_TORCH_OAM_R, (uint8_t)(p | S_FLIPX));
    move_sprite(UI_TITLE_TORCH_OAM_L, lx, ty);
    move_sprite(UI_TITLE_TORCH_OAM_R, rx, ty);
}

static void ui_title_style_begin(uint8_t bkg_text_row, uint8_t torch_left_pad_px) {
    set_bkg_palette(0u, 1u, ui_title_bkg_pal);
    set_sprite_palette(PAL_PLAYER, 1u, ui_title_torch_sprite_pal);
    set_sprite_palette(PAL_TITLE_FIRE, 1u, ui_title_fire_pal);
    set_sprite_palette(PAL_TITLE_RISE_SKULL, 1u, ui_title_bkg_pal); // same ramp as border skull BGP 0 — load_palettes restores OCP3
    ui_title_fire_init();
    ui_title_torch_place(bkg_text_row, torch_left_pad_px);
    SHOW_SPRITES;
}

static void ui_title_logo_draw_bkg(uint8_t map_x, uint8_t map_y_top) { // TITLE_LOGO_MAP_W×H scaled tiles, BKG pal 0
    uint8_t buf[TITLE_LOGO_MAP_W];
    uint8_t j, i;
    for (j = 0u; j < TITLE_LOGO_MAP_H; j++) {
        for (i = 0u; i < TITLE_LOGO_MAP_W; i++)
            buf[i] = title_logo_bkg_vram_slot[j * TITLE_LOGO_MAP_W + i];
        set_bkg_tiles(map_x, (uint8_t)(map_y_top + j), TITLE_LOGO_MAP_W, 1u, buf);
    }
    VBK_REG = VBK_ATTRIBUTES;
    for (j = 0u; j < TITLE_LOGO_MAP_H; j++) {
        for (i = 0u; i < TITLE_LOGO_MAP_W; i++)
            set_bkg_attribute_xy((uint8_t)(map_x + i), (uint8_t)(map_y_top + j), 0u);
    }
    VBK_REG = VBK_TILES;
}

static void ui_title_style_end(void) {
    title_logo_bkg_vram_restore(); // HOME: MBC-safe tileset read — was in bank 5 and could white-screen after return
    set_bkg_palette(0u, 1u, ui_default_bkg_pal0);
    ui_title_torch_hide();
    load_palettes(); // BANKED in render.c — do not SWITCH_ROM here; stubs must own bank save/restore
}

static void ui_title_try_spawn_fire(uint8_t from_right, uint16_t fc) {
    uint8_t i;
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) {
        if (ui_title_fire_ttl[i] != 0u) continue;
        {
            uint8_t base_x = from_right
                ? (uint8_t)(ui_title_torch_rx + 3u + (uint8_t)(fc & 7u))
                : (uint8_t)(ui_title_torch_lx + 3u + (uint8_t)((fc >> 1) & 7u));
            ui_title_fire_x[i] = base_x;
            ui_title_fire_y[i] = (uint8_t)(ui_title_torch_ty - 2u);
            ui_title_fire_ttl[i] = (uint8_t)(24u + (uint8_t)(DIV_REG & 11u));
            return;
        }
    }
}

static void ui_title_try_spawn_rise(uint16_t fc) { // border-skull sprites only; fall from top with horizontal tumble (screen-space)
    uint8_t i;
    for (i = 0u; i < UI_TITLE_RISE_COUNT; i++) {
        if (ui_title_rise_ttl[i] != 0u) continue;
        {
            uint8_t x = (uint8_t)(16u + (uint8_t)((uint16_t)(DIV_REG + (uint8_t)fc + (uint16_t)i * 23u) % 120u));
            ui_title_rise_x[i] = x;
            ui_title_rise_y[i] = (uint8_t)(16u + (uint8_t)(DIV_REG & 15u)); // top band; DIV jitter spreads rows
            ui_title_rise_ttl[i] = (uint8_t)(72u + (uint8_t)(DIV_REG & 47u)); // long enough to cross ~144px at ~2px/frame
            ui_title_rise_vx[i] = (int8_t)((DIV_REG & 1u) ? 2 : -2);
            return;
        }
    }
}

static void ui_title_menu_anim_tick(uint16_t frame_counter) {
    uint8_t i;
    uint8_t ft = (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE);
    uint8_t fp = (uint8_t)(PAL_TITLE_FIRE & 7u);
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) {
        if (ui_title_fire_ttl[i] == 0u) continue;
        {
            uint8_t y = ui_title_fire_y[i];
            int16_t nx = (int16_t)ui_title_fire_x[i];
            if (((frame_counter + (uint16_t)i) & 3u) == 1u) nx++;
            else if (((frame_counter + (uint16_t)i) & 3u) == 3u) nx--;
            if (nx < 8) nx = 8;
            if (nx > 152) nx = 152;
            y = (uint8_t)((uint16_t)y - 1u);
            if (((frame_counter + i) & 1u) == 0u) y = (uint8_t)((uint16_t)y - 1u); // ~2 px/frame average rise
            ui_title_fire_ttl[i] = (uint8_t)(ui_title_fire_ttl[i] - 1u);
            if (ui_title_fire_ttl[i] == 0u || y < 20u) {
                ui_title_fire_ttl[i] = 0u;
                move_sprite((uint8_t)(UI_TITLE_FIRE_FIRST + i), 0u, 0u);
            } else {
                ui_title_fire_y[i] = y;
                ui_title_fire_x[i] = (uint8_t)nx;
                set_sprite_tile((uint8_t)(UI_TITLE_FIRE_FIRST + i), ft);
                set_sprite_prop((uint8_t)(UI_TITLE_FIRE_FIRST + i), fp);
                move_sprite((uint8_t)(UI_TITLE_FIRE_FIRST + i), (uint8_t)nx, y);
            }
        }
    }
    if ((frame_counter & 3u) == 0u) ui_title_try_spawn_fire(0, frame_counter);
    if ((frame_counter & 3u) == 2u) ui_title_try_spawn_fire(1, frame_counter);
    if ((frame_counter & 1u) == 0u) ui_title_try_spawn_rise(frame_counter);
    if ((frame_counter & 7u) == 3u) ui_title_try_spawn_rise((uint16_t)(frame_counter ^ 0xA5A5u));
    for (i = 0u; i < UI_TITLE_RISE_COUNT; i++) {
        if (ui_title_rise_ttl[i] == 0u) continue;
        {
            uint8_t y = ui_title_rise_y[i];
            int16_t nx = (int16_t)ui_title_rise_x[i];
            int8_t vx = ui_title_rise_vx[i];
            nx += (int16_t)vx;
            if ((((uint16_t)y + frame_counter + (uint16_t)i) & 7u) == 0u)
                nx += ((DIV_REG & 1u) ? 2 : -2);
            if (nx < 8) { nx = 8; if (vx < 0) vx = (int8_t)(-vx); }
            if (nx > 152) { nx = 152; if (vx > 0) vx = (int8_t)(-vx); }
            ui_title_rise_vx[i] = vx;
            y = (uint8_t)((uint16_t)y + 1u);
            if ((((uint16_t)frame_counter + (uint16_t)i + (uint16_t)DIV_REG) & 1u) == 0u)
                y = (uint8_t)((uint16_t)y + 1u);
            ui_title_rise_ttl[i] = (uint8_t)(ui_title_rise_ttl[i] - 1u);
            if (ui_title_rise_ttl[i] == 0u || y > 164u) {
                ui_title_rise_ttl[i] = 0u;
                move_sprite((uint8_t)(UI_TITLE_RISE_FIRST + i), 0u, 0u);
            } else {
                uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_LOADING_SKULL);
                uint8_t pr = (uint8_t)(PAL_TITLE_RISE_SKULL & 7u);
                if ((((uint16_t)frame_counter + (uint16_t)i + (uint16_t)(y >> 2u)) & 1u) != 0u) pr |= S_FLIPX;
                ui_title_rise_y[i] = y;
                ui_title_rise_x[i] = (uint8_t)nx;
                set_sprite_tile((uint8_t)(UI_TITLE_RISE_FIRST + i), tt);
                set_sprite_prop((uint8_t)(UI_TITLE_RISE_FIRST + i), pr);
                move_sprite((uint8_t)(UI_TITLE_RISE_FIRST + i), (uint8_t)nx, y);
            }
        }
    }
    { // brazier bases: alternate C3/C4 every few frames (shared by title + seed-word menus)
        uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET
            + (((frame_counter >> 2) & 1u) ? TILE_LIGHT_4 : TILE_LIGHT_3));
        set_sprite_tile(UI_TITLE_TORCH_OAM_L, tt);
        set_sprite_tile(UI_TITLE_TORCH_OAM_R, tt);
    }
}

static char combat_log[COMBAT_LOG_LINES][COMBAT_LOG_LEN];
static uint8_t combat_log_pal[COMBAT_LOG_LINES]; // per-row CGB palette when drawing log lines

UIPanelMode ui_panel_mode = UI_PANEL_COMBAT;
static uint8_t panel_inspect_slot;

static uint8_t chat_quiet_turns;              // player turns with log nonempty and reclaim not armed
static uint8_t chat_reclaim_done_until_push; // after auto-clear: skip counting until ui_combat_log_push

static uint8_t combat_log_any(void) {
    return combat_log[0][0] || combat_log[1][0] || combat_log[2][0];
}

static void combat_log_zero_buffers(void) {
    uint8_t i, j;
    for (i = 0; i < COMBAT_LOG_LINES; i++) {
        for (j = 0; j < COMBAT_LOG_LEN; j++) combat_log[i][j] = 0;
        combat_log_pal[i] = PAL_UI;
    }
}

void ui_combat_log_clear(void) BANKED {
    combat_log_zero_buffers();
    chat_quiet_turns = 0u;
    chat_reclaim_done_until_push = 0u;
}

void ui_combat_log_push_pal(const char *line, uint8_t pal) BANKED {
    uint8_t r, i;
    chat_quiet_turns = 0u;
    chat_reclaim_done_until_push = 0u;
    for (r = 0; r < COMBAT_LOG_LINES - 1u; r++) { // shift lines up (drop oldest)
        for (i = 0; i < COMBAT_LOG_LEN; i++)
            combat_log[r][i] = combat_log[r + 1u][i];
        combat_log_pal[r] = combat_log_pal[r + 1u];
    }
    for (i = 0; i < COMBAT_LOG_LEN; i++) combat_log[COMBAT_LOG_LINES - 1u][i] = 0;
    for (i = 0; i < COMBAT_LOG_LEN - 1u && line[i]; i++)
        combat_log[COMBAT_LOG_LINES - 1u][i] = line[i];
    combat_log_pal[COMBAT_LOG_LINES - 1u] = pal;
}

void ui_combat_log_push(const char *line) BANKED {
    ui_combat_log_push_pal(line, PAL_UI);
}

#define UI_MSG_LINE 20u // matches ui combat log row cap

void ui_push_combat_log(uint8_t type_idx, uint8_t dmg, uint8_t hp_remaining_for_pct) BANKED {
    char logbuf[UI_MSG_LINE];
    char namebuf[12]; // SLIMESKULL + NUL is the longest current name; copy here to stay valid after bcall returns to bank 5
    uint8_t p = 0, d = dmg, mhp, pct, ni;
    enemy_type_short_name_copy(type_idx, namebuf, sizeof namebuf);
    for (ni = 0u; namebuf[ni] && p < 9u; ni++) logbuf[p++] = namebuf[ni];
    logbuf[p++] = ' ';
    if (d) {
        logbuf[p++] = '-';
        if (d >= 100u) { logbuf[p++] = (char)('0' + d / 100u); d %= 100u; }
        if (d >= 10u)  { logbuf[p++] = (char)('0' + d / 10u);  d %= 10u; }
        logbuf[p++] = (char)('0' + d);
        mhp = enemy_effective_max_hp(type_idx);
        pct = mhp ? (uint8_t)(((uint16_t)hp_remaining_for_pct * 100u) / (uint16_t)mhp) : 0u;
        if (pct > 99u) pct = 99u;
        logbuf[p++] = ' ';
        if (pct >= 10u) { logbuf[p++] = (char)('0' + pct / 10u); logbuf[p++] = (char)('0' + pct % 10u); }
        else            { logbuf[p++] = (char)('0' + pct); }
        logbuf[p++] = '%';
    } else {
        logbuf[p++] = 'D'; logbuf[p++] = 'I'; logbuf[p++] = 'E'; logbuf[p++] = 'S';
    }
    logbuf[p] = 0;
    ui_combat_log_push_pal(logbuf, PAL_UI);
}

void ui_push_combat_log_shield_burn(uint8_t type_idx, uint8_t dmg, uint8_t hp_remaining_for_pct) BANKED {
    char logbuf[UI_MSG_LINE];
    char namebuf[12];
    uint8_t p = 0, d = dmg, mhp, pct, ni;
    if (dmg == 0u) return; // only "-N" lines; kills pass lethal damage, not a separate DIES
    enemy_type_short_name_copy(type_idx, namebuf, sizeof namebuf);
    for (ni = 0u; namebuf[ni] && p < 9u; ni++) logbuf[p++] = namebuf[ni];
    logbuf[p++] = ' ';
    logbuf[p++] = 'b';
    logbuf[p++] = 'u';
    logbuf[p++] = 'r';
    logbuf[p++] = 'n';
    logbuf[p++] = 'e';
    logbuf[p++] = 'd';
    logbuf[p++] = ' ';
    if (d) {
        logbuf[p++] = '-';
        if (d >= 100u) {
            logbuf[p++] = (char)('0' + d / 100u);
            d %= 100u;
        }
        if (d >= 10u) {
            logbuf[p++] = (char)('0' + d / 10u);
            d %= 10u;
        }
        logbuf[p++] = (char)('0' + d);
        if (hp_remaining_for_pct > 0u) {
            mhp = enemy_effective_max_hp(type_idx);
            pct = mhp ? (uint8_t)(((uint16_t)hp_remaining_for_pct * 100u) / (uint16_t)mhp) : 0u;
            if (pct > 99u) pct = 99u;
            if (p + 5u < COMBAT_LOG_LEN) {
                logbuf[p++] = ' ';
                if (pct >= 10u) {
                    logbuf[p++] = (char)('0' + pct / 10u);
                    logbuf[p++] = (char)('0' + pct % 10u);
                } else {
                    logbuf[p++] = (char)('0' + pct);
                }
                logbuf[p++] = '%';
            }
        }
    }
    logbuf[p] = 0;
    ui_combat_log_push_pal(logbuf, PAL_XP_UI);
}

void ui_push_xp_gain_line(uint8_t amt) BANKED {
    char buf[UI_MSG_LINE];
    uint8_t p = 0;
    uint16_t need = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
    uint16_t pct16 = need ? (((uint16_t)amt * 100u) / need) : 0u;
    uint8_t pct = (pct16 > 100u) ? 100u : (uint8_t)pct16;
    buf[p++] = '+';
    if (amt >= 100u)      { buf[p++] = (char)('0' + amt / 100u); buf[p++] = (char)('0' + (amt % 100u) / 10u); buf[p++] = (char)('0' + amt % 10u); }
    else if (amt >= 10u)  { buf[p++] = (char)('0' + amt / 10u);  buf[p++] = (char)('0' + amt % 10u); }
    else                  { buf[p++] = (char)('0' + amt); }
    buf[p++] = ' '; buf[p++] = 'X'; buf[p++] = 'P'; buf[p++] = ' '; buf[p++] = '(';
    if (pct >= 100u)     { buf[p++] = '1'; buf[p++] = '0'; buf[p++] = '0'; }
    else if (pct >= 10u) { buf[p++] = (char)('0' + pct / 10u); buf[p++] = (char)('0' + pct % 10u); }
    else                 { buf[p++] = (char)('0' + pct); }
    buf[p++] = '%'; buf[p++] = ')'; buf[p] = 0;
    ui_combat_log_push_pal(buf, PAL_XP_UI);
}

void ui_push_level_up_line(uint8_t new_level) BANKED {
    char buf[UI_MSG_LINE];
    uint8_t i = 0;
    const char *s = "You level up! (";
    while (*s) buf[i++] = *s++;
    if (new_level >= 100u)     { buf[i++] = (char)('0' + new_level / 100u); buf[i++] = (char)('0' + (new_level % 100u) / 10u); buf[i++] = (char)('0' + new_level % 10u); }
    else if (new_level >= 10u) { buf[i++] = (char)('0' + new_level / 10u);  buf[i++] = (char)('0' + new_level % 10u); }
    else                       { buf[i++] = (char)('0' + new_level); }
    buf[i++] = ')'; buf[i] = 0;
    ui_combat_log_push_pal(buf, PAL_UI);
}

uint8_t ui_combat_log_tick_quiet_turn(void) BANKED {
    if (chat_reclaim_done_until_push) return 0u;
    if (!combat_log_any()) return 0u;
    if (chat_quiet_turns < 255u) chat_quiet_turns++;
    if (chat_quiet_turns < UI_CHAT_RECLAIM_AFTER_TURNS) return 0u;
    combat_log_zero_buffers();
    chat_quiet_turns = 0u;
    chat_reclaim_done_until_push = 1u;
    return 1u;
}

const char *const seed_words_desc[SEED_WORDS_N] = { // first word line (adjective-ish)
    "ASHEN","BLEAK","BLIND","BLOOD","BLUNT","BONED","BURNT","COLD","CRUEL","CURST",
    "DARK","DEAD","DEEP","DENSE","DIRE","DREAD","DREAR","DULL","DUSK","DUSTY",
    "EMBER","FELL","FETID","FOUL","GAUNT","GRIM","IRON","LOST","LUNAR","MURKY",
    "PALE","ROT","SHADE","SHORN","STARK","STILL","SUNK","TOXIC","VILE","VOID"
};
const char *const seed_words_noun[SEED_WORDS_N] = { // second word on row 1
    "ASH","BANE","BLOT","BONE","BRIAR","BRIER","COIL","CROW","CRYPT","DUST",
    "EMBER","FANG","FLAME","FROND","FROST","FUNGI","HORN","IRON","LARVA","MIRE",
    "MIST","MOSS","MOTH","MURK","PITH","ROOT","RUIN","SHADE","SKULL","SLIME",
    "SMOKE","SPINE","SPORE","STONE","THORN","TIDE","TOMB","VENOM","WILT","WORM"
};
const char *const seed_words_place[SEED_WORDS_N] = { // row 2 place name
    "ABYSS","BOG","BRINK","CAIRN","CAVES","CHASM","CRAGS","CRYPT","DEEP","DELL",
    "DELVE","DUNES","FELLS","FLATS","GORGE","GULCH","HEATH","KEEP","MARSH","MIRE",
    "MOORS","MOUND","NOOK","PEAKS","PITS","RIFT","RUINS","SANDS","SHELF","SHORE",
    "SLOPE","SPIRE","STEPS","TOMB","VALE","VAULT","WASTE","WEALD","WILDS","WOOD"
};

static void set_win_attribute_xy(uint8_t x, uint8_t y, uint8_t a) { // CGB palette for window map
    VBK_REG = VBK_ATTRIBUTES;
    set_win_tile_xy(x, y, a);
    VBK_REG = VBK_TILES;
}

static void win_putc(uint8_t x, uint8_t y, char c) { // IBM font tile indices match setchar (tile 0 = space)
    uint8_t t = (uint8_t)((uint8_t)((unsigned char)c - 32u));
    set_win_tile_xy(x, y, t);
    set_win_attribute_xy(x, y, PAL_UI);
}

static void win_putc_pal(uint8_t x, uint8_t y, char c, uint8_t pal) { // char with explicit CGB palette
    uint8_t t = (uint8_t)((uint8_t)((unsigned char)c - 32u));
    set_win_tile_xy(x, y, t);
    set_win_attribute_xy(x, y, pal);
}

static void win_puts(uint8_t x, uint8_t y, const char *s, uint8_t pal) { // null-terminated string to window row
    while (*s) { win_putc_pal(x++, y, *s++, pal); }
}

static void win_put_uint8(uint8_t x, uint8_t y, uint8_t v, uint8_t width, uint8_t pal) { // right-justified decimal
    char dig[3];
    uint8_t i = 0, pad;
    if (v == 0) { dig[i++] = '0'; }
    else { while (v) { dig[i++] = (char)('0' + (v % 10u)); v /= 10u; } }
    for (pad = i; pad < width; pad++) win_putc_pal(x++, y, ' ', pal); // space-pad leading
    while (i--) win_putc_pal(x++, y, dig[i], pal);
}

static void win_put_space(uint8_t x, uint8_t y) { // blank space tile + UI palette
    set_win_tile_xy(x, y, 0u);
    set_win_attribute_xy(x, y, PAL_UI);
}

static void win_puts_row_pad_cols(uint8_t y, const char *s, uint8_t pal, uint8_t cols) {
    uint8_t x = 0;
    while (*s && x < cols) win_putc_pal(x++, y, *s++, pal);
    while (x < cols) win_put_space(x++, y);
}

static void win_clear_row(uint8_t win_y, uint8_t pal) {
    uint8_t x;
    for (x = 0; x < UI_PANEL_COLS; x++) win_putc_pal(x, win_y, ' ', pal);
}

static void ui_draw_belt_placeholder_row(void) { // N4/O4 "SPELL" | 4×(slot + uses) | pad | N6/O6 "ITEM"
    uint8_t x = 0u, s, v, icon_pal;
    v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SPELL_L);
    set_win_tile_xy(x, UI_BELT_WIN_Y, v);
    set_win_attribute_xy(x++, UI_BELT_WIN_Y, PAL_UI);
    v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SPELL_R);
    set_win_tile_xy(x, UI_BELT_WIN_Y, v);
    set_win_attribute_xy(x++, UI_BELT_WIN_Y, PAL_UI);
    for (s = 0u; s < BELT_SLOT_COUNT; s++) {
        if (s == 0u && player_class == 2u && player_level >= 1u) {
            v = TILE_WITCH_BOLT_VRAM;
            icon_pal = (witch_shot_cooldown_turns == 0u) ? PAL_WALL_BG : PAL_CORPSE;
        } else if (s == 0u && player_class == 3u && player_level >= 1u) { // zerker slot 0 — Whirlwind (I10)
            v = TILE_ZERKER_WHIRLWIND_VRAM;
            icon_pal = (zerker_whirlwind_cooldown_turns == 0u) ? PAL_WALL_BG : PAL_CORPSE;
        } else if (s == 0u && player_class == 0u && player_level >= 1u) { // knight slot 0 — holy fire shield (I9 art; LIFE_UI palette while active, WALL_BG when ready)
            v = TILE_KNIGHT_SHIELD_VRAM;
            icon_pal = knight_shield_active ? PAL_LIFE_UI : PAL_WALL_BG;
        } else {
            v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY); // empty: M14 patched onto K1 VRAM at boot; else TILE_ITEM_* when wired
            icon_pal = PAL_UI;
        }
        set_win_tile_xy(x, UI_BELT_WIN_Y, v);
        set_win_attribute_xy(x++, UI_BELT_WIN_Y, icon_pal);
        if (belt_slot_charges[s] == 0u) win_put_space(x++, UI_BELT_WIN_Y);
        else win_putc_pal(x++, UI_BELT_WIN_Y, (char)('0' + (belt_slot_charges[s] > 9u ? 9u : belt_slot_charges[s])), PAL_UI);
    }
    while (x < 18u) win_put_space(x++, UI_BELT_WIN_Y);
    v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_ITEM_L);
    set_win_tile_xy(x, UI_BELT_WIN_Y, v);
    set_win_attribute_xy(x++, UI_BELT_WIN_Y, PAL_UI);
    v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_ITEM_R);
    set_win_tile_xy(x, UI_BELT_WIN_Y, v);
    set_win_attribute_xy(x++, UI_BELT_WIN_Y, PAL_UI);
}

static void ui_draw_top_hud(void) { // bottom window row: L:♥×5 HP% XP% FLOORdd
    uint8_t hy = UI_HUD_WIN_Y, tx = 0;
    uint8_t k, pct = (uint8_t)((uint16_t)player_hp * 100u / player_hp_max);
    uint8_t pct8 = pct, xp_pct;
    uint8_t vram;

    win_putc_pal(tx++, hy, 'L', PAL_LIFE_UI);
    win_putc_pal(tx++, hy, ':', PAL_LIFE_UI);
    for (k = 0; k < LIFE_BAR_LEN; k++) {
        if (pct >= (uint8_t)(20u * (k + 1u))) {
            vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_HEART_FULL);
            set_win_tile_xy(tx, hy, vram);
            set_win_attribute_xy(tx, hy, PAL_LIFE_UI);
        } else if (pct >= (uint8_t)(20u * k + 10u)) {
            vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_HEART_HALF);
            set_win_tile_xy(tx, hy, vram);
            set_win_attribute_xy(tx, hy, PAL_LIFE_UI);
        } else {
            win_putc_pal(tx, hy, '_', PAL_LIFE_UI);
        }
        tx++;
    }
    win_put_uint8(tx, hy, pct8, 3, PAL_LIFE_UI);
    tx = (uint8_t)(tx + 3u);
    win_putc_pal(tx++, hy, '%', PAL_LIFE_UI);
    {
        uint16_t next_level_xp = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
        xp_pct = (player_xp >= next_level_xp) ? 99u : (uint8_t)((player_xp * 100u) / next_level_xp);
    }
    win_putc_pal(tx++, hy, 'X', PAL_XP_UI);
    win_putc_pal(tx++, hy, 'P', PAL_XP_UI);
    win_putc_pal(tx++, hy, (char)('0' + xp_pct / 10u), PAL_XP_UI);
    win_putc_pal(tx++, hy, (char)('0' + xp_pct % 10u), PAL_XP_UI);
    win_putc_pal(tx++, hy, '%', PAL_XP_UI);
    vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_FLOOR_L);
    set_win_tile_xy(tx, hy, vram);
    set_win_attribute_xy(tx++, hy, PAL_UI);
    vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_FLOOR_R);
    set_win_tile_xy(tx, hy, vram);
    set_win_attribute_xy(tx++, hy, PAL_UI);
    win_putc_pal(tx++, hy, (char)('0' + floor_num / 10u), PAL_UI);
    win_putc_pal(tx++, hy, (char)('0' + floor_num % 10u), PAL_UI);
    while (tx < GRID_W) win_put_space(tx++, hy);
}

static void format_class_level_buf(char *buf) { // "KNIGHT, Lvl 3" into buf (COMBAT_LOG_LEN)
    static const char *const kc[PLAYER_CLASS_COUNT] = { "KNIGHT", "SCOUNDREL", "WITCH", "ZERKER" };
    uint8_t i = 0u;
    uint8_t v;
    const char *s = kc[(unsigned)player_class % PLAYER_CLASS_COUNT];
    while (*s) buf[i++] = *s++;
    buf[i++] = ',';
    buf[i++] = ' ';
    buf[i++] = 'L';
    buf[i++] = 'v';
    buf[i++] = 'l';
    buf[i++] = ' ';
    v = (player_level > 99u) ? 99u : player_level;
    if (v >= 10u) buf[i++] = (char)('0' + v / 10u);
    buf[i++] = (char)('0' + (v % 10u));
    buf[i] = '\0';
}

static void ui_draw_class_level_line(uint8_t win_y) { // idle panel top row
    char buf[COMBAT_LOG_LEN];
    format_class_level_buf(buf);
    win_puts_row_pad_cols(win_y, buf, PAL_UI, UI_PANEL_COLS);
}

static void ui_draw_reclaim_idle_panel(void) { // after 8 quiet turns: one line only, no seed/zone
    char buf[COMBAT_LOG_LEN];
    format_class_level_buf(buf);
    win_puts_row_pad_cols(UI_PANEL_WIN_Y0, buf, PAL_UI, UI_PANEL_COLS);
    win_clear_row(UI_PANEL_WIN_Y1, PAL_UI);
    win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
}

static void ui_draw_combat_panel(void) {
    uint8_t i;
    if (!combat_log_any()) {
        if (chat_reclaim_done_until_push) ui_draw_reclaim_idle_panel();
        else {
            ui_draw_class_level_line(UI_PANEL_WIN_Y0);
            ui_draw_seed_words(run_seed, UI_PANEL_WIN_Y1, UI_PANEL_WIN_Y2);
        }
    } else {
        for (i = 0; i < COMBAT_LOG_LINES; i++)
            win_puts_row_pad_cols((uint8_t)(UI_PANEL_WIN_Y0 + i), combat_log[i], combat_log_pal[i], UI_PANEL_COLS);
    }
}

static void ui_draw_inspect_panel(void) {
    uint8_t slot = panel_inspect_slot;
    uint8_t t, x;
    if (slot >= num_enemies || !enemy_alive[slot]) {
        win_clear_row(UI_PANEL_WIN_Y0, PAL_UI);
        win_clear_row(UI_PANEL_WIN_Y1, PAL_UI);
        win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
        return;
    }
    t = enemy_type[slot];
    {
        char namebuf[12];
        enemy_type_short_name_copy(t, namebuf, sizeof namebuf);
        win_puts_row_pad_cols(UI_PANEL_WIN_Y0, namebuf, PAL_UI, UI_PANEL_COLS);
    }
    { // HP N/M — one line under name (panel rows 1–2; HUD on row 4 after ui_draw_bottom_rows)
        uint8_t hp = enemy_hp[slot], mhp = enemy_effective_max_hp(t);
        x = 0;
        win_putc_pal(x++, UI_PANEL_WIN_Y1, 'H', PAL_UI);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, 'P', PAL_UI);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, ' ', PAL_UI);
        win_put_uint8(x, UI_PANEL_WIN_Y1, hp, 3, PAL_UI); x = (uint8_t)(x + 3u);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, '/', PAL_UI);
        win_put_uint8(x, UI_PANEL_WIN_Y1, mhp, 3, PAL_UI); x = (uint8_t)(x + 3u);
        while (x < UI_PANEL_COLS) win_putc_pal(x++, UI_PANEL_WIN_Y1, ' ', PAL_UI);
    }
    win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
}

static uint8_t ui_u8_digits(uint8_t v) { // compact width accounting for perf panel layout
    if (v >= 100u) return 3u;
    if (v >= 10u) return 2u;
    return 1u;
}

static void ui_draw_perf_metric(uint8_t y, uint8_t *x, const char *tag, PerfMetric m) { // "TAGa/m" compact metric fragment
    uint8_t avg = perf_avg(m);
    uint8_t max = perf_max(m);
    win_puts(*x, y, tag, PAL_UI); *x = (uint8_t)(*x + 2u);
    win_put_uint8(*x, y, avg, 2u, PAL_UI); *x = (uint8_t)(*x + ui_u8_digits(avg));
    win_putc_pal((*x)++, y, '/', PAL_UI);
    win_put_uint8(*x, y, max, 2u, PAL_UI); *x = (uint8_t)(*x + ui_u8_digits(max));
}

static void ui_draw_perf_pair(uint8_t y, const char *tag_l, PerfMetric l, const char *tag_r, PerfMetric r) {
    uint8_t x = 0u;
    ui_draw_perf_metric(y, &x, tag_l, l);
    win_putc_pal(x++, y, ' ', PAL_UI);
    ui_draw_perf_metric(y, &x, tag_r, r);
    while (x < UI_PANEL_COLS) win_put_space(x++, y);
}

static void ui_draw_perf_panel(void) { // 3-line rolling avg/max in DIV ticks
    ui_draw_perf_pair(UI_PANEL_WIN_Y0, "AI", PERF_ENEMY_MOVE, "CM", PERF_CAMERA_SCROLL);
    ui_draw_perf_pair(UI_PANEL_WIN_Y1, "RD", PERF_DRAW_SCREEN, "OV", PERF_DRAW_OVERLAY);
    {
        uint8_t x = 0u;
        ui_draw_perf_metric(UI_PANEL_WIN_Y2, &x, "HT", PERF_HIT_RESOLVE);
        while (x < UI_PANEL_COLS) win_put_space(x++, UI_PANEL_WIN_Y2);
    }
}

static void put_word5(uint8_t x, uint8_t y, const char *s) { // fixed 5-char word into BKG via setchar
    uint8_t i;
    for (i = 0; i < 5; i++) { gotoxy((uint8_t)(x+i), y); setchar(s[i] ? s[i] : ' '); } // pad short strings
}

static void put_word5_win(uint8_t x, uint8_t y, const char *s) { // same for window tilemap rows 0–1
    uint8_t i;
    for (i = 0; i < 5; i++) { win_putc((uint8_t)(x+i), y, s[i] ? s[i] : ' '); }
}

void run_seed_to_triple(uint16_t seed, uint8_t *d, uint8_t *n, uint8_t *p) BANKED { // same logic as ui_draw_seed_words / picker
    uint16_t s = seed;
    if (s < 1u) s = 1u;
    if (s > 64000u) s = 64000u;
    s--;
    *d = (uint8_t)(s % 40u);
    *n = (uint8_t)((s / 40u) % 40u);
    *p = (uint8_t)((s / 1600u) % 40u);
}

void ui_game_over_put_seed_words(uint8_t d, uint8_t n, uint8_t p) BANKED { // BKG rows 9–10; must run in bank(ui) so seed string ROM is mapped
    put_word5(0u, 9u, seed_words_desc[d]);
    put_word5(6u, 9u, seed_words_noun[n]);
    put_word5(0u, 10u, seed_words_place[p]);
}

void window_ui_show(void) BANKED { // belt + 3 text rows + HUD; WIN from UI_WINDOW_Y_START down
    uint8_t wx, wy;
    WX_REG = 7u;
    WY_REG = UI_WINDOW_Y_START;
    VBK_REG = VBK_TILES; // match lcd_clear_display: tile + attr planes stay paired (CGB)
    fill_win_rect(0, 0, 32, UI_WINDOW_TILE_ROWS, 0u);
    VBK_REG = VBK_ATTRIBUTES;
    fill_win_rect(0, 0, 32, UI_WINDOW_TILE_ROWS, 0u);
    VBK_REG = VBK_TILES;
    for (wy = 0; wy < UI_WINDOW_TILE_ROWS; wy++)
        for (wx = 0; wx < 32u; wx++)
            set_win_attribute_xy(wx, wy, PAL_UI);
}

void window_ui_hide(void) BANKED {
    HIDE_WIN;
}

#define UI_LOAD_SKULL_OAM_L 38u // above SP_ENEMY_BASE+NUM_ENEMIES
#define UI_LOAD_SKULL_OAM_R 39u

extern volatile uint8_t ui_loading_active;
extern volatile uint8_t ui_load_phase;

void ui_loading_screen_begin(void) BANKED {
    uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_LOADING_SKULL);
    ui_load_phase = 0;
    ui_loading_active = 1u;
    music_loading_screen_set(1u);
    gotoxy(5, 8);
    printf("Descending");
    set_sprite_tile(UI_LOAD_SKULL_OAM_L, tt);
    set_sprite_tile(UI_LOAD_SKULL_OAM_R, tt);
    set_sprite_prop(UI_LOAD_SKULL_OAM_L, (uint8_t)(PAL_CORPSE & 7u)); // grey ramp slot 0 — keep PAL_UI (6) for non-enemy text only
    set_sprite_prop(UI_LOAD_SKULL_OAM_R, (uint8_t)(PAL_CORPSE & 7u));
    SHOW_SPRITES;
    move_sprite(UI_LOAD_SKULL_OAM_L, 32u, 80u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 136u, 80u);
}

void ui_loading_screen_end(void) BANKED {
    music_loading_screen_set(0u);
    ui_loading_active = 0u;
    move_sprite(UI_LOAD_SKULL_OAM_L, 0u, 0u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 0u, 0u);
}

void ui_draw_bottom_rows(void) BANKED {
    VBK_REG = VBK_TILES; // render.c leaves VBK 0 but other paths may not — win writes assume tile plane
    ui_draw_belt_placeholder_row();
    switch (ui_panel_mode) {
        case UI_PANEL_COMBAT:   ui_draw_combat_panel();   break;
        case UI_PANEL_PERF:     ui_draw_perf_panel();     break;
        case UI_PANEL_INSPECT:  ui_draw_inspect_panel();  break;
        default:                ui_draw_combat_panel();   break;
    }
    ui_draw_top_hud();
}

void ui_draw_seed_words(uint16_t seed, uint8_t win_y_desc_noun, uint8_t win_y_place) BANKED {
    uint8_t x, d, n, p;
    run_seed_to_triple(seed, &d, &n, &p);
    put_word5_win(0u, win_y_desc_noun, seed_words_desc[d]);
    put_word5_win(6u, win_y_desc_noun, seed_words_noun[n]);
    for (x = 11; x < UI_PANEL_COLS; x++) win_put_space(x, win_y_desc_noun);
    put_word5_win(0u, win_y_place, seed_words_place[p]);
    for (x = 5; x < UI_PANEL_COLS; x++) win_put_space(x, win_y_place);
}

void ui_panel_show_combat(void) BANKED { ui_panel_mode = UI_PANEL_COMBAT; }

void ui_panel_toggle_perf(void) BANKED {
    if (ui_panel_mode == UI_PANEL_PERF) ui_panel_mode = UI_PANEL_COMBAT;
    else ui_panel_mode = UI_PANEL_PERF;
}

void ui_panel_show_inspect(uint8_t enemy_slot) BANKED {
    panel_inspect_slot = enemy_slot;
    ui_panel_mode = UI_PANEL_INSPECT;
}

uint8_t ui_panel_inspect_slot(void) BANKED { return panel_inspect_slot; }

static uint16_t input_seed_words_screen(uint16_t initial_seed, uint16_t entropy_hint) { // interactive seed picker
    uint8_t  word_pos = 0, prev_j = 0; // word_pos 0..2 selects which word the caret edits
    uint16_t frame_counter = 0;              // feeds entropy mixer each frame
    uint16_t s = initial_seed ? initial_seed : 1u;
    if (s > 64000u) s = 64000u;
    s--;
    uint8_t d = (uint8_t)(s % 40u);             // current indices
    uint8_t n = (uint8_t)((s / 40u) % 40u);
    uint8_t p = (uint8_t)((s / 1600u) % 40u);

    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("UI:seed");
    ui_title_style_begin(1u, (uint8_t)(UI_TITLE_TORCH_PAD_L_TITLE - 16u)); // left brazier −16px vs title

    while (1) {
        gotoxy(3, 1); printf("SEED WORDS");
        put_word5(1,  3, seed_words_desc[d]);
        gotoxy(6,  3); setchar(' ');
        put_word5(7,  3, seed_words_noun[n]);
        gotoxy(12, 3); setchar(' ');
        put_word5(13, 3, seed_words_place[p]);
        gotoxy(1,  4); printf("     ");
        gotoxy(6,  4); printf("     ");
        gotoxy(11, 4); printf("     ");
        gotoxy(1 + word_pos * 6, 4); setchar('^'); // caret under active word block
        gotoxy(1, 6); printf("L/R word");
        gotoxy(1, 7); printf("U/D scroll");
        gotoxy(1, 8); printf("New seed: (A)");
        gotoxy(1, 9); printf("START=play");
        {
            uint8_t j    = joypad();
            uint8_t edge = (uint8_t)(j & (uint8_t)~prev_j); // buttons newly pressed this frame
            if (edge & J_START) {
                uint16_t fs = (uint16_t)(1u + (uint16_t)d + 40u*(uint16_t)n + 1600u*(uint16_t)p); // pack triple back to seed
                if (!fs) fs = 1;
                ui_title_style_end();
                return fs;
            }
            if (edge & J_A) {
                uint16_t t = seed_entropy_random_seed(entropy_hint, frame_counter); // reroll all three indices
                if (t > 64000u) t = 64000u; t--;
                d = (uint8_t)(t % 40u); n = (uint8_t)((t/40u) % 40u); p = (uint8_t)((t/1600u) % 40u);
            }
            if (edge & J_LEFT)  word_pos = (word_pos == 0) ? 2 : (uint8_t)(word_pos-1); // wrap caret
            if (edge & J_RIGHT) word_pos = (uint8_t)((word_pos+1) % 3);
            if (edge & J_UP) {
                if      (word_pos == 0) d = (uint8_t)((d+1) % SEED_WORDS_N);
                else if (word_pos == 1) n = (uint8_t)((n+1) % SEED_WORDS_N);
                else                    p = (uint8_t)((p+1) % SEED_WORDS_N);
            }
            if (edge & J_DOWN) {
                if      (word_pos == 0) d = (uint8_t)(d == 0 ? SEED_WORDS_N-1 : d-1);
                else if (word_pos == 1) n = (uint8_t)(n == 0 ? SEED_WORDS_N-1 : n-1);
                else                    p = (uint8_t)(p == 0 ? SEED_WORDS_N-1 : p-1);
            }
            prev_j = j;
        }
        frame_counter++;
        wait_vbl_done();
        ui_title_menu_anim_tick(frame_counter);
    }
}

uint16_t title_screen(uint16_t entropy_hint) BANKED { // blocking until START or SELECT path returns a seed
    uint8_t  blink_counter = 0, blink_visible = 1, prev_j = 0;
    uint16_t frame_counter = 0;

    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("UI_title");
    ui_title_style_begin(7u, UI_TITLE_TORCH_PAD_L_TITLE);
    ui_title_menu_border_draw();
    title_logo_bkg_vram_patch();
    ui_title_logo_draw_bkg(4u, 3u); // 12×4 scaled; centered (x=4), one row up vs (4,4)
    gotoxy(7, 7); printf("Abyss"); // centered; 8px below logo (map row below 4-row block: 3+4=7)
    gotoxy(4, 10); printf("Start: Quest"); // 12 chars — (20−12)/2; shown before blink loop
    gotoxy(4, 12); printf("Select: Seed"); // blank row 11 under Start: Quest

    while (1) {
        uint8_t  j    = joypad();
        uint8_t  edge = (uint8_t)(j & (uint8_t)~prev_j);
        uint16_t mixed = seed_entropy_random_seed(entropy_hint, frame_counter); // fresh candidate each frame for START
        if (edge & J_START) {
            ui_title_style_end();
            return mixed;
        }
        if (edge & J_SELECT) {
            ui_title_style_end();
            uint16_t seed = input_seed_words_screen(mixed, entropy_hint); // user-defined mnemonic seed
            if (!seed) seed = 1;
            return seed;
        }
        blink_counter++;
        if (blink_counter >= 30) { // ~0.5s at 60Hz VBlank loop
            blink_counter = 0; blink_visible ^= 1;
            gotoxy(4, 10);
            if (blink_visible) printf("Start: Quest");
            else               printf("            "); // 12 spaces
        }
        frame_counter++;
        wait_vbl_done();
        ui_title_menu_anim_tick(frame_counter);
        prev_j = j;
    }
}

