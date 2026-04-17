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

#define CLASS_MENU_EMBLEM_X 12u // one 2×2 on the right; text stays left
#define CLASS_MENU_EMBLEM_Y  7u

/* One emblem 2×2: ROM indices base, base+1, base+16, base+17 (e.g. Knight A15 B15 / A16 B16). Boot only loads ROM 0–127 → VRAM; emblems are 224+ so copy from banked tileset_tiles into scratch VRAM. */
static void class_emblem_draw(uint8_t sel) {
    static const uint8_t tl[4] = {
        (uint8_t)TILE_EMBLEM_KNIGHT_TL,
        (uint8_t)TILE_EMBLEM_SCOUNDREL_TL,
        (uint8_t)TILE_EMBLEM_WITCH_TL,
        (uint8_t)TILE_EMBLEM_ZERKER_TL,
    };
    uint8_t base = tl[(unsigned)sel % PLAYER_CLASS_COUNT];
    uint8_t pack[64]; // 4×16-byte tiles in map order: TL, TR, BL, BR
    uint8_t buf[4];
    uint8_t i, j;
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    memcpy(pack + 0u, tileset_tiles + (uint16_t)base * 16u, 16u);
    memcpy(pack + 16u, tileset_tiles + (uint16_t)(uint8_t)(base + 1u) * 16u, 16u);
    memcpy(pack + 32u, tileset_tiles + (uint16_t)(uint8_t)(base + 16u) * 16u, 16u);
    memcpy(pack + 48u, tileset_tiles + (uint16_t)(uint8_t)(base + 17u) * 16u, 16u);
    SWITCH_ROM(sb);
    set_bkg_data(CLASS_EMBLEM_VRAM_START, 4u, pack);
    buf[0] = CLASS_EMBLEM_VRAM_START;
    buf[1] = (uint8_t)(CLASS_EMBLEM_VRAM_START + 1u);
    buf[2] = (uint8_t)(CLASS_EMBLEM_VRAM_START + 2u);
    buf[3] = (uint8_t)(CLASS_EMBLEM_VRAM_START + 3u);
    set_bkg_tiles(CLASS_MENU_EMBLEM_X, CLASS_MENU_EMBLEM_Y, 2u, 2u, buf);
    VBK_REG = VBK_ATTRIBUTES;
    for (j = 0u; j < 2u; j++)
        for (i = 0u; i < 2u; i++)
            set_bkg_attribute_xy((uint8_t)(CLASS_MENU_EMBLEM_X + i), (uint8_t)(CLASS_MENU_EMBLEM_Y + j), 0u);
    VBK_REG = VBK_TILES;
}

static void class_emblem_vram_restore(void) { // put back ROM 124–127 tiles overwritten by scratch slots
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    set_bkg_data(CLASS_EMBLEM_VRAM_START, 4u, tileset_tiles + (uint16_t)CLASS_EMBLEM_VRAM_ROM_RESTORE * 16u);
    SWITCH_ROM(sb);
}

BANKREF(state_char_create_enter)
void state_char_create_enter(void) BANKED {
    uint8_t prev_j = 0, sel = 0, prev_sel = 255u;
    BANK_DBG("CC_enter");
    lcd_gameplay_active = 0u;
    window_ui_hide();
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
