#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "equipment.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include "map.h"
#include "entity_sprites.h"
BANKREF_EXTERN(ground_item_kill)
BANKREF_EXTERN(entity_sprites_inv_cursor_show)
BANKREF_EXTERN(entity_sprites_inv_cursor_hide)
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

#define PU_SEL_TAKE    0u
#define PU_SEL_LEAVE   1u
#define PU_SEL_DISCARD 2u
#define PU_OPT_COUNT   3u

// inv_cursor cy for options at bg rows 15, 16, 17 (cy = row - 1 due to +1 in cursor_show)
#define PU_ARROW_CX    2u  // sprite at screen x=16, directly left of text at col 3 (screen x=24)
#define PU_TAKE_CY    14u
#define PU_LEAVE_CY   15u
#define PU_DISCARD_CY 16u

static uint8_t pu_prev_j;
static uint8_t pu_sel;
static uint8_t pu_inv_full;
static uint8_t pu_kind; // cached so we still know what was offered after ground slot is killed

static void pu_cursor_update(void) {
    entity_sprites_inv_cursor_show(PU_ARROW_CX, (uint8_t)(PU_TAKE_CY + pu_sel));
    set_sprite_tile(SP_INV_CURSOR, (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE));
    set_sprite_prop(SP_INV_CURSOR, (uint8_t)(PAL_XP_UI & 7u));
}

static void draw_icon(uint8_t x, uint8_t y) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(pu_kind));
    set_bkg_tiles(x, y, 1, 1, &v);
    set_bkg_attribute_xy(x, y, items_kind_palette(pu_kind));
    VBK_REG = VBK_TILES;
}

static void draw_equip_slot_info(void) {
    const char *label;
    uint8_t slot, cur_kind, slot_v, slot_pal;
    char cur_name[14];
    slot = items_equip_slot(pu_kind);
    switch (slot) {
        case EQUIP_SLOT_HEAD:    label = "Head"; break;
        case EQUIP_SLOT_BODY:    label = "Body"; break;
        case EQUIP_SLOT_FEET:    label = "Feet"; break;
        case EQUIP_SLOT_WEAPON:  label = "Hand"; break;
        case EQUIP_SLOT_OFFHAND: label = "Hand"; break;
        case EQUIP_SLOT_RING:    label = "Ring"; break;
        default: return;
    }
    cur_kind = equipped_kind_in_slot(slot);
    if (cur_kind != ITEM_KIND_NONE) {
        uint8_t cur_idx = equipped_inv_index(slot);
        int8_t cur_mod = (cur_idx < INVENTORY_MAX_SLOTS) ? inventory_mod_level[cur_idx] : 0;
        slot_v   = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(cur_kind));
        slot_pal = items_kind_palette(cur_kind);
        items_kind_display_name_copy(cur_kind, cur_mod, cur_name, sizeof cur_name);
    } else {
        slot_v   = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY);
        slot_pal = PAL_UI;
    }
    gotoxy(2u, 7u); printf("%s", label);
    set_bkg_tiles(6u, 7u, 1u, 1u, &slot_v);
    set_bkg_attribute_xy(6u, 7u, slot_pal);
    VBK_REG = VBK_TILES;
    gotoxy(8u, 7u);
    if (cur_kind != ITEM_KIND_NONE) printf("%s", cur_name);
    else printf("(empty)");
}

static void draw_phase(void) {
    char namebuf[18];
    items_kind_display_name_copy(pu_kind, ground_item_mod_level[pending_pickup_slot], namebuf, sizeof namebuf);
    lcd_clear_display();
    gotoxy(2, 4); printf("Found:");
    draw_icon(3, 6);
    gotoxy(5, 6); printf("%s", namebuf);
    if (items_kind_category(pu_kind) == ITEM_CAT_EQUIPMENT) {
        draw_equip_slot_info();
    } else if (items_kind_category(pu_kind) == ITEM_CAT_CONSUMABLE) {
        uint8_t existing = 0u, i;
        for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
            if (inventory_kind[i] == pu_kind) { existing = inventory_count[i]; break; }
        }
        gotoxy(2u, 7u); printf("You have %d", (int)existing);
    }
    if (pu_inv_full) {
        gotoxy(2u, 9u); printf("(no room)");
    }
    // bottom 3 rows: options (arrow cursor is a sprite, not text)
    gotoxy(3u, 15u); printf("Take");
    gotoxy(3u, 16u); printf("Leave");
    gotoxy(3u, 17u); printf("Discard");
    pu_cursor_update();
}

BANKREF(state_pickup_enter)
void state_pickup_enter(void) BANKED {
    BANK_DBG("PU_enter");
    pu_prev_j = joypad(); // ignore any button still held from the walk that triggered the modal
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    if (pending_pickup_slot >= MAX_GROUND_ITEMS
            || ground_item_kind[pending_pickup_slot] == ITEM_KIND_NONE) {
        pending_pickup_slot = 255u;
        next_state = STATE_GAMEPLAY;
        return;
    }
    pu_kind = ground_item_kind[pending_pickup_slot];
    pu_inv_full = (inventory_first_empty() == 255u) ? 1u : 0u;
    pu_sel = PU_SEL_TAKE;
    draw_phase();
}

BANKREF(state_pickup_tick)
void state_pickup_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~pu_prev_j);
    pu_prev_j = j;

    if (e & J_UP) {
        pu_sel = (pu_sel == 0u) ? (PU_OPT_COUNT - 1u) : (uint8_t)(pu_sel - 1u);
        pu_cursor_update();
    } else if (e & J_DOWN) {
        pu_sel = (uint8_t)((pu_sel + 1u) % PU_OPT_COUNT);
        pu_cursor_update();
    } else if (e & (J_A | J_B)) {
        if ((e & J_A) && pu_sel == PU_SEL_TAKE && !pu_inv_full) {
            int8_t pu_mod = ground_item_mod_level[pending_pickup_slot];
            inventory_add(pu_kind, pu_mod);
            ground_item_kill(pending_pickup_slot);
            {
                char log[20];
                char namebuf[16];
                uint8_t i = 0u, k = 0u;
                items_kind_display_name_copy(pu_kind, pu_mod, namebuf, sizeof namebuf);
                log[i++] = 'G'; log[i++] = 'o'; log[i++] = 't'; log[i++] = ' ';
                while (namebuf[k] && i < 19u) { log[i++] = namebuf[k++]; }
                log[i] = 0;
                ui_combat_log_push(log);
            }
        } else if ((e & J_A) && pu_sel == PU_SEL_DISCARD) {
            ground_item_kill(pending_pickup_slot);
        }
        // Leave, full Take, and B all exit without action
        entity_sprites_inv_cursor_hide();
        pending_pickup_slot = 255u;
        next_state = STATE_GAMEPLAY;
    }

    wait_vbl_done();
}
