#pragma bank 1

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include "defs.h"
#include "tileset.h"
#include "class_palettes.h"
#include <gb/cgb.h>
#include <gb/gb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/platform.h>

#define CLASS_MENU_EMBLEM_X 14u // shifted right by 16px (2 tiles)
#define CLASS_MENU_EMBLEM_Y  7u
#define CLASS_EMBLEM_VRAM_SCALE2_START 176u
#define CLASS_EMBLEM_VRAM_SCALE2_COUNT 16u
#define CLASS_EMBLEM_VRAM_SCALE2_ROM_RESTORE 48u

static uint8_t tile2bpp_get_px(const uint8_t *tile, uint8_t x, uint8_t y) {
    uint8_t m = (uint8_t)(0x80u >> x);
    uint8_t lo = (tile[(uint8_t)(y * 2u)] & m) ? 1u : 0u;
    uint8_t hi = (tile[(uint8_t)(y * 2u + 1u)] & m) ? 2u : 0u;
    return (uint8_t)(lo | hi);
}

static void tile2bpp_set_px(uint8_t *tile, uint8_t x, uint8_t y, uint8_t c) {
    uint8_t m = (uint8_t)(0x80u >> x);
    uint8_t lo_i = (uint8_t)(y * 2u);
    uint8_t hi_i = (uint8_t)(lo_i + 1u);
    if (c & 1u) tile[lo_i] |= m; else tile[lo_i] &= (uint8_t)~m;
    if (c & 2u) tile[hi_i] |= m; else tile[hi_i] &= (uint8_t)~m;
}

/* One emblem 2×2: ROM indices base, base+1, base+16, base+17 (e.g. Knight A15 B15 / A16 B16). Boot only loads ROM 0–127 → VRAM; emblems are 224+ so copy from banked tileset_tiles into scratch VRAM. */
static void class_emblem_draw(uint8_t sel) {
    static const uint8_t tl[4] = {
        (uint8_t)TILE_EMBLEM_KNIGHT_TL,
        (uint8_t)TILE_EMBLEM_SCOUNDREL_TL,
        (uint8_t)TILE_EMBLEM_WITCH_TL,
        (uint8_t)TILE_EMBLEM_ZERKER_TL,
    };
    uint8_t base = tl[(unsigned)sel % PLAYER_CLASS_COUNT];
    uint8_t pack[64]; // 4×16-byte source tiles in map order: TL, TR, BL, BR
    uint8_t out[256]; // 16×16-byte tiles: true 2× nearest-neighbor scale (16×16 -> 32×32)
    uint8_t buf[16];
    uint8_t i, j, ox, oy;
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    memcpy(pack + 0u, tileset_tiles + (uint16_t)base * 16u, 16u);
    memcpy(pack + 16u, tileset_tiles + (uint16_t)(uint8_t)(base + 1u) * 16u, 16u);
    memcpy(pack + 32u, tileset_tiles + (uint16_t)(uint8_t)(base + 16u) * 16u, 16u);
    memcpy(pack + 48u, tileset_tiles + (uint16_t)(uint8_t)(base + 17u) * 16u, 16u);
    SWITCH_ROM(sb);
    memset(out, 0, sizeof(out));
    for (oy = 0u; oy < 32u; oy++) {
        for (ox = 0u; ox < 32u; ox++) {
            uint8_t sx = (uint8_t)(ox >> 1u);
            uint8_t sy = (uint8_t)(oy >> 1u);
            uint8_t qx = (sx >= 8u) ? 1u : 0u;
            uint8_t qy = (sy >= 8u) ? 1u : 0u;
            uint8_t src_id = (uint8_t)(qy * 2u + qx);
            uint8_t src_x = (uint8_t)(sx & 7u);
            uint8_t src_y = (uint8_t)(sy & 7u);
            uint8_t c = tile2bpp_get_px(pack + (uint16_t)src_id * 16u, src_x, src_y);
            uint8_t tx = (uint8_t)(ox >> 3u);
            uint8_t ty = (uint8_t)(oy >> 3u); // 4x4 tile grid
            uint8_t tid = (uint8_t)(ty * 4u + tx);
            uint8_t dx = (uint8_t)(ox & 7u);
            uint8_t dy = (uint8_t)(oy & 7u);
            tile2bpp_set_px(out + (uint16_t)tid * 16u, dx, dy, c);
        }
    }
    set_bkg_data(CLASS_EMBLEM_VRAM_SCALE2_START, CLASS_EMBLEM_VRAM_SCALE2_COUNT, out);
    for (j = 0u; j < 4u; j++) {
        for (i = 0u; i < 4u; i++) {
            buf[(uint8_t)(j * 4u + i)] = (uint8_t)(CLASS_EMBLEM_VRAM_SCALE2_START + (uint8_t)(j * 4u + i));
        }
    }
    set_bkg_tiles(CLASS_MENU_EMBLEM_X, CLASS_MENU_EMBLEM_Y, 4u, 4u, buf);
    VBK_REG = VBK_ATTRIBUTES;
    {
        uint8_t pal = PAL_CLASS_EMBLEM_KNIGHT;
        if (sel == 1u) pal = PAL_CLASS_EMBLEM_SCOUNDREL;
        else if (sel == 2u) pal = PAL_CLASS_EMBLEM_WITCH;
        else if (sel == 3u) pal = PAL_CLASS_EMBLEM_ZERKER;
    for (j = 0u; j < 4u; j++)
        for (i = 0u; i < 4u; i++)
            set_bkg_attribute_xy((uint8_t)(CLASS_MENU_EMBLEM_X + i), (uint8_t)(CLASS_MENU_EMBLEM_Y + j), pal);
    }
    VBK_REG = VBK_TILES;
}

static void class_emblem_vram_restore(void) { // restore ROM tiles for 64-tile class-menu scratch region
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    set_bkg_data(CLASS_EMBLEM_VRAM_SCALE2_START, CLASS_EMBLEM_VRAM_SCALE2_COUNT, tileset_tiles + (uint16_t)CLASS_EMBLEM_VRAM_SCALE2_ROM_RESTORE * 16u);
    SWITCH_ROM(sb);
}

BANKREF(state_char_create_enter)
void state_char_create_enter(void) BANKED {
    uint8_t prev_j = 0, sel = 0, prev_sel = 255u;
    BANK_DBG("CC_enter");
    lcd_gameplay_active = 0u;
    window_ui_hide();
    class_palettes_bkg_emblem_init();
    wait_vbl_done();
    lcd_clear_display();
    for (;;) {
        gotoxy(1, 4); printf("CHOOSE CLASS");
        gotoxy(0, 7); printf(sel == 0 ? ">" : " ");
        gotoxy(2, 7); printf("KNIGHT");
        gotoxy(0, 9); printf(sel == 1 ? ">" : " ");
        gotoxy(2, 9); printf("SCOUNDREL");
        gotoxy(0, 11); printf(sel == 2 ? ">" : " ");
        gotoxy(2, 11); printf("WITCH");
        gotoxy(0, 13); printf(sel == 3 ? ">" : " ");
        gotoxy(2, 13); printf("ZERKER");
        gotoxy(0, 16); printf("A=confirm");
        if (sel != prev_sel) {
            class_emblem_draw(sel);
            prev_sel = sel;
        }
        {
            uint8_t j = joypad();
            uint8_t e = (uint8_t)(j & (uint8_t)~prev_j);
            if (e & J_UP)   sel = (uint8_t)((sel + PLAYER_CLASS_COUNT - 1u) % PLAYER_CLASS_COUNT);
            if (e & J_DOWN) sel = (uint8_t)((sel + 1u) % PLAYER_CLASS_COUNT);
            if (e & J_A) {
                player_class = sel;
                class_emblem_vram_restore();
                class_palettes_sprite_player_apply(); // BANKED far-call — do not SWITCH_ROM here (return must see bank 1 code)
                break;
            }
            prev_j = j;
        }
        wait_vbl_done();
    }
    g_prev_j = 0;
    next_state = STATE_GAMEPLAY;
}
