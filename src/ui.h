#ifndef UI_H
#define UI_H

#include <gbdk/platform.h>
#include "defs.h"

BANKREF_EXTERN(ui)

typedef enum {
    UI_PANEL_COMBAT   = 0,
    UI_PANEL_PERF     = 1,
    UI_PANEL_INSPECT  = 2,
} UIPanelMode;

#define SEED_WORDS_N 40u

void     run_seed_to_triple(uint16_t seed, uint8_t *d, uint8_t *n, uint8_t *p) BANKED;

uint16_t title_screen(uint16_t entropy_hint) BANKED;
void     ui_draw_bottom_rows(void) BANKED;
void     ui_draw_seed_words(uint16_t seed, uint8_t win_y) BANKED;
void     window_ui_show(void) BANKED;
void     window_ui_hide(void) BANKED;
void     ui_combat_log_clear(void) BANKED;
void     ui_combat_log_push(const char *line) BANKED;           // window palette PAL_UI
void     ui_combat_log_push_pal(const char *line, uint8_t pal) BANKED; // CGB BGP palette index (e.g. PAL_XP_UI_BG)
void     ui_combat_log_push_gold_suffix(const char *line, uint8_t gold_from) BANKED; // PAL_UI up to gold_from, then PAL_XP_UI_BG
void     ui_confirm_prompt_push(uint8_t kind, uint8_t aux) BANKED; // zone-confirm prompt line (CONFIRM_* in defs.h); aux = dungeon/town id

// combat-side text formatters — live in bank 5 (UI) to keep HOME small; BANKED so combat.c (HOME) can far-call them
void     ui_push_combat_log(uint8_t type_idx, uint8_t dmg, uint8_t hp_remaining_for_pct, uint8_t is_crit) BANKED;
void     ui_push_combat_log_shield_burn(uint8_t type_idx, uint8_t dmg, uint8_t hp_remaining_for_pct) BANKED; // "NAME burned -N" (+ optional %); lethal hit uses dmg only
void     ui_push_xp_gain_line(uint8_t amt) BANKED;
void     ui_push_level_up_line(uint8_t new_level) BANKED;
uint8_t  ui_combat_log_tick_quiet_turn(void) BANKED; // returns 1 if log was reclaimed (caller should redraw UI)
void     ui_panel_show_combat(void) BANKED;
void     ui_panel_toggle_perf(void) BANKED;
void     ui_panel_show_inspect(uint8_t enemy_slot) BANKED;
uint8_t  ui_panel_inspect_slot(void) BANKED;

void     ui_game_over_put_seed_words(uint8_t d, uint8_t n, uint8_t p) BANKED;
void     ui_map_put_seed_line(uint8_t x, uint8_t y) BANKED;

void     ui_loading_screen_begin(uint8_t ascending) BANKED;
void     ui_loading_screen_end(void) BANKED;
void     ui_loading_vblank(void); // home ROM: lcd VBL — not BANKED

#endif // UI_H
