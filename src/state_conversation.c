#pragma bank 26

// Conversation modal. Bumping ANY town villager opens this — the same "walk into it" gesture that
// starts a melee turn against an enemy (state_gameplay.c's move branch); biome_town.c's
// town_npc_blocks sets next_state directly rather than queueing a global, because bank 2 has no
// room for a dispatch hook (see the comment at that call site). The canned one-liner villagers used
// to say on a bump is now a PROXIMITY greeting instead: town_npcs_tick pushes it to the chat box
// when the player comes within TOWN_NPC_GREET_RADIUS tiles.
//
// The screen is black-on-black like the trade menu (state_talk.c) with the fullscreen menu ring
// around it (ui_draw_bkg_frame, the same border the seed picker uses), a 2x-scaled cloaked-figure
// portrait in the top right, and the dialogue body + selectable options below.
//
// Dialogue is a flat node table: each node is a few pre-wrapped body lines plus up to DLG_MAX_OPTS
// options, and every option either jumps to another node (DLG_ACT_GOTO) or fires an action. Adding
// dialogue is only ever new DlgNode rows — the whole table lives in this bank alongside the code
// that prints it, so no string ever crosses a bank boundary (project_cross_bank_string_literal_gotcha).

#include "debug_bank.h"
#include "game_state.h"
#include "globals.h"
#include "defs.h"
#include "dungeon.h" // TOWN_FLOOR_BASE — floor_num back to a town id
#include "lcd.h"
#include "names.h"
#include "tileset_io.h"
#include "ui.h"
#include "entity_sprites.h"
BANKREF_EXTERN(entity_sprites_inv_cursor_show)
BANKREF_EXTERN(entity_sprites_inv_cursor_hide)
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>

/* ── Screen layout ───────────────────────────────────────────────────────────
   Everything sits inside ui_draw_bkg_frame's 1-tile ring, so cols 1..18 / rows 1..16.
   Cursor cy is one less than the row it points at (entity_sprites_inv_cursor_show adds 1). */
#define CV_PORTRAIT_X  14u // top-right, 4 wide x 6 tall: cols 14..17, rows 1..6
#define CV_PORTRAIT_Y   1u
#define CV_PORTRAIT_W   4u
#define CV_PORTRAIT_H   6u
#define CV_NAME_COL     1u
#define CV_NAME_ROW     1u
#define CV_BODY_COL     1u
#define CV_BODY_ROW     8u // 3 lines, rows 8..10
#define CV_BODY_LINES   3u
#define CV_OPT_COL      3u // arrow parks at col 2
#define CV_OPT_ROW     13u // rows 13..16
#define CV_ARROW_CX     2u

// Black paper / white ink for BG palette 0, same contract as state_talk.c's pal_talk_bg: gameplay
// re-entry runs apply_field_palette() (state_gameplay.c), which restores slot 0 and invalidates the
// wall-palette cache so slots 1 and 3 get re-pushed by draw_screen's apply_wall_palette. That is
// why the portrait can borrow PAL_PILLAR_BG and the frame can borrow PAL_WALL_BG with no undo here.
static const palette_color_t pal_conv_bg[] = {
    RGB(0, 0, 0), RGB(10, 10, 10), RGB(20, 20, 20), RGB(31, 31, 31),
};

// Cloak ramp for the portrait (BG slot PAL_PILLAR_BG). idx0 is pure black so the figure's
// transparent background merges into the menu; 1-3 climb through a cold shadowed grey.
static const palette_color_t pal_conv_cloak[] = {
    RGB(0, 0, 0), RGB(4, 3, 8), RGB(11, 9, 17), RGB(24, 22, 29),
};

/* ── Dialogue tree ───────────────────────────────────────────────────────────
   Body lines are pre-wrapped by hand to <= 18 chars (cols 1..18) and option labels to <= 16
   (cols 3..18). There is no runtime word wrap available here: story_ui.c's word_wrap() is bank-14
   static and writes through the floor_bits scratch overlay, which is only legal before
   generate_level. NULL ends a node's body early. */
#define DLG_MAX_OPTS 4u

#define DLG_ACT_GOTO  0u // opt_arg = next node id
#define DLG_ACT_END   1u // close, back to gameplay
#define DLG_ACT_TRADE 2u // hand off to the trade menu (STATE_TALK)

typedef struct {
    const char *line[CV_BODY_LINES];
    const char *opt_label[DLG_MAX_OPTS];
    uint8_t     opt_action[DLG_MAX_OPTS];
    uint8_t     opt_arg[DLG_MAX_OPTS];
    uint8_t     opt_count;
} DlgNode;

#define N_TRADER_ROOT   0u
#define N_TRADER_TOWN   1u
#define N_ELDER_ROOT    2u
#define N_ELDER_DEEP    3u
#define N_ELDER_ADVICE  4u
#define N_WATCH_ROOT    5u
#define N_WATCH_WALLS   6u
#define N_CHILD_ROOT    7u
#define N_CHILD_GAME    8u

// Rows are positional and MUST stay in N_* order — SDCC has no designated-initializer support to
// lean on here, so the #defines above are just indices into this array.
static const DlgNode dlg_nodes[] = {
    { /* N_TRADER_ROOT */
        { "Coin for steel,", "steel for coin.", "Same as ever." },
        { "Trade", "Ask of the town", "Leave", 0 },
        { DLG_ACT_TRADE, DLG_ACT_GOTO, DLG_ACT_END, 0u },
        { 0u, N_TRADER_TOWN, 0u, 0u },
        3u,
    },
    { /* N_TRADER_TOWN */
        { "Walls hold. The", "dungeons do not.", "Come back richer." },
        { "Back", "Trade", "Leave", 0 },
        { DLG_ACT_GOTO, DLG_ACT_TRADE, DLG_ACT_END, 0u },
        { N_TRADER_ROOT, 0u, 0u, 0u },
        3u,
    },
    { /* N_ELDER_ROOT */
        { "You have the look", "of one bound for", "the deep places." },
        { "Ask of the deep", "Ask for advice", "Leave", 0 },
        { DLG_ACT_GOTO, DLG_ACT_GOTO, DLG_ACT_END, 0u },
        { N_ELDER_DEEP, N_ELDER_ADVICE, 0u, 0u },
        3u,
    },
    { /* N_ELDER_DEEP */
        { "Nine ways down.", "Each one deeper", "than the last." },
        { "Back", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_ELDER_ROOT, 0u, 0u, 0u },
        2u,
    },
    { /* N_ELDER_ADVICE */
        { "Carry more than", "you think you", "need. Always." },
        { "Back", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_ELDER_ROOT, 0u, 0u, 0u },
        2u,
    },
    { /* N_WATCH_ROOT */
        { "Keep your blade", "sheathed inside", "these walls." },
        { "Ask of the walls", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_WATCH_WALLS, 0u, 0u, 0u },
        2u,
    },
    { /* N_WATCH_WALLS */
        { "Brick and stone.", "They have held", "since before me." },
        { "Back", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_WATCH_ROOT, 0u, 0u, 0u },
        2u,
    },
    { /* N_CHILD_ROOT */
        { "Are you a real", "hero? You look", "tired for one." },
        { "Play along", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_CHILD_GAME, 0u, 0u, 0u },
        2u,
    },
    { /* N_CHILD_GAME */
        { "Ha! Then bring me", "back a monster", "tooth. A big one." },
        { "Back", "Leave", 0, 0 },
        { DLG_ACT_GOTO, DLG_ACT_END, 0u, 0u },
        { N_CHILD_ROOT, 0u, 0u, 0u },
        2u,
    },
};

// Root node per villager persona. Slot TOWN_TRADER_NPC always gets the trader tree; everyone else
// hashes into this table.
static const uint8_t dlg_persona_root[] = { N_ELDER_ROOT, N_WATCH_ROOT, N_CHILD_ROOT };
#define DLG_PERSONA_COUNT 3u

static uint8_t cv_prev_j;
static uint8_t cv_node;
static uint8_t cv_sel;   // 0..opt_count-1
static uint8_t cv_town;  // 0..TOWN_COUNT-1
static uint8_t cv_npc;   // villager slot this conversation belongs to

// Which tree an NPC talks from — a pure hash of (run_seed, town, slot), stable across re-entry.
// It must not touch rand(): reseeding the shared RNG from a menu would desync floor generation
// (same rule as state_talk.c's shop_kind).
static uint8_t dlg_root_for_npc(uint8_t slot) {
    uint16_t h;
    if (slot == TOWN_TRADER_NPC) return N_TRADER_ROOT;
    h = (uint16_t)(run_seed ^ (uint16_t)(0x5B27u + (uint16_t)cv_town * 0x3A1Fu));
    h = (uint16_t)(h * 25173u + 13849u);
    h = (uint16_t)(h + (uint16_t)slot * 0x6D2Bu);
    h = (uint16_t)(h * 25173u + 13849u);
    return dlg_persona_root[(uint8_t)((h >> 7) % DLG_PERSONA_COUNT)];
}

/* ── Portrait ────────────────────────────────────────────────────────────────
   Source art is 2x3 sheet cells at O10:P12 (TILE_NPC_CLOAK + {0,1,16,17,32,33}), blown up 2x to
   4x6 tiles. Scaling runs one source tile at a time — 16 B in, 64 B out — instead of packing all
   six up front like state_char_create.c's class_emblem_draw, which overlays floor_bits[] and is
   therefore only legal before generate_level. A mid-game modal has to stay on the stack, and ~104 B
   of frame is comfortable against the WRAM headroom (docs/BANKS.md).
   Destination is the dead font tail 104..127 — see the CONV_PORTRAIT_VRAM note in defs.h. Nothing
   else ever reads those tiles, so there is no restore on the way out. */
static const uint8_t cloak_src[6] = {
    (uint8_t)(TILE_NPC_CLOAK),        (uint8_t)(TILE_NPC_CLOAK + 1u),
    (uint8_t)(TILE_NPC_CLOAK + 16u),  (uint8_t)(TILE_NPC_CLOAK + 17u),
    (uint8_t)(TILE_NPC_CLOAK + 32u),  (uint8_t)(TILE_NPC_CLOAK + 33u),
};

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

// Tile data only — run once on entry. Each source tile expands into 4 consecutive VRAM slots
// (quadrant order TL, TR, BL, BR); portrait_stamp() is what maps those back to screen cells.
static void portrait_upload(void) {
    uint8_t pack[16];
    uint8_t out[64];
    uint8_t t, ox, oy;
    for (t = 0u; t < 6u; t++) {
        tileset_read_tiles(pack, cloak_src[t], 1u); // HOME shim: bank 26 must not SWITCH_ROM itself
        for (oy = 0u; oy < 16u; oy++) {
            for (ox = 0u; ox < 16u; ox++) {
                uint8_t c   = tile2bpp_get_px(pack, (uint8_t)(ox >> 1u), (uint8_t)(oy >> 1u));
                uint8_t tid = (uint8_t)((uint8_t)(oy >> 3u) * 2u + (uint8_t)(ox >> 3u));
                tile2bpp_set_px(out + (uint16_t)tid * 16u, (uint8_t)(ox & 7u), (uint8_t)(oy & 7u), c);
            }
        }
        set_bkg_data((uint8_t)(CONV_PORTRAIT_VRAM + (uint8_t)(t * 4u)), 4u, out);
    }
}

// Tilemap + attributes. Re-run on every full redraw, since lcd_clear_display wipes both planes.
static void portrait_stamp(void) {
    uint8_t buf[CV_PORTRAIT_W * CV_PORTRAIT_H];
    uint8_t sx, sy, qx, qy, i, j;
    for (sy = 0u; sy < 3u; sy++) {
        for (sx = 0u; sx < 2u; sx++) {
            uint8_t t = (uint8_t)(sy * 2u + sx);
            for (qy = 0u; qy < 2u; qy++)
                for (qx = 0u; qx < 2u; qx++)
                    buf[(uint8_t)((uint8_t)(sy * 2u + qy) * CV_PORTRAIT_W + (uint8_t)(sx * 2u + qx))] =
                        (uint8_t)(CONV_PORTRAIT_VRAM + (uint8_t)(t * 4u) + (uint8_t)(qy * 2u + qx));
        }
    }
    set_bkg_tiles(CV_PORTRAIT_X, CV_PORTRAIT_Y, CV_PORTRAIT_W, CV_PORTRAIT_H, buf);
    for (j = 0u; j < CV_PORTRAIT_H; j++)
        for (i = 0u; i < CV_PORTRAIT_W; i++)
            set_bkg_attribute_xy((uint8_t)(CV_PORTRAIT_X + i), (uint8_t)(CV_PORTRAIT_Y + j), PAL_PILLAR_BG);
    VBK_REG = VBK_TILES; // set_bkg_attribute_xy leaves VBK on the attribute plane
}

static void cursor_at(uint8_t cy) {
    entity_sprites_inv_cursor_show(CV_ARROW_CX, cy);
    set_sprite_tile(SP_INV_CURSOR, (uint8_t)(TILESET_VRAM_OFFSET + TILE_ARROW_SE));
    set_sprite_prop(SP_INV_CURSOR, (uint8_t)(PAL_XP_UI & 7u));
}

static void cursor_update(void) {
    cursor_at((uint8_t)(CV_OPT_ROW - 1u + cv_sel));
}

// Full repaint, same shape as state_talk.c's draw_root: an LCD-off clear is the only safe way to
// blank both planes at once, and it must land in VBlank or the screen flashes white. Only a node
// change calls this; moving the cursor just moves the sprite.
static void draw_node(void) {
    const DlgNode *n = &dlg_nodes[cv_node];
    char nm[16];
    uint8_t i;
    wait_vbl_done();
    lcd_clear_display();
    ui_draw_bkg_frame(); // bank 5 — the seed-screen ring; also pushes its gold ramp into PAL_WALL_BG
    portrait_stamp();
    npc_name_copy(cv_town, cv_npc, nm, sizeof nm); // bank 14, copies into this RAM buffer
    gotoxy(CV_NAME_COL, CV_NAME_ROW); printf("%s", nm);
    for (i = 0u; i < CV_BODY_LINES; i++) {
        if (!n->line[i]) break;
        gotoxy(CV_BODY_COL, (uint8_t)(CV_BODY_ROW + i)); printf("%s", n->line[i]);
    }
    for (i = 0u; i < n->opt_count; i++) {
        gotoxy(CV_OPT_COL, (uint8_t)(CV_OPT_ROW + i)); printf("%s", n->opt_label[i]);
    }
    cursor_update();
}

static void enter_node(uint8_t node) {
    cv_node = node;
    cv_sel = 0u;
    draw_node();
}

BANKREF(state_conversation_enter)
void state_conversation_enter(void) BANKED {
    BANK_DBG("CV_enter");
    cv_prev_j = joypad(); // swallow the direction still held from the bump that opened this
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    set_bkg_palette(0u, 1u, pal_conv_bg);
    set_bkg_palette(PAL_PILLAR_BG, 1u, pal_conv_cloak);
    cv_town = (floor_num >= TOWN_FLOOR_BASE) ? (uint8_t)(floor_num - TOWN_FLOOR_BASE) : 0u;
    if (cv_town >= TOWN_COUNT) cv_town = 0u;
    cv_npc = (pending_talk_npc < MAX_TOWN_NPCS) ? pending_talk_npc : 0u;
    portrait_upload();
    enter_node(dlg_root_for_npc(cv_npc));
}

BANKREF(state_conversation_tick)
void state_conversation_tick(void) BANKED {
    const DlgNode *n = &dlg_nodes[cv_node];
    uint8_t j = joypad();
    uint8_t e = (uint8_t)(j & (uint8_t)~cv_prev_j);
    cv_prev_j = j;

    if (e & J_UP) {
        cv_sel = (cv_sel == 0u) ? (uint8_t)(n->opt_count - 1u) : (uint8_t)(cv_sel - 1u);
        cursor_update();
    } else if (e & J_DOWN) {
        cv_sel = (uint8_t)((cv_sel + 1u) % n->opt_count);
        cursor_update();
    } else if (e & J_A) {
        uint8_t act = n->opt_action[cv_sel];
        if (act == DLG_ACT_GOTO) {
            enter_node(n->opt_arg[cv_sel]);
        } else if (act == DLG_ACT_TRADE) {
            // Hand off to the existing trade menu, which runs unchanged and returns to gameplay on
            // its own B. pending_talk_npc stays set — state_talk.c is what clears it.
            entity_sprites_inv_cursor_hide();
            next_state = STATE_TALK;
            wait_vbl_done();
            return;
        } else {
            goto leave;
        }
    } else if (e & J_B) {
        goto leave;
    }

    wait_vbl_done();
    return;

leave:
    entity_sprites_inv_cursor_hide();
    pending_talk_npc = 255u;
    next_state = STATE_GAMEPLAY;
    wait_vbl_done();
}
