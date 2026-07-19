#pragma bank 3

#include "debug_bank.h"
#include "entity_sprites.h"
#include "game_state.h"
#include "globals.h" // inv_desc_scx
#include "equipment.h"
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
#define INV_MODE_BELT_PICK 2u

#define MAX_EQUIP_MARKS 8u // OAM slots SP_ENEMY_BASE .. SP_ENEMY_BASE+7; swept by entity_sprites on gameplay re-entry

#define EQUIP_PANEL_X  10u
#define EQUIP_PANEL_Y   1u

static void put_stat_uint(uint8_t x, uint8_t y, uint8_t v, uint8_t width) {
    uint8_t i = 0u, pad;
    char dig[3];
    if (!v) { dig[i++] = '0'; }
    else { while (v) { dig[i++] = (char)('0' + v % 10u); v /= 10u; } }
    gotoxy(x, y);
    for (pad = i; pad < width; pad++) putchar(' ');
    while (i) putchar(dig[--i]);
}

static uint8_t inv_prev_j;
static uint8_t inv_cursor; // 0..29
static uint8_t inv_mode;
static uint8_t inv_swap_src;   // slot being moved onto the belt (INV_MODE_BELT_PICK)
static uint8_t belt_pick_idx;  // 0..BELT_ITEM_SLOT_COUNT-1 candidate belt slot

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

/* "BELT" spelled in the spacer row beneath the first BELT_ITEM_SLOT_COUNT slots
   (slots 0..3 are the gameplay quick-use belt). Top-row spacer is BG row INV_GRID_Y+1. */
static void draw_belt_label(void) {
    static const char letters[BELT_ITEM_SLOT_COUNT] = { 'B', 'E', 'L', 'T' };
    uint8_t s, x, y = (uint8_t)(INV_GRID_Y + 1u);
    for (s = 0u; s < BELT_ITEM_SLOT_COUNT; s++) {
        x = (uint8_t)(INV_GRID_X + s * INV_CELL_DX);
        gotoxy(x, y);
        setchar(letters[s]);
        set_bkg_attribute_xy(x, y, PAL_UI);
        VBK_REG = VBK_TILES;
    }
}

/* True when the cursor item can be sent to the belt: a usable (non-equipment) item
   that is not already sitting in a belt slot. */
static uint8_t cursor_can_send_to_belt(void) {
    uint8_t kind = inventory_kind[inv_cursor];
    return (kind != ITEM_KIND_NONE &&
            items_kind_category(kind) != ITEM_CAT_EQUIPMENT &&
            inv_cursor >= BELT_ITEM_SLOT_COUNT) ? 1u : 0u;
}

/* Exchange both items across all four parallel inventory arrays. */
static void swap_slots(uint8_t a, uint8_t b) {
    uint8_t tk = inventory_kind[a], te = inventory_equipped[a], tc = inventory_count[a];
    int8_t  tm = inventory_mod_level[a];
    inventory_kind[a]      = inventory_kind[b];
    inventory_equipped[a]  = inventory_equipped[b];
    inventory_count[a]     = inventory_count[b];
    inventory_mod_level[a] = inventory_mod_level[b];
    inventory_kind[b]      = tk;
    inventory_equipped[b]  = te;
    inventory_count[b]     = tc;
    inventory_mod_level[b] = tm;
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
    /* Row 0: Head | Hand (primary) */
    gotoxy(EQUIP_PANEL_X, EQUIP_PANEL_Y);
    printf("Head");
    draw_equip_slot_tile(14u, EQUIP_PANEL_Y, ITEM_KIND_HELMET);
    gotoxy(15u, EQUIP_PANEL_Y);
    printf("Hand");
    draw_equip_slot_tile(19u, EQUIP_PANEL_Y, equipped_kind_in_slot(EQUIP_SLOT_WEAPON));
    /* Row 1: Body | Hand (off-hand) */
    gotoxy(EQUIP_PANEL_X, (uint8_t)(EQUIP_PANEL_Y + 1u));
    printf("Body");
    draw_equip_slot_tile(14u, (uint8_t)(EQUIP_PANEL_Y + 1u), ITEM_KIND_TUNIC);
    gotoxy(15u, (uint8_t)(EQUIP_PANEL_Y + 1u));
    printf("Hand");
    draw_equip_slot_tile(19u, (uint8_t)(EQUIP_PANEL_Y + 1u), ITEM_KIND_SHIELD);
    /* Row 2: Feet | Ring */
    gotoxy(EQUIP_PANEL_X, (uint8_t)(EQUIP_PANEL_Y + 2u));
    printf("Feet");
    draw_equip_slot_tile(14u, (uint8_t)(EQUIP_PANEL_Y + 2u), ITEM_KIND_BOOTS);
    gotoxy(15u, (uint8_t)(EQUIP_PANEL_Y + 2u));
    printf("Ring");
    draw_equip_slot_tile(19u, (uint8_t)(EQUIP_PANEL_Y + 2u), equipped_kind_in_slot(EQUIP_SLOT_RING));
}

static void draw_stats_panel(void) {
    uint8_t v;
    uint8_t y = (uint8_t)(EQUIP_PANEL_Y + 4u); /* blank line at EQUIP_PANEL_Y+3; stats start at +4 */

    /* HP row: heart tile + :cur/max */
    v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_HEART_FULL);
    set_bkg_tiles(EQUIP_PANEL_X, y, 1u, 1u, &v);
    set_bkg_attribute_xy(EQUIP_PANEL_X, y, PAL_LIFE_UI);
    VBK_REG = VBK_TILES;
    gotoxy((uint8_t)(EQUIP_PANEL_X + 1u), y); putchar(':');
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 2u), y, player_hp, 3u);
    gotoxy((uint8_t)(EQUIP_PANEL_X + 5u), y); putchar('/');
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 6u), y, player_hp_max, 3u);

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 1u)); printf("Damage:");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 1u), player_damage, 3u);

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 2u)); printf("Crit%%:");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 2u), player_crit_chance, 2u);
    gotoxy((uint8_t)(EQUIP_PANEL_X + 9u), (uint8_t)(y + 2u)); putchar('%');

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 3u)); printf("Dodge%%:");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 3u), player_dodge, 2u);
    gotoxy((uint8_t)(EQUIP_PANEL_X + 9u), (uint8_t)(y + 3u)); putchar('%');

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 4u)); printf("Armor:");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 4u), player_armor, 2u);
    gotoxy((uint8_t)(EQUIP_PANEL_X + 9u), (uint8_t)(y + 4u)); putchar('%');

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 5u)); printf("MagDef:");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 5u), player_magdef, 2u);
    gotoxy((uint8_t)(EQUIP_PANEL_X + 9u), (uint8_t)(y + 5u)); putchar('%');

    gotoxy(EQUIP_PANEL_X, (uint8_t)(y + 6u)); printf("Light :");
    put_stat_uint((uint8_t)(EQUIP_PANEL_X + 7u), (uint8_t)(y + 6u), player_light_radius(), 3u);
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
    uint8_t i, pos, tile_val;
    gotoxy(0, INV_DESC_ROW);
    for (i = 0u; i < INV_DESC_W; i++) {
        pos = (uint8_t)(desc_off + i);
        if (pos >= desc_total_len) pos = (uint8_t)(pos - desc_total_len);
        putchar((pos < desc_base_len) ? desc_buf[pos] : ' ');
    }
    /* Write col 20 directly — putchar wraps at col 20 to (0, row+1) */
    pos = (uint8_t)(desc_off + INV_DESC_W);
    if (pos >= desc_total_len) pos = (uint8_t)(pos - desc_total_len);
    tile_val = (uint8_t)((pos < desc_base_len) ? desc_buf[pos] : ' ');
    set_bkg_tiles(INV_DESC_W, INV_DESC_ROW, 1u, 1u, &tile_val);
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
    uint8_t cx, cy, nlen, xi;
    char name[INV_NAME_LEN + 1u];
    uint8_t inv_kind = inventory_kind[inv_cursor];
    cell_origin(inv_cursor, &cx, &cy);
    entity_sprites_inv_cursor_show(cx, cy);
    for (xi = 2u; xi < 20u; xi++) set_bkg_attribute_xy(xi, INV_NAME_ROW, PAL_UI);
    gotoxy(2, INV_NAME_ROW);
    printf("                  "); // 18 spaces to clear name+count area
    if (inv_kind != ITEM_KIND_NONE) {
        items_kind_display_name_copy(inv_kind, inventory_mod_level[inv_cursor], name, (uint8_t)sizeof name);
        nlen = 0u; while (name[nlen]) nlen++;
        gotoxy(2, INV_NAME_ROW); printf("%s", name);
        if (items_kind_category(inv_kind) == ITEM_CAT_CONSUMABLE) {
            uint8_t cnt = inventory_count[inv_cursor];
            uint8_t digs = (cnt >= 100u) ? 3u : (cnt >= 10u) ? 2u : 1u;
            printf(" x%u", (unsigned int)cnt);
            for (xi = (uint8_t)(2u + nlen + 1u); xi < (uint8_t)(2u + nlen + 1u + 1u + digs); xi++)
                set_bkg_attribute_xy(xi, INV_NAME_ROW, PAL_XP_UI_BG);
        }
    } else {
        gotoxy(2, INV_NAME_ROW); printf("(empty)");
    }
    /* contextual action hint: usable items off the belt can be sent to it */
    gotoxy(1, 16);
    if (cursor_can_send_to_belt()) printf("A:to belt B:drop");
    else                           printf("A:equip  B:drop ");
    reset_desc_ticker(inv_kind);
}

static void draw_menu_tabs_inv(void) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
    uint8_t x;
    gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
    set_bkg_tiles(0u, 0u, 1u, 1u, &v);
    set_bkg_attribute_xy(0u, 0u, PAL_XP_UI_BG);
    for (x = 6u;  x <= 9u;  x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // STAT
    for (x = 11u; x <= 15u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // SPELL
    for (x = 17u; x <= 19u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // MAP
    VBK_REG = VBK_TILES;
    if (player_stat_points) { // unspent level-up points reminder
        gotoxy(10, 0); putchar('+');
        set_bkg_attribute_xy(10u, 0u, PAL_XP_UI_BG);
        VBK_REG = VBK_TILES;
    }
}

static void draw_grid_screen(void) {
    entity_sprites_inv_cursor_hide();
    lcd_clear_display();
    draw_menu_tabs_inv();
    draw_grid();
    draw_belt_label();
    draw_equipped_marks();
    draw_equip_panel();
    draw_stats_panel();
    draw_cursor_and_name(); // also paints the contextual A:... help line (row 16)
    gotoxy(1, 17); printf("START resume");
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(0u, INV_DESC_ROW, INV_DESC_DRAW_W, 1u, PAL_XP_UI_BG);
    VBK_REG = VBK_TILES;
}

static void draw_drop_confirm(void) {
    uint8_t kind = inventory_kind[inv_cursor];
    uint8_t v;
    char namebuf[18];
    items_kind_display_name_copy(kind, inventory_mod_level[inv_cursor], namebuf, sizeof namebuf);
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

static void draw_belt_pick(void) {
    uint8_t cx, cy, xi;
    char name[INV_NAME_LEN + 1u];
    /* highlight the candidate belt slot with the inventory cursor sprite */
    cell_origin(belt_pick_idx, &cx, &cy);
    entity_sprites_inv_cursor_show(cx, cy);
    /* name row: "To belt: <item>" */
    for (xi = 2u; xi < 20u; xi++) set_bkg_attribute_xy(xi, INV_NAME_ROW, PAL_UI);
    gotoxy(2, INV_NAME_ROW);
    printf("                  "); // 18 spaces to clear name area
    items_kind_display_name_copy(inventory_kind[inv_swap_src], inventory_mod_level[inv_swap_src],
                                 name, (uint8_t)sizeof name);
    gotoxy(2, INV_NAME_ROW); printf("%s", name);
    /* help rows: pick instructions */
    gotoxy(1, 16); printf("<> pick slot   ");
    gotoxy(1, 17); printf("A:ok  B:cancel ");
}

BANKREF(state_inventory_enter)
void state_inventory_enter(void) BANKED {
    static const palette_color_t inv_bkg0_black[4] = { RGB(0,0,0), RGB(8,8,8), RGB(16,16,16), RGB(31,31,31) };
    // PAL_WALL_BG carries metal/bronze-ring icons; the overworld repurposes slot 3 for snow terrain,
    // so restore a neutral stone ramp here (== wall_palette_table[0]) so item icons aren't icy.
    static const palette_color_t inv_metal_ramp[4] = { RGB(2,2,6), RGB(8,6,4), RGB(14,12,10), RGB(22,20,18) };
    BANK_DBG("IV_enter");
    inv_prev_j = joypad();
    inv_cursor = 0u;
    inv_mode   = INV_MODE_GRID;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    set_bkg_palette(0u, 1u, inv_bkg0_black); // menu is black-backed even on the overworld (green field slot 0)
    set_bkg_palette(PAL_WALL_BG, 1u, inv_metal_ramp); // restore metal ramp (hub may have left snow here)
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

    if (inv_mode == INV_MODE_BELT_PICK) {
        if (e & J_LEFT) {
            belt_pick_idx = (belt_pick_idx == 0u) ? (uint8_t)(BELT_ITEM_SLOT_COUNT - 1u)
                                                  : (uint8_t)(belt_pick_idx - 1u);
            wait_vbl_done();
            draw_belt_pick();
        } else if (e & J_RIGHT) {
            belt_pick_idx = (uint8_t)((belt_pick_idx + 1u) % BELT_ITEM_SLOT_COUNT);
            wait_vbl_done();
            draw_belt_pick();
        } else if (e & J_A) {
            swap_slots(inv_swap_src, belt_pick_idx);
            inv_cursor = belt_pick_idx; // cursor follows the item onto the belt
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

    if (e & J_START)  { inv_desc_scx = 0u; entity_sprites_inv_cursor_hide(); entity_sprites_equip_marks_hide(); next_state = STATE_GAMEPLAY; goto out; }
    if (e & J_SELECT) { inv_desc_scx = 0u; entity_sprites_inv_cursor_hide(); entity_sprites_equip_marks_hide(); next_state = STATE_STATS;    goto out; }

    if (e & J_A) {
        uint8_t kind = inventory_kind[inv_cursor];
        if (kind != ITEM_KIND_NONE && items_kind_category(kind) == ITEM_CAT_EQUIPMENT) {
            if (!inventory_equipped[inv_cursor]) {
                uint8_t i, my_slot, ek;
                my_slot = items_equip_slot(kind);
                if (my_slot != EQUIP_SLOT_NONE) {
                    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
                        ek = inventory_kind[i];
                        if (i != inv_cursor && inventory_equipped[i] &&
                                items_equip_slot(ek) == my_slot) {
                            inventory_equipped[i] = 0u;
                            items_equip_apply(ek, i, 0u);
                        }
                    }
                }
            }
            inventory_equipped[inv_cursor] ^= 1u;
            items_equip_apply(kind, inv_cursor, inventory_equipped[inv_cursor]);
            wait_vbl_done();
            draw_equipped_marks();
            draw_equip_panel();
            draw_stats_panel();
        } else if (cursor_can_send_to_belt()) {
            inv_swap_src  = inv_cursor;
            belt_pick_idx = 0u;
            inv_mode      = INV_MODE_BELT_PICK;
            inv_desc_scx  = 0u;
            wait_vbl_done();
            draw_belt_pick();
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
        if (e & J_LEFT) {
            if ((inv_cursor % INV_GRID_COLS) > 0u) inv_cursor--;
            else inv_cursor = (uint8_t)(inv_cursor + (INV_GRID_COLS - 1u));
        }
        if (e & J_RIGHT) {
            if ((inv_cursor % INV_GRID_COLS) < (uint8_t)(INV_GRID_COLS - 1u)) inv_cursor++;
            else inv_cursor = (uint8_t)(inv_cursor - (INV_GRID_COLS - 1u));
        }
        if (e & J_UP) {
            if (inv_cursor >= INV_GRID_COLS) inv_cursor = (uint8_t)(inv_cursor - INV_GRID_COLS);
            else inv_cursor = (uint8_t)(inv_cursor + (uint8_t)((INV_GRID_ROWS - 1u) * INV_GRID_COLS));
        }
        if (e & J_DOWN) {
            if ((uint8_t)(inv_cursor + INV_GRID_COLS) < INVENTORY_MAX_SLOTS) inv_cursor = (uint8_t)(inv_cursor + INV_GRID_COLS);
            else inv_cursor = (uint8_t)(inv_cursor % INV_GRID_COLS);
        }
        if (inv_cursor != old) {
            wait_vbl_done();
            draw_cursor_and_name();
        }
    }
out:
    inv_prev_j = j;
    wait_vbl_done();
}
