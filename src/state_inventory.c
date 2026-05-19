#pragma bank 3

#include "debug_bank.h"
#include "entity_sprites.h"
#include "game_state.h"
#include "globals.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

BANKREF_EXTERN(entity_sprites_inv_cursor_show)
BANKREF_EXTERN(entity_sprites_inv_cursor_hide)

#define INV_GRID_X     0u // BG col origin of the 5x6 cell grid
#define INV_GRID_Y     1u
#define INV_GRID_COLS  5u
#define INV_GRID_ROWS  6u
#define INV_CELL_DX    2u // 1-tile icon + 1-tile quantity slot; cursor is a sprite below the icon
#define INV_CELL_DY    2u
#define INV_NAME_ROW  14u
#define INV_NAME_LEN  16u

static uint8_t inv_prev_j;
static uint8_t inv_cursor; // 0..29

static void cell_origin(uint8_t slot, uint8_t *cx, uint8_t *cy) {
    *cx = (uint8_t)(INV_GRID_X + (slot % INV_GRID_COLS) * INV_CELL_DX);
    *cy = (uint8_t)(INV_GRID_Y + (slot / INV_GRID_COLS) * INV_CELL_DY);
}

static void draw_cell(uint8_t slot) {
    uint8_t cx, cy, v, pal;
    uint8_t kind = inventory_kind[slot];
    cell_origin(slot, &cx, &cy);
    if (kind == ITEM_KIND_NONE) {
        v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY);
        pal = PAL_UI;
    } else {
        v = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(kind));
        pal = items_kind_palette(kind);
    }
    set_bkg_tiles(cx, cy, 1, 1, &v);
    set_bkg_attribute_xy(cx, cy, pal);
    VBK_REG = VBK_TILES;
}

static void draw_grid(void) {
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) draw_cell(i);
}

static void draw_cursor_and_name(void) {
    uint8_t cx, cy;
    char name[18];
    cell_origin(inv_cursor, &cx, &cy);
    entity_sprites_inv_cursor_show(cx, cy);
    gotoxy(2, INV_NAME_ROW);
    {
        uint8_t pad;
        for (pad = 0u; pad < INV_NAME_LEN; pad++) setchar(' ');
    }
    if (inventory_kind[inv_cursor] != ITEM_KIND_NONE) {
        items_kind_name_copy(inventory_kind[inv_cursor], name, sizeof name);
        gotoxy(2, INV_NAME_ROW); printf("%s", name);
    } else {
        gotoxy(2, INV_NAME_ROW); printf("(empty)");
    }
}

static void draw_menu_tabs_inv(void) {
    gotoxy(0, 0); printf(">ITEM STAT SPEL MAP ");
}

BANKREF(state_inventory_enter)
void state_inventory_enter(void) BANKED {
    BANK_DBG("IV_enter");
    inv_prev_j = joypad();
    inv_cursor = 0u;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    draw_menu_tabs_inv();
    draw_grid();
    draw_cursor_and_name();
    gotoxy(1, 16); printf("D-pad pick");
    gotoxy(1, 17); printf("START resume");
}

BANKREF(state_inventory_tick)
void state_inventory_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~inv_prev_j);
    if (e & J_START)  { entity_sprites_inv_cursor_hide(); next_state = STATE_GAMEPLAY; goto out; }
    if (e & J_SELECT) { entity_sprites_inv_cursor_hide(); next_state = STATE_STATS;    goto out; }
    {
        uint8_t old = inv_cursor;
        if ((e & J_LEFT)  && (inv_cursor % INV_GRID_COLS) > 0u) inv_cursor--;
        if ((e & J_RIGHT) && (inv_cursor % INV_GRID_COLS) < (uint8_t)(INV_GRID_COLS - 1u)) inv_cursor++;
        if ((e & J_UP)    && inv_cursor >= INV_GRID_COLS) inv_cursor = (uint8_t)(inv_cursor - INV_GRID_COLS);
        if ((e & J_DOWN)  && (uint8_t)(inv_cursor + INV_GRID_COLS) < INVENTORY_MAX_SLOTS) inv_cursor = (uint8_t)(inv_cursor + INV_GRID_COLS);
        if (inv_cursor != old) {
            wait_vbl_done();
            draw_cursor_and_name();
        }
    }
out:
    inv_prev_j = j;
    wait_vbl_done();
}
