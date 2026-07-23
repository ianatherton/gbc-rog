#pragma bank 3

/* SPELL subscreen — train spells with spell points (+1/level) and pick the
   2-slot belt loadout. Rows are the active class's 6 spells (bank-27 table);
   un-designed spells render as "---". Follows state_stats.c idioms. */

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "lcd.h"
#include "ui.h"
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

#define SP_ROW_BASE 4u
#define SP_DESC_ROW 11u
#define SP_HINT_ROW 13u

static uint8_t ab_prev_j;
static uint8_t sp_cursor; // 0..SPELLS_PER_CLASS-1; only ever rests on rows where spells_exists()

static uint8_t sp_id(uint8_t i) { return SPELL_ID(player_class, i); }

static void sp_draw_pts(void) {
    gotoxy(13, 2); printf("       ");
    if (player_spell_points) { gotoxy(13, 2); printf("PTS %u", (unsigned)player_spell_points); }
}

static void sp_draw_badge(uint8_t i) { // [1]/[2] = assigned belt slot; cols 16-18
    gotoxy(16, (uint8_t)(SP_ROW_BASE + i));
    if      (belt_spell[0] == i) printf("[1]");
    else if (belt_spell[1] == i) printf("[2]");
    else                         printf("   ");
}

static void sp_draw_row(uint8_t i) {
    uint8_t row = (uint8_t)(SP_ROW_BASE + i);
    uint8_t id  = sp_id(i);
    char buf[12];
    gotoxy(1, row); printf("                   "); // clear cols 1-19
    gotoxy(1, row);
    if (!spells_exists(id)) { printf("---"); return; }
    spells_name_copy(id, buf, sizeof buf);
    printf("%s", buf);
    gotoxy(13, row);
    if (spell_rank[i] == 0u && player_level < spells_unlock_level(id)) {
        printf("Lv%u", (unsigned)spells_unlock_level(id)); // level-gated — can't learn yet
    } else {
        uint8_t r, maxr = spells_max_rank(id);
        for (r = 0u; r < maxr && r < 3u; r++) putchar((r < spell_rank[i]) ? '*' : '-');
    }
    sp_draw_badge(i);
}

static void sp_draw_desc(void) {
    char buf[20];
    gotoxy(1, SP_DESC_ROW); printf("                   ");
    if (!spells_exists(sp_id(sp_cursor))) return;
    gotoxy(1, SP_DESC_ROW);
    if (spell_rank[sp_cursor] == 0u && spells_learned_count() >= 2u) {
        printf("Locked: 2 chosen"); // pick-2 rule — this spell can't be learned this run
        return;
    }
    spells_desc_copy(sp_id(sp_cursor), buf, sizeof buf);
    printf("%s", buf);
}

static void sp_cursor_draw(void) {
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
    uint8_t row = (uint8_t)(SP_ROW_BASE + sp_cursor);
    set_bkg_tiles(0u, row, 1u, 1u, &v);
    set_bkg_attribute_xy(0u, row, PAL_XP_UI_BG);
    VBK_REG = VBK_TILES;
}

static void sp_cursor_clear(void) {
    uint8_t row = (uint8_t)(SP_ROW_BASE + sp_cursor);
    gotoxy(0, row); putchar(' ');
    set_bkg_attribute_xy(0u, row, 0u);
    VBK_REG = VBK_TILES;
}

static void sp_cursor_move(uint8_t up) { // wraps; skips un-designed rows
    uint8_t n;
    for (n = 0u; n < SPELLS_PER_CLASS; n++) {
        if (up) sp_cursor = (uint8_t)(sp_cursor ? sp_cursor - 1u : SPELLS_PER_CLASS - 1u);
        else    sp_cursor = (uint8_t)((sp_cursor + 1u) % SPELLS_PER_CLASS);
        if (spells_exists(sp_id(sp_cursor))) break;
    }
}

static void sp_try_train(void) {
    uint8_t id = sp_id(sp_cursor);
    uint8_t r  = spell_rank[sp_cursor];
    if (!player_spell_points || !spells_exists(id)) return;
    if (player_level < spells_unlock_level(id)) return;
    if (r >= spells_max_rank(id)) return;
    if (r == 0u && spells_learned_count() >= 2u) return; // pick-2 rule: two distinct spells trained = locked in
    spell_rank[sp_cursor] = (uint8_t)(r + 1u);
    player_spell_points--;
    if (r == 0u) { // just learned — auto-slot into a free belt slot (matches old witch-root UX)
        if      (belt_spell[0] >= SPELLS_PER_CLASS) belt_spell[0] = sp_cursor;
        else if (belt_spell[1] >= SPELLS_PER_CLASS && belt_spell[0] != sp_cursor) belt_spell[1] = sp_cursor;
    }
    sp_draw_row(sp_cursor);
    sp_draw_pts();
    if (!player_spell_points) { // all spent — retire the SPELL-tab '+'
        gotoxy(16, 0); putchar(' ');
        set_bkg_attribute_xy(16u, 0u, 0u);
        VBK_REG = VBK_TILES;
    }
}

static void sp_assign(uint8_t slot) { // LEFT=slot 0, RIGHT=slot 1; re-press unassigns; never duplicates
    uint8_t other = (uint8_t)(1u - slot);
    uint8_t i = sp_cursor, k;
    if (!spells_exists(sp_id(i)) || spell_rank[i] == 0u) return;
    if (belt_spell[slot] == i) {
        belt_spell[slot] = SPELL_IDX_NONE;
    } else {
        if (belt_spell[other] == i) belt_spell[other] = belt_spell[slot]; // swap slots
        belt_spell[slot] = i;
    }
    for (k = 0u; k < SPELLS_PER_CLASS; k++)
        if (spells_exists(sp_id(k))) sp_draw_badge(k);
}

BANKREF(state_ability_enter)
void state_ability_enter(void) BANKED {
    uint8_t i;
    BANK_DBG("AB_enter");
    ab_prev_j = joypad();
    sp_cursor = 0u;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    {
        uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE);
        uint8_t x;
        gotoxy(0, 0); printf(" ITEM STAT SPELL MAP");
        set_bkg_tiles(10u, 0u, 1u, 1u, &v);
        set_bkg_attribute_xy(10u, 0u, PAL_XP_UI_BG);
        for (x = 1u; x <= 4u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG);  // ITEM
        for (x = 6u; x <= 9u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG);  // STAT
        for (x = 17u; x <= 19u; x++) set_bkg_attribute_xy(x, 0u, PAL_XP_UI_BG); // MAP
        VBK_REG = VBK_TILES;
        if (player_stat_points) { // unspent stat points; col 10 holds this tab's arrow
            gotoxy(5, 0); putchar('+');
            set_bkg_attribute_xy(5u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
        if (player_spell_points) { // unspent spell points, right after SPELL
            gotoxy(16, 0); putchar('+');
            set_bkg_attribute_xy(16u, 0u, PAL_XP_UI_BG);
            VBK_REG = VBK_TILES;
        }
    }
    gotoxy(1, 2); printf("SPELLS");
    sp_draw_pts();
    for (i = 0u; i < SPELLS_PER_CLASS; i++) sp_draw_row(i);
    sp_draw_desc();
    gotoxy(1, SP_HINT_ROW);            printf("A:train L/R:belt");
    gotoxy(1, (uint8_t)(SP_HINT_ROW + 1u)); printf("START resume");
    sp_cursor_draw();
}

BANKREF(state_ability_tick)
void state_ability_tick(void) BANKED {
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~ab_prev_j);
    if (e & (J_UP | J_DOWN)) {
        sp_cursor_clear();
        sp_cursor_move((uint8_t)(e & J_UP));
        sp_cursor_draw();
        sp_draw_desc();
    }
    if (e & J_A)     sp_try_train();
    if (e & J_LEFT)  sp_assign(0u);
    if (e & J_RIGHT) sp_assign(1u);
    if (e & J_START)  next_state = STATE_GAMEPLAY;
    if (e & J_SELECT) next_state = STATE_MAP;
    ab_prev_j = j;
    wait_vbl_done();
}
