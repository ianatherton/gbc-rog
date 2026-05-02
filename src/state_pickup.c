#pragma bank 3

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "items.h"
#include "lcd.h"
#include "ui.h"
#include "map.h"
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

static void draw_phase(void) {
    char namebuf[10];
    items_kind_name_copy(pu_kind, namebuf, sizeof namebuf);
    lcd_clear_display();
    if (pu_phase == PU_PHASE_FULL) {
        gotoxy(2, 4); printf("Found:");
        draw_icon(3, 6);
        gotoxy(5, 6); printf("%s", namebuf);
        gotoxy(2, 9); printf("No room!");
        gotoxy(2, 11); printf("A or B back");
        return;
    }
    gotoxy(2, 4); printf("Found:");
    draw_icon(3, 6);
    gotoxy(5, 6); printf("%s", namebuf);
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
