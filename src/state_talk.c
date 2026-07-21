#pragma bank 3

// Conversation modal. Bumping a town villager used to push one canned line into the combat log;
// villager slot TOWN_TRADER_NPC now opens this pop-up instead (biome_town.c town_npc_blocks sets
// next_state directly — bank 2 has no room for a dispatch hook). Trade is the only conversation
// type so far; pending_talk_npc is what a second type would branch on.
//
// Trade is deliberately flat: every item is worth exactly 1 token ('*'), buying or selling, and
// selling is the only way to earn tokens. Each trader stocks TOWN_SHOP_SLOTS kinds hashed from
// run_seed + town id, so the shelf is identical every time you walk back in; buying clears the
// slot's bit in town_shop_sold[] and it stays sold for the rest of the run.

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "dungeon.h" // TOWN_FLOOR_BASE — floor_num back to a town id
#include "equipment.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include "entity_sprites.h"
BANKREF_EXTERN(entity_sprites_inv_cursor_show)
BANKREF_EXTERN(entity_sprites_inv_cursor_hide)
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

#define TALK_MODE_ROOT 0u
#define TALK_MODE_BUY  1u
#define TALK_MODE_SELL 2u

#define TK_SEL_BUY   0u
#define TK_SEL_SELL  1u
#define TK_SEL_LEAVE 2u
#define TK_ROOT_OPTS 3u

// Cursor cy is one less than the BG row it points at (entity_sprites_inv_cursor_show adds 1).
#define TK_ARROW_CX   2u  // same column the pickup modal parks its arrow in
#define TK_ROOT_ROW  15u  // Buy / Sell / Leave on the bottom three rows, matching STATE_PICKUP
#define TK_LIST_ROW   4u  // first row of the 8-entry buy/sell list
#define TK_LIST_ROWS  8u
#define TK_NAME_COL   5u
#define TK_ICON_COL   3u
#define TK_PRICE_COL 16u

static uint8_t tk_prev_j;
static uint8_t tk_mode;
static uint8_t tk_root_sel;  // 0..TK_ROOT_OPTS-1
static uint8_t tk_list_sel;  // row within the visible window, 0..TK_LIST_ROWS-1
static uint8_t tk_sell_top;  // first sellable-slot ordinal shown in TALK_MODE_SELL
static uint8_t tk_sell_n;    // sellable (occupied, unequipped) inventory slots
static uint8_t tk_town;      // 0..TOWN_COUNT-1

// Right-aligned unsigned printer, same idea as state_inventory.c's put_stat_uint — printf("%d")
// pulls in far more code than this is worth.
static void put_uint(uint8_t x, uint8_t y, uint8_t v, uint8_t width) {
    uint8_t i = 0u, pad;
    char dig[3];
    if (!v) { dig[i++] = '0'; }
    else { while (v) { dig[i++] = (char)('0' + v % 10u); v /= 10u; } }
    gotoxy(x, y);
    for (pad = i; pad < width; pad++) putchar(' ');
    while (i) putchar(dig[--i]);
}

// Stock is a pure function of (run_seed, town, slot) — no RAM, and stable across re-entry. It must
// not touch rand(): reseeding the shared RNG from a menu would desync floor generation.
static uint8_t shop_kind(uint8_t slot) {
    uint16_t h = (uint16_t)(run_seed ^ (uint16_t)(0x9E37u + (uint16_t)tk_town * 0x2C9Fu));
    h = (uint16_t)(h * 25173u + 13849u);
    h = (uint16_t)(h + (uint16_t)slot * 0x7ED5u);
    h = (uint16_t)(h * 25173u + 13849u);
    return items_drop_table_pick((uint8_t)(h >> 5));
}

static uint8_t shop_slot_sold(uint8_t slot) {
    return (uint8_t)((town_shop_sold[tk_town] >> slot) & 1u);
}

// Equipped gear is skipped rather than auto-unequipped — silently stripping the player's armour
// mid-menu is a worse surprise than having to unequip first.
static uint8_t sell_slot_is_sellable(uint8_t s) {
    return (uint8_t)(inventory_kind[s] != ITEM_KIND_NONE && !inventory_equipped[s]);
}

// Maps the n-th sellable entry to its inventory slot; 255 if n is past the end.
static uint8_t sell_nth_slot(uint8_t n) {
    uint8_t s, seen = 0u;
    for (s = 0u; s < INVENTORY_MAX_SLOTS; s++) {
        if (!sell_slot_is_sellable(s)) continue;
        if (seen == n) return s;
        seen++;
    }
    return 255u;
}

static uint8_t sell_count(void) {
    uint8_t s, n = 0u;
    for (s = 0u; s < INVENTORY_MAX_SLOTS; s++) if (sell_slot_is_sellable(s)) n++;
    return n;
}

static void draw_icon(uint8_t kind, uint8_t x, uint8_t y) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(kind));
    set_bkg_tiles(x, y, 1u, 1u, &v);
    set_bkg_attribute_xy(x, y, items_kind_palette(kind));
    VBK_REG = VBK_TILES; // set_bkg_attribute_xy leaves VBK on the attribute plane
}

static void blank_icon(uint8_t x, uint8_t y) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY);
    set_bkg_tiles(x, y, 1u, 1u, &v);
    set_bkg_attribute_xy(x, y, PAL_UI);
    VBK_REG = VBK_TILES;
}

static void draw_tokens(uint8_t x, uint8_t y) {
    gotoxy(x, y); putchar('*');
    put_uint((uint8_t)(x + 1u), y, player_tokens, 2u);
}

static void cursor_at(uint8_t cy) {
    entity_sprites_inv_cursor_show(TK_ARROW_CX, cy);
    set_sprite_tile(SP_INV_CURSOR, (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE));
    set_sprite_prop(SP_INV_CURSOR, (uint8_t)(PAL_XP_UI & 7u));
}

static void cursor_update(void) {
    if (tk_mode == TALK_MODE_ROOT) cursor_at((uint8_t)(TK_ROOT_ROW - 1u + tk_root_sel));
    else                           cursor_at((uint8_t)(TK_LIST_ROW - 1u + tk_list_sel));
}

static void draw_root(void) {
    lcd_clear_display();
    gotoxy(2u, 3u); printf("Trade");
    draw_tokens(15u, 3u);
    gotoxy(2u, 5u); printf("All goods");
    gotoxy(2u, 6u); printf("cost *1.");
    gotoxy(3u, TK_ROOT_ROW);            printf("Buy");
    gotoxy(3u, (uint8_t)(TK_ROOT_ROW + 1u)); printf("Sell");
    gotoxy(3u, (uint8_t)(TK_ROOT_ROW + 2u)); printf("Leave");
    cursor_update();
}

static void draw_buy(void) {
    uint8_t i;
    lcd_clear_display();
    gotoxy(2u, 1u); printf("Buy");
    draw_tokens(15u, 1u);
    for (i = 0u; i < TK_LIST_ROWS; i++) {
        uint8_t row = (uint8_t)(TK_LIST_ROW + i);
        if (shop_slot_sold(i)) {
            blank_icon(TK_ICON_COL, row);
            gotoxy(TK_NAME_COL, row); printf("---");
        } else {
            char nm[12];
            uint8_t kind = shop_kind(i);
            items_kind_display_name_copy(kind, 0, nm, sizeof nm);
            draw_icon(kind, TK_ICON_COL, row);
            gotoxy(TK_NAME_COL, row); printf("%s", nm);
            gotoxy(TK_PRICE_COL, row); printf("*1");
        }
    }
    gotoxy(2u, 14u); printf("A buy  B back");
    cursor_update();
}

static void draw_sell(void) {
    uint8_t i;
    lcd_clear_display();
    gotoxy(2u, 1u); printf("Sell");
    draw_tokens(15u, 1u);
    for (i = 0u; i < TK_LIST_ROWS; i++) {
        uint8_t row = (uint8_t)(TK_LIST_ROW + i);
        uint8_t s = sell_nth_slot((uint8_t)(tk_sell_top + i));
        if (s != 255u) { // rows past the end stay as lcd_clear_display left them
            char nm[11];
            items_kind_display_name_copy(inventory_kind[s], inventory_mod_level[s], nm, sizeof nm);
            draw_icon(inventory_kind[s], TK_ICON_COL, row);
            gotoxy(TK_NAME_COL, row); printf("%s", nm);
            if (inventory_count[s] > 1u) { // a sale takes one off the stack, so show what's left
                gotoxy((uint8_t)(TK_PRICE_COL - 2u), row);
                putchar('x');
                putchar(inventory_count[s] >= 10u ? '*' : (char)('0' + inventory_count[s]));
            }
            gotoxy(TK_PRICE_COL, row); printf("+*1");
        }
    }
    if (!tk_sell_n) { gotoxy(2u, (uint8_t)(TK_LIST_ROW + 1u)); printf("Nothing to sell."); }
    gotoxy(2u, 14u); printf("A sell B back");
    cursor_update();
}

static void draw_mode(void) {
    if      (tk_mode == TALK_MODE_BUY)  draw_buy();
    else if (tk_mode == TALK_MODE_SELL) draw_sell();
    else                                draw_root();
}

// Log lines are assembled a character at a time from a stack buffer: the item names live in bank
// 13, and handing a foreign-bank pointer to ui_combat_log_push (bank 5) garbles it — same reason
// state_pickup.c builds its "Got <name>" line this way.
static void log_trade(const char *verb3, uint8_t kind, int8_t mod) {
    char line[20];
    char nm[16];
    uint8_t i = 0u, k = 0u;
    items_kind_display_name_copy(kind, mod, nm, sizeof nm);
    while (verb3[i] && i < 8u) { line[i] = verb3[i]; i++; }
    while (nm[k] && i < 19u) { line[i++] = nm[k++]; }
    line[i] = 0;
    ui_combat_log_push(line);
}

static void do_buy(void) {
    uint8_t kind;
    if (!player_tokens || shop_slot_sold(tk_list_sel)) return;
    if (inventory_first_empty() == 255u) return; // no room — leave the token unspent
    kind = shop_kind(tk_list_sel);
    if (!inventory_add(kind, 0)) return;
    player_tokens--;
    town_shop_sold[tk_town] |= (uint8_t)(1u << tk_list_sel);
    log_trade("Bought ", kind, 0);
    draw_buy();
}

static void do_sell(void) {
    uint8_t s = sell_nth_slot((uint8_t)(tk_sell_top + tk_list_sel));
    uint8_t kind;
    int8_t mod;
    if (s == 255u) return;
    kind = inventory_kind[s];
    mod  = inventory_mod_level[s];
    inventory_remove(s); // carries the belt/equipment compaction fixup — don't clear the slot directly
    if (player_tokens < TOKENS_MAX) player_tokens++;
    tk_sell_n = sell_count();
    // The list shrank under the cursor: pull the window and cursor back into range.
    if (tk_sell_top && (uint8_t)(tk_sell_top + TK_LIST_ROWS) > tk_sell_n) tk_sell_top--;
    if (tk_sell_n <= tk_sell_top) tk_sell_top = 0u;
    {
        uint8_t vis = (uint8_t)(tk_sell_n - tk_sell_top);
        if (vis > TK_LIST_ROWS) vis = TK_LIST_ROWS;
        if (vis && tk_list_sel >= vis) tk_list_sel = (uint8_t)(vis - 1u);
        else if (!vis) tk_list_sel = 0u;
    }
    log_trade("Sold ", kind, mod);
    draw_sell();
}

static void enter_mode(uint8_t mode) {
    tk_mode = mode;
    tk_list_sel = 0u;
    if (mode == TALK_MODE_SELL) { tk_sell_n = sell_count(); tk_sell_top = 0u; }
    draw_mode();
}

BANKREF(state_talk_enter)
void state_talk_enter(void) BANKED {
    BANK_DBG("TK_enter");
    tk_prev_j = joypad(); // swallow the direction still held from the bump that opened this
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    tk_town = (floor_num >= TOWN_FLOOR_BASE) ? (uint8_t)(floor_num - TOWN_FLOOR_BASE) : 0u;
    if (tk_town >= TOWN_COUNT) tk_town = 0u;
    tk_root_sel = TK_SEL_BUY;
    enter_mode(TALK_MODE_ROOT);
}

BANKREF(state_talk_tick)
void state_talk_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~tk_prev_j);
    tk_prev_j = j;

    if (tk_mode == TALK_MODE_ROOT) {
        if (e & J_UP) {
            tk_root_sel = (tk_root_sel == 0u) ? (TK_ROOT_OPTS - 1u) : (uint8_t)(tk_root_sel - 1u);
            cursor_update();
        } else if (e & J_DOWN) {
            tk_root_sel = (uint8_t)((tk_root_sel + 1u) % TK_ROOT_OPTS);
            cursor_update();
        } else if (e & J_A) {
            if      (tk_root_sel == TK_SEL_BUY)  enter_mode(TALK_MODE_BUY);
            else if (tk_root_sel == TK_SEL_SELL) enter_mode(TALK_MODE_SELL);
            else                                 goto leave;
        } else if (e & J_B) {
            goto leave;
        }
    } else {
        uint8_t rows = TK_LIST_ROWS;
        if (tk_mode == TALK_MODE_SELL) {
            rows = (uint8_t)(tk_sell_n - tk_sell_top);
            if (rows > TK_LIST_ROWS) rows = TK_LIST_ROWS;
            if (!rows) rows = 1u; // empty list still parks the cursor on row 0
        }
        if (e & J_UP) {
            if (tk_list_sel) tk_list_sel--;
            else if (tk_mode == TALK_MODE_SELL && tk_sell_top) { tk_sell_top--; draw_sell(); }
            cursor_update();
        } else if (e & J_DOWN) {
            if ((uint8_t)(tk_list_sel + 1u) < rows) tk_list_sel++;
            else if (tk_mode == TALK_MODE_SELL
                     && (uint8_t)(tk_sell_top + TK_LIST_ROWS) < tk_sell_n) { tk_sell_top++; draw_sell(); }
            cursor_update();
        } else if (e & J_A) {
            if (tk_mode == TALK_MODE_BUY) do_buy();
            else                          do_sell();
        } else if (e & J_B) {
            enter_mode(TALK_MODE_ROOT);
        }
    }

    wait_vbl_done();
    return;

leave:
    entity_sprites_inv_cursor_hide();
    pending_talk_npc = 255u;
    next_state = STATE_GAMEPLAY;
    wait_vbl_done();
}
