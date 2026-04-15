#ifndef UI_H
#define UI_H

#include "defs.h"

typedef enum {
    UI_PANEL_COMBAT   = 0,
    UI_PANEL_INSPECT  = 2,
} UIPanelMode;

extern UIPanelMode ui_panel_mode;

#define SEED_WORDS_N 40u
extern const char *const seed_words_desc[SEED_WORDS_N];
extern const char *const seed_words_noun[SEED_WORDS_N];
extern const char *const seed_words_place[SEED_WORDS_N];
void     run_seed_to_triple(uint16_t seed, uint8_t *d, uint8_t *n, uint8_t *p);

uint16_t title_screen(uint16_t entropy_hint);
void     ui_draw_bottom_rows(void);
void     ui_draw_seed_words(uint16_t seed, uint8_t win_y_desc_noun, uint8_t win_y_place);
void     window_ui_show(void);
void     window_ui_hide(void);
void     ui_combat_log_clear(void);
void     ui_combat_log_push(const char *line);

void     ui_panel_show_combat(void);
void     ui_panel_show_inspect(uint8_t enemy_slot);
uint8_t  ui_panel_inspect_slot(void);

void     ui_loading_screen_begin(void);
void     ui_loading_screen_end(void);
void     ui_loading_vblank(void);

#endif // UI_H
