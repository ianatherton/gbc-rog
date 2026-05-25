#pragma bank 3

#include "debug_bank.h"
#include "entity_sprites.h"
#include "game_state.h"
#include "globals.h" // inv_desc_scx
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
#define INV_DESC_ROW  15u
#define INV_DESC_W    20u // visible character columns (screen width in tiles)
#define INV_DESC_DRAW_W 21u // one extra tile to cover the sub-pixel peek on the right

#define INV_MODE_GRID 0u
#define INV_MODE_DROP 1u

#define MAX_EQUIP_MARKS 8u // OAM slots SP_ENEMY_BASE .. SP_ENEMY_BASE+7; swept by entity_sprites on gameplay re-entry

#define EQUIP_PANEL_X  10u
#define EQUIP_PANEL_Y   1u

static uint8_t inv_prev_j;
static uint8_t inv_cursor; // 0..29
static uint8_t inv_mode;

static char    desc_buf[48];   // description text
static uint8_t desc_base_len;  // strlen(desc_buf)
static uint8_t desc_total_len; // desc_base_len + INV_DESC_W (virtual: [text][20 spaces])
static uint8_t desc_off;       // tile (character) scroll position into virtual buffer
static uint8_t desc_pix;       // sub-tile pixel offset 0-7; drives inv_desc_scx for the ISR

static void cell_origin(uint8_t slot, uint8_t *cx, uint8_t *cy) {
    *cx = (uint8_t)(INV_GRID_X + (slot % INV_GRID_COLS) * INV_CELL_DX);
    *cy = (uint8_t)(INV_GRID_Y + (slot / INV_GRID_COLS) * INV_CELL_DY);
}

static void draw_cell(uint8_t slot) {
    uint8_t cx, cy, v, pal;
    uint8_t kind  = inventory_kind[slot];
    uint8_t count = inventory_count[slot];
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
    /* quantity digit at cx+1 */
    gotoxy((uint8_t)(cx + 1u), cy);
    if (kind == ITEM_KIND_NONE || count <= 1u) setchar(' ');
    else if (count <= 9u)                      setchar((char)('0' + count));
    else                                       setchar('*');
    set_bkg_attribute_xy((uint8_t)(cx + 1u), cy, PAL_UI);
    VBK_REG = VBK_TILES;
}

static void draw_grid(void) {
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) draw_cell(i);
}

static uint8_t is_kind_equipped(uint8_t kind) {
    uint8_t i;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
        if (inventory_kind[i] == kind && inventory_equipped[i]) return 1u;
    }
    return 0u;
}

static void draw_equip_slot_tile(uint8_t x, uint8_t y, uint8_t kind) {
    uint8_t v, pal;
    if (kind != ITEM_KIND_NONE && is_kind_equipped(kind)) {
        v   = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(kind));
        pal = items_kind_palette(kind);
    } else {
        v   = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY);
        pal = PAL_UI;
    }
    set_bkg_tiles(x, y, 1u, 1u, &v);
    set_bkg_attribute_xy(x, y, pal);
    VBK_REG = VBK_TILES;
}

static void draw_equip_panel(void) {
    /* Row 1: Head | Body */
    gotoxy(EQUIP_PANEL_X, EQUIP_PANEL_Y);
    printf("Head");
    draw_equip_slot_tile(14u, EQUIP_PANEL_Y, ITEM_KIND_HELMET);
    gotoxy(15u, EQUIP_PANEL_Y);
    printf("Body");
    draw_equip_slot_tile(19u, EQUIP_PANEL_Y, ITEM_KIND_TUNIC);
    /* Row 3: Feet | Ring */
    gotoxy(EQUIP_PANEL_X, (uint8_t)(EQUIP_PANEL_Y + 2u));
    printf("Feet");
    draw_equip_slot_tile(14u, (uint8_t)(EQUIP_PANEL_Y + 2u), ITEM_KIND_BOOTS);
    gotoxy(15u, (uint8_t)(EQUIP_PANEL_Y + 2u));
    printf("Ring");
    draw_equip_slot_tile(19u, (uint8_t)(EQUIP_PANEL_Y + 2u), ITEM_KIND_NONE);
    /* Row 5: Primary */
    gotoxy(EQUIP_PANEL_X, (uint8_t)(EQUIP_PANEL_Y + 4u));
    printf("Primary__");
    draw_equip_slot_tile(19u, (uint8_t)(EQUIP_PANEL_Y + 4u), ITEM_KIND_RUSTY_SWORD);
    /* Row 7: Secondary */
    gotoxy(EQUIP_PANEL_X, (uint8_t)(EQUIP_PANEL_Y + 6u));
    printf("Secondary");
    draw_equip_slot_tile(19u, (uint8_t)(EQUIP_PANEL_Y + 6u), ITEM_KIND_NONE);
}

static void draw_equipped_marks(void) {
    uint8_t slot, cx, cy;
    uint8_t sp = (uint8_t)SP_ENEMY_BASE;
    for (slot = 0u; slot < INVENTORY_MAX_SLOTS && sp < (uint8_t)(SP_ENEMY_BASE + MAX_EQUIP_MARKS); slot++) {
        if (inventory_kind[slot] == ITEM_KIND_NONE || !inventory_equipped[slot]) continue;
        cell_origin(slot, &cx, &cy);
        set_sprite_tile(sp, TILE_EQUIP_MARK_VRAM);
        set_sprite_prop(sp, 0u);
        move_sprite(sp,
                    (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (uint16_t)cx * 8u),
                    (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (uint16_t)cy * 8u));
        sp++;
    }
    for (; sp < (uint8_t)(SP_ENEMY_BASE + MAX_EQUIP_MARKS); sp++)
        move_sprite(sp, 0u, 0u);
}

static void draw_desc_row(void) {
    uint8_t i;
    gotoxy(0, INV_DESC_ROW);
    // Write INV_DESC_DRAW_W (21) chars: the extra tile at col 20 fills the pixel gap
    // revealed by SCX_REG when desc_pix > 0.
    for (i = 0u; i < INV_DESC_DRAW_W; i++) {
        uint8_t pos = (uint8_t)(desc_off + i);
        if (pos >= desc_total_len) pos = (uint8_t)(pos - desc_total_len);
        putchar((pos < desc_base_len) ? desc_buf[pos] : ' ');
    }
}

static void reset_desc_ticker(uint8_t kind) {
    uint8_t i = 0u;
    if (kind != ITEM_KIND_NONE) {
        items_kind_desc_copy(kind, desc_buf, (uint8_t)sizeof desc_buf);
        while (desc_buf[i]) i++;
    } else {
        desc_buf[0] = 0;
    }
    desc_base_len  = i;
    desc_total_len = (uint8_t)(i + INV_DESC_W); // [desc text][20 trailing spaces]
    desc_off  = i;   // start at first trailing space → viewport is all spaces (text off right)
    desc_pix  = 0u;
    inv_desc_scx = 0u;
    draw_desc_row(); // immediately clears row to spaces
}

static void draw_cursor_and_name(void) {
    uint8_t cx, cy;
    char name[INV_NAME_LEN + 1u];
    cell_origin(inv_cursor, &cx, &cy);
    entity_sprites_inv_cursor_show(cx, cy);
    // fill with spaces then overwrite — printf advances cursor, clearing leftovers
    gotoxy(2, INV_NAME_ROW);
    printf("                "); // INV_NAME_LEN spaces
    if (inventory_kind[inv_cursor] != ITEM_KIND_NONE) {
        items_kind_name_copy(inventory_kind[inv_cursor], name, (uint8_t)sizeof name);
        gotoxy(2, INV_NAME_ROW); printf("%s", name);
    } else {
        gotoxy(2, INV_NAME_ROW); printf("(empty)");
    }
    reset_desc_ticker(inventory_kind[inv_cursor]);
}

static void draw_menu_tabs_inv(void) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
    gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
    set_bkg_tiles(0u, 0u, 1u, 1u, &v);
    set_bkg_attribute_xy(0u, 0u, PAL_XP_UI);
    VBK_REG = VBK_TILES;
}

static void draw_grid_screen(void) {
    entity_sprites_inv_cursor_hide();
    lcd_clear_display();
    draw_menu_tabs_inv();
    draw_grid();
    draw_equipped_marks();
    draw_equip_panel();
    draw_cursor_and_name();
    gotoxy(1, 16); printf("A:equip  B:drop");
    gotoxy(1, 17); printf("START resume");
}

static void draw_drop_confirm(void) {
    uint8_t kind = inventory_kind[inv_cursor];
    uint8_t v;
    char namebuf[18];
    items_kind_name_copy(kind, namebuf, sizeof namebuf);
    entity_sprites_inv_cursor_hide();
    lcd_clear_display();
    gotoxy(3, 4); printf("Item drop?");
    v = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(kind));
    set_bkg_tiles(3u, 6u, 1u, 1u, &v);
    set_bkg_attribute_xy(3u, 6u, items_kind_palette(kind));
    VBK_REG = VBK_TILES;
    gotoxy(5, 6); printf("%s", namebuf);
    gotoxy(3, 9); printf("A:Yes  B:Cancel");
}

BANKREF(state_inventory_enter)
void state_inventory_enter(void) BANKED {
    BANK_DBG("IV_enter");
    inv_prev_j = joypad();
    inv_cursor = 0u;
    inv_mode   = INV_MODE_GRID;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    draw_grid_screen();
}

BANKREF(state_inventory_tick)
void state_inventory_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~inv_prev_j);

    if (inv_mode == INV_MODE_GRID && desc_base_len > 0u) {
        if (++desc_pix >= 8u) {
            desc_pix = 0u;
            if (++desc_off >= desc_total_len) desc_off = 0u;
            draw_desc_row();
        }
        inv_desc_scx = desc_pix;
    }

    if (inv_mode == INV_MODE_DROP) {
        if (e & J_A) {
            inventory_remove(inv_cursor);
            inv_mode = INV_MODE_GRID;
            wait_vbl_done();
            draw_grid_screen();
        } else if (e & J_B) {
            inv_mode = INV_MODE_GRID;
            wait_vbl_done();
            draw_grid_screen();
        }
        goto out;
    }

    if (e & J_START)  { inv_desc_scx = 0u; entity_sprites_inv_cursor_hide(); next_state = STATE_GAMEPLAY; goto out; }
    if (e & J_SELECT) { inv_desc_scx = 0u; entity_sprites_inv_cursor_hide(); next_state = STATE_STATS;    goto out; }

    if (e & J_A) {
        uint8_t kind = inventory_kind[inv_cursor];
        if (kind != ITEM_KIND_NONE && items_kind_category(kind) == ITEM_CAT_EQUIPMENT) {
            if (!inventory_equipped[inv_cursor]) {
                uint8_t i;
                for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
                    if (i != inv_cursor && inventory_kind[i] == kind && inventory_equipped[i]) {
                        inventory_equipped[i] = 0u;
                        items_equip_apply(kind, 0u);
                    }
                }
            }
            inventory_equipped[inv_cursor] ^= 1u;
            items_equip_apply(kind, inventory_equipped[inv_cursor]);
            wait_vbl_done();
            draw_equipped_marks();
            draw_equip_panel();
        }
        goto out;
    }

    if ((e & J_B) && inventory_kind[inv_cursor] != ITEM_KIND_NONE) {
        inv_desc_scx = 0u;
        inv_mode = INV_MODE_DROP;
        wait_vbl_done();
        draw_drop_confirm();
        goto out;
    }

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
