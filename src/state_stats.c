#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

#define STATS_ROW_BASE   4u
#define STATS_ROW_COUNT  6u  // HP DMG CRIT DODGE ARMOR MAGDEF — light is not assignable
#define STATS_HINT_ROW  13u

static uint8_t stats_prev_j;
static uint8_t stats_cursor; // 0..STATS_ROW_COUNT-1

static void stats_draw_row(uint8_t i) {
    gotoxy(1, (uint8_t)(STATS_ROW_BASE + i));
    switch (i) {
    case 0u: printf("HP     %u/%u", (unsigned)player_hp, (unsigned)player_hp_max); break;
    case 1u: printf("DMG    %u",    (unsigned)player_damage);      break;
    case 2u: printf("CRIT   %u%%",  (unsigned)player_crit_chance); break;
    case 3u: printf("DODGE  %u%%",  (unsigned)player_dodge);       break;
    case 4u: printf("ARMOR  %u%%",  (unsigned)player_armor);       break;
    default: printf("MAGDEF %u%%",  (unsigned)player_magdef);      break;
    }
}

static void stats_draw_pts(void) {
    gotoxy(13, 2); printf("       ");
    if (player_stat_points) { gotoxy(13, 2); printf("PTS %u", (unsigned)player_stat_points); }
}

static void stats_cursor_draw(void) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
    uint8_t row = (uint8_t)(STATS_ROW_BASE + stats_cursor);
    set_bkg_tiles(0u, row, 1u, 1u, &v);
    set_bkg_attribute_xy(0u, row, PAL_XP_UI_BG);
    VBK_REG = VBK_TILES;
}

static void stats_cursor_clear(void) {
    uint8_t row = (uint8_t)(STATS_ROW_BASE + stats_cursor);
    gotoxy(0, row); putchar(' ');
    set_bkg_attribute_xy(0u, row, 0u);
    VBK_REG = VBK_TILES;
}

/* Spend one point on stat i. Returns 1 if it applied (capped stats refuse). */
static uint8_t stats_spend_point(uint8_t i) {
    switch (i) {
    case 0u:
        if (player_hp_max >= 255u) return 0u;
        player_hp_max = (uint8_t)((player_hp_max <= 245u) ? player_hp_max + 10u : 255u);
        player_hp     = (uint8_t)((player_hp     <= 245u) ? player_hp     + 10u : 255u);
        if (player_hp > player_hp_max) player_hp = player_hp_max;
        return 1u;
    case 1u:
        if (player_damage >= 255u) return 0u;
        player_damage = (uint8_t)((player_damage <= 252u) ? player_damage + 3u : 255u);
        return 1u;
    case 2u: if (player_crit_chance >= 100u) return 0u; player_crit_chance++; return 1u;
    case 3u: if (player_dodge       >= 100u) return 0u; player_dodge++;       return 1u;
    case 4u: if (player_armor       >= 100u) return 0u; player_armor++;       return 1u;
    default: if (player_magdef      >= 100u) return 0u; player_magdef++;      return 1u;
    }
}

BANKREF(state_stats_enter)
void state_stats_enter(void) BANKED {
    uint8_t i;
    BANK_DBG("ST_enter");
    stats_prev_j = joypad();
    stats_cursor = 0u;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("ST_draw");
    {
        uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
        uint8_t x;
        gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
        set_bkg_tiles(5u, 0u, 1u, 1u, &v);
        set_bkg_attribute_xy(5u, 0u, PAL_XP_UI_BG);
        for (x = 1u;  x <= 4u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // ITEM
        for (x = 11u; x <= 15u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // SPELL
        for (x = 17u; x <= 19u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // MAP
        VBK_REG = VBK_TILES;
        if (player_stat_points) { // unspent level-up points reminder
            gotoxy(10, 0); putchar('+');
            set_bkg_attribute_xy(10u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
        if (player_spell_points) { // unspent spell points, right after SPELL
            gotoxy(16, 0); putchar('+');
            set_bkg_attribute_xy(16u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
    }
    gotoxy(1, 2); printf("STATS");
    stats_draw_pts();
    for (i = 0u; i < STATS_ROW_COUNT; i++) stats_draw_row(i);
    {
        uint16_t next_xp = (uint16_t)PLAYER_LEVEL_XP_BASE
                         + (uint16_t)(player_level - 1u) * (uint16_t)PLAYER_LEVEL_XP_STEP;
        uint16_t xp_rem  = (player_xp < next_xp) ? (uint16_t)(next_xp - player_xp) : 0u;
        gotoxy(1, 11); printf("LV %u  LIGHT %u", (unsigned)player_level, (unsigned)player_light_radius());
        gotoxy(1, 12); printf("EXP %u  NXT %u", (unsigned)player_xp, (unsigned)xp_rem);
    }
    gotoxy(1, STATS_HINT_ROW); printf("START resume");
    if (player_stat_points) {
        gotoxy(15, STATS_HINT_ROW); printf("A:add");
        stats_cursor_draw();
    }
}

BANKREF(state_stats_tick)
void state_stats_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~stats_prev_j);
    if (player_stat_points) {
        if (e & (J_UP | J_DOWN)) {
            stats_cursor_clear();
            if (e & J_UP) stats_cursor = (uint8_t)(stats_cursor ? stats_cursor - 1u : STATS_ROW_COUNT - 1u);
            else          stats_cursor = (uint8_t)((stats_cursor + 1u) % STATS_ROW_COUNT);
            stats_cursor_draw();
        }
        if ((e & J_A) && stats_spend_point(stats_cursor)) {
            player_stat_points--;
            stats_draw_row(stats_cursor);
            stats_draw_pts();
            if (!player_stat_points) { // all spent — retire the allocation UI
                stats_cursor_clear();
                gotoxy(15, STATS_HINT_ROW); printf("     ");
                gotoxy(10, 0); putchar(' '); // tab-bar '+'
                set_bkg_attribute_xy(10u, 0u, 0u); // spacer cols stay attr 0 in the tab bar
                VBK_REG = VBK_TILES;
            }
        }
    }
    if (e & J_START)  next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_ABILITY;
    stats_prev_j = j;
    wait_vbl_done();
}
