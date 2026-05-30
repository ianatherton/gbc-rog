#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include "map.h"
BANKREF_EXTERN(ground_item_kill)
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

#define PU_PHASE_GET     0u
#define PU_PHASE_DISCARD 1u
#define PU_PHASE_FULL    2u // inventory had no room when prompt opened — A/B both exit

static uint8_t pu_prev_j;
static uint8_t pu_phase;
static uint8_t pu_kind; // cached so we still know what was offered after ground slot is killed

static void draw_icon(uint8_t x, uint8_t y) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(pu_kind));
    set_bkg_tiles(x, y, 1, 1, &v);
    set_bkg_attribute_xy(x, y, items_kind_palette(pu_kind));
    VBK_REG = VBK_TILES;
}

static void draw_equip_slot_info(void) {
    const char *label;
    uint8_t slot_kind, cur_kind, i, slot_v, slot_pal;
    char cur_name[14];
    switch (pu_kind) {
        case ITEM_KIND_HELMET:      label = "Head"; slot_kind = ITEM_KIND_HELMET;      break;
        case ITEM_KIND_TUNIC:       label = "Body"; slot_kind = ITEM_KIND_TUNIC;       break;
        case ITEM_KIND_BOOTS:       label = "Feet"; slot_kind = ITEM_KIND_BOOTS;       break;
        case ITEM_KIND_RUSTY_SWORD: label = "Hand"; slot_kind = ITEM_KIND_RUSTY_SWORD; break;
        default: return;
    }
    cur_kind = ITEM_KIND_NONE;
    for (i = 0u; i < INVENTORY_MAX_SLOTS; i++) {
        if (inventory_kind[i] == slot_kind && inventory_equipped[i]) { cur_kind = slot_kind; break; }
    }
    if (cur_kind != ITEM_KIND_NONE) {
        slot_v   = (uint8_t)(TILESET_VRAM_OFFSET + items_kind_tile(cur_kind));
        slot_pal = items_kind_palette(cur_kind);
        items_kind_name_copy(cur_kind, cur_name, sizeof cur_name);
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
    items_kind_name_copy(pu_kind, namebuf, sizeof namebuf);
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
    if (pu_phase == PU_PHASE_FULL) {
        gotoxy(2, 9); printf("No room!");
        gotoxy(2, 11); printf("A or B back");
        return;
    }
    if (pu_phase == PU_PHASE_GET) {
        gotoxy(2, 9); printf("Get?");
    } else {
        gotoxy(2, 9); printf("Discard?");
    }
    gotoxy(2, 11); printf("A:Yes  B:No");
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
    pu_phase = (inventory_first_empty() == 255u) ? PU_PHASE_FULL : PU_PHASE_GET;
    draw_phase();
}

BANKREF(state_pickup_tick)
void state_pickup_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~pu_prev_j);
    pu_prev_j = j;
    if (pu_phase == PU_PHASE_FULL) {
        if (e & (J_A | J_B)) {
            pending_pickup_slot = 255u;
            next_state = STATE_GAMEPLAY;
        }
        wait_vbl_done();
        return;
    }
    if (pu_phase == PU_PHASE_GET) {
        if (e & J_A) {
            inventory_add(pu_kind);
            ground_item_kill(pending_pickup_slot);
            {
                char log[20];
                char namebuf[16];
                uint8_t i = 0u, k = 0u;
                items_kind_name_copy(pu_kind, namebuf, sizeof namebuf);
                log[i++] = 'G'; log[i++] = 'o'; log[i++] = 't'; log[i++] = ' ';
                while (namebuf[k] && i < 19u) { log[i++] = namebuf[k++]; }
                log[i] = 0;
                ui_combat_log_push(log);
            }
            pending_pickup_slot = 255u;
            next_state = STATE_GAMEPLAY;
        } else if (e & J_B) {
            pu_phase = PU_PHASE_DISCARD;
            wait_vbl_done();
            draw_phase();
        }
    } else { // PU_PHASE_DISCARD
        if (e & J_A) {
            ground_item_kill(pending_pickup_slot);
            pending_pickup_slot = 255u;
            next_state = STATE_GAMEPLAY;
        } else if (e & J_B) {
            pending_pickup_slot = 255u;
            next_state = STATE_GAMEPLAY;
        }
    }
    wait_vbl_done();
}
