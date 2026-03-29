#include "ui.h"          // title_screen, game_over_screen, playfield HUD
#include "lcd.h"         // lcd_gameplay_active for title vs play raster
#include "seed_entropy.h" // deterministic-ish random seed from hardware jitter

#define UI_HUD_WIN_Y 0u // window tilemap row 0 = HUD (ISR shows window at lines 0–7)

#define SEED_WORDS_N 40 // vocabulary size per category; seed maps to triple index
#define UI_COMBAT_LOG_CAP 4u
#define UI_COMBAT_LOG_W  GRID_W

static char combat_log[UI_COMBAT_LOG_CAP][UI_COMBAT_LOG_W + 1u];
static uint8_t combat_log_head;  // next write index
static uint8_t combat_log_count; // 0..UI_COMBAT_LOG_CAP valid entries

void ui_combat_log_clear(void) {
    uint8_t i, j;
    combat_log_head = 0;
    combat_log_count = 0;
    for (i = 0; i < UI_COMBAT_LOG_CAP; i++)
        for (j = 0; j <= UI_COMBAT_LOG_W; j++) combat_log[i][j] = 0;
}

void ui_combat_log_push(const char *s) {
    uint8_t i = 0;
    char *dst = combat_log[combat_log_head];
    while (i < UI_COMBAT_LOG_W && s[i]) {
        dst[i] = s[i];
        i++;
    }
    dst[i] = '\0';
    combat_log_head = (uint8_t)((combat_log_head + 1u) % UI_COMBAT_LOG_CAP);
    if (combat_log_count < UI_COMBAT_LOG_CAP) combat_log_count++;
}

static const char *const seed_words_desc[SEED_WORDS_N] = { // first word line (adjective-ish)
    "ASHEN","BLEAK","BLIND","BLOOD","BLUNT","BONED","BURNT","COLD","CRUEL","CURST",
    "DARK","DEAD","DEEP","DENSE","DIRE","DREAD","DREAR","DULL","DUSK","DUSTY",
    "EMBER","FELL","FETID","FOUL","GAUNT","GRIM","IRON","LOST","LUNAR","MURKY",
    "PALE","ROT","SHADE","SHORN","STARK","STILL","SUNK","TOXIC","VILE","VOID"
};
static const char *const seed_words_noun[SEED_WORDS_N] = { // second word on row 1
    "ASH","BANE","BLOT","BONE","BRIAR","BRIER","COIL","CROW","CRYPT","DUST",
    "EMBER","FANG","FLAME","FROND","FROST","FUNGI","HORN","IRON","LARVA","MIRE",
    "MIST","MOSS","MOTH","MURK","PITH","ROOT","RUIN","SHADE","SKULL","SLIME",
    "SMOKE","SPINE","SPORE","STONE","THORN","TIDE","TOMB","VENOM","WILT","WORM"
};
static const char *const seed_words_place[SEED_WORDS_N] = { // row 2 place name
    "ABYSS","BOG","BRINK","CAIRN","CAVES","CHASM","CRAGS","CRYPT","DEEP","DELL",
    "DELVE","DUNES","FELLS","FLATS","GORGE","GULCH","HEATH","KEEP","MARSH","MIRE",
    "MOORS","MOUND","NOOK","PEAKS","PITS","RIFT","RUINS","SANDS","SHELF","SHORE",
    "SLOPE","SPIRE","STEPS","TOMB","VALE","VAULT","WASTE","WEALD","WILDS","WOOD"
};

static void set_win_attribute_xy(uint8_t x, uint8_t y, uint8_t a) { // CGB palette for window map
    VBK_REG = VBK_ATTRIBUTES;
    set_win_tile_xy(x, y, a);
    VBK_REG = VBK_TILES;
}

static void win_putc(uint8_t x, uint8_t y, char c) { // IBM font tile indices match setchar (tile 0 = space)
    uint8_t t = (uint8_t)((uint8_t)((unsigned char)c - 32u));
    set_win_tile_xy(x, y, t);
    set_win_attribute_xy(x, y, PAL_UI);
}

static void win_putc_pal(uint8_t x, uint8_t y, char c, uint8_t pal) { // char with explicit CGB palette
    uint8_t t = (uint8_t)((uint8_t)((unsigned char)c - 32u));
    set_win_tile_xy(x, y, t);
    set_win_attribute_xy(x, y, pal);
}

static void win_puts(uint8_t x, uint8_t y, const char *s, uint8_t pal) { // null-terminated string to window row
    while (*s) { win_putc_pal(x++, y, *s++, pal); }
}

static void win_put_uint8(uint8_t x, uint8_t y, uint8_t v, uint8_t width, uint8_t pal) { // right-justified decimal
    char dig[3];
    uint8_t i = 0, pad;
    if (v == 0) { dig[i++] = '0'; }
    else { while (v) { dig[i++] = (char)('0' + (v % 10u)); v /= 10u; } }
    for (pad = i; pad < width; pad++) win_putc_pal(x++, y, ' ', pal); // space-pad leading
    while (i--) win_putc_pal(x++, y, dig[i], pal);
}

static void win_put_space(uint8_t x, uint8_t y) { // blank space tile + UI palette
    set_win_tile_xy(x, y, 0u);
    set_win_attribute_xy(x, y, PAL_UI);
}

static void win_puts_row_pad(uint8_t y, const char *s, uint8_t pal) { // GRID_W cols, space-pad rest
    uint8_t x = 0;
    while (*s && x < GRID_W) win_putc_pal(x++, y, *s++, pal);
    while (x < GRID_W) win_put_space(x++, y);
}

static void ui_draw_combat_bottom_rows(void) { // last two log lines on window rows 1–2 (newest on row 2)
    uint8_t last = (uint8_t)((combat_log_head + UI_COMBAT_LOG_CAP - 1u) % UI_COMBAT_LOG_CAP);
    uint8_t prev = (uint8_t)((combat_log_head + UI_COMBAT_LOG_CAP - 2u) % UI_COMBAT_LOG_CAP);
    if (combat_log_count >= 2u) win_puts_row_pad(1, combat_log[prev], PAL_UI);
    else {
        uint8_t x;
        for (x = 0; x < GRID_W; x++) win_put_space(x, 1);
    }
    win_puts_row_pad(2, combat_log[last], PAL_UI);
}

static void put_word5(uint8_t x, uint8_t y, const char *s) { // fixed 5-char word into BKG via setchar
    uint8_t i;
    for (i = 0; i < 5; i++) { gotoxy((uint8_t)(x+i), y); setchar(s[i] ? s[i] : ' '); } // pad short strings
}

static void put_word5_win(uint8_t x, uint8_t y, const char *s) { // same for window tilemap rows 0–1
    uint8_t i;
    for (i = 0; i < 5; i++) { win_putc((uint8_t)(x+i), y, s[i] ? s[i] : ' '); }
}

static void run_seed_to_triple(uint16_t seed, uint8_t *d, uint8_t *n, uint8_t *p) { // same logic as ui_draw_seed_words / picker
    uint16_t s = seed;
    if (s < 1u) s = 1u;
    if (s > 64000u) s = 64000u;
    s--;
    *d = (uint8_t)(s % 40u);
    *n = (uint8_t)((s / 40u) % 40u);
    *p = (uint8_t)((s / 1600u) % 40u);
}

void window_ui_show(void) { // HUD row 0 + seed rows 1–2: ISR toggles window on/off per band
    uint8_t wx, wy;
    WX_REG = 7u;
    WY_REG = 0u; // window starts at line 0; ISR hides it at line 8 and re-shows at bottom
    fill_win_rect(0, 0, 32, 3, 0u); // tile 0 = font space (matches win_put_space)
    for (wy = 0; wy < 3u; wy++)
        for (wx = 0; wx < 32u; wx++)
            set_win_attribute_xy(wx, wy, PAL_UI);
}

void window_ui_hide(void) {
    HIDE_WIN;
}

#define UI_LOAD_SKULL_OAM_L 38u // above SP_ENEMY_BASE+NUM_ENEMIES
#define UI_LOAD_SKULL_OAM_R 39u

volatile uint8_t ui_loading_active;
static volatile uint8_t ui_load_phase;

static const int8_t ui_load_bob12[12] = { 0, 1, 2, 2, 1, 0, -1, -2, -2, -1, 0, 0 }; // one gentle bounce / 12 VBlanks

void ui_loading_vblank(void) {
    int8_t dy;
    uint8_t ph;
    uint8_t hw_y;
    if (!ui_loading_active) return;
    ph = ui_load_phase++;
    dy = ui_load_bob12[ph % 12u];
    hw_y = (uint8_t)((int16_t)64 + 16 + (int16_t)dy); // screen row ~64px; hardware OAM Y includes +16
    move_sprite(UI_LOAD_SKULL_OAM_L, (uint8_t)(24u + 8u), hw_y); // flanks 10-char label centered in GRID_W
    move_sprite(UI_LOAD_SKULL_OAM_R, (uint8_t)(128u + 8u), hw_y);
}

void ui_loading_screen_begin(void) {
    uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_LOADING_SKULL);
    ui_load_phase = 0;
    ui_loading_active = 1u;
    gotoxy(5, 8);
    printf("Descending");
    set_sprite_tile(UI_LOAD_SKULL_OAM_L, tt);
    set_sprite_tile(UI_LOAD_SKULL_OAM_R, tt);
    set_sprite_prop(UI_LOAD_SKULL_OAM_L, (uint8_t)(PAL_UI & 7u));
    set_sprite_prop(UI_LOAD_SKULL_OAM_R, (uint8_t)(PAL_UI & 7u));
    SHOW_SPRITES;
    move_sprite(UI_LOAD_SKULL_OAM_L, 32u, 80u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 136u, 80u);
}

void ui_loading_screen_end(void) {
    ui_loading_active = 0u;
    move_sprite(UI_LOAD_SKULL_OAM_L, 0u, 0u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 0u, 0u);
}

void ui_draw_top_hud(void) { // L:♥×5 HP%XP%% FLOORdd — window row 0 (fits GRID_W=20)
    uint8_t hy = UI_HUD_WIN_Y, tx = 0;
    uint8_t k, pct = (uint8_t)((uint16_t)player_hp * 100u / player_hp_max);
    uint8_t pct8 = pct, xp_pct;
    uint8_t vram;

    win_putc_pal(tx++, hy, 'L', PAL_LIFE_UI);
    win_putc_pal(tx++, hy, ':', PAL_LIFE_UI);
    for (k = 0; k < LIFE_BAR_LEN; k++) {
        if (pct >= (uint8_t)(20u * (k + 1u))) {
            vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_HEART_FULL);
            set_win_tile_xy(tx, hy, vram);
            set_win_attribute_xy(tx, hy, PAL_LIFE_UI);
        } else if (pct >= (uint8_t)(20u * k + 10u)) {
            vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_HEART_HALF);
            set_win_tile_xy(tx, hy, vram);
            set_win_attribute_xy(tx, hy, PAL_LIFE_UI);
        } else {
            win_putc_pal(tx, hy, '_', PAL_LIFE_UI); // empty bar segment matches life color
        }
        tx++;
    }
    win_put_uint8(tx, hy, pct8, 3, PAL_LIFE_UI);
    tx = (uint8_t)(tx + 3u);
    win_putc_pal(tx++, hy, '%', PAL_LIFE_UI);
    {
        uint16_t next_level_xp = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
        xp_pct = (player_xp >= next_level_xp) ? 99u : (uint8_t)((player_xp * 100u) / next_level_xp);
    }
    win_putc_pal(tx++, hy, 'X', PAL_XP_UI);
    win_putc_pal(tx++, hy, 'P', PAL_XP_UI);
    {
        win_putc_pal(tx++, hy, (char)('0' + xp_pct / 10u), PAL_XP_UI);
        win_putc_pal(tx++, hy, (char)('0' + xp_pct % 10u), PAL_XP_UI);
    }
    win_putc_pal(tx++, hy, '%', PAL_XP_UI);
    vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_FLOOR_L);
    set_win_tile_xy(tx, hy, vram);
    set_win_attribute_xy(tx++, hy, PAL_UI);
    vram = (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_FLOOR_R);
    set_win_tile_xy(tx, hy, vram);
    set_win_attribute_xy(tx++, hy, PAL_UI);
    win_putc_pal(tx++, hy, (char)('0' + floor_num / 10u), PAL_UI);
    win_putc_pal(tx++, hy, (char)('0' + floor_num % 10u), PAL_UI);
    while (tx < GRID_W) win_put_space(tx++, hy);
}

void ui_draw_bottom_rows(void) { // window layer rows 0–1 in win map (screen rows 16–17)
    if (combat_log_count) ui_draw_combat_bottom_rows();
    else                  ui_draw_seed_words(run_seed, 0, 0, 0);
}

void ui_draw_seed_words(uint16_t seed, uint8_t hvx, uint8_t b1vy, uint8_t b2vy) { // seed words → window rows 1–2 (row 0 = HUD)
    uint8_t x, d, n, p;
    (void)b1vy;
    (void)b2vy;
    run_seed_to_triple(seed, &d, &n, &p);
    put_word5_win(hvx,       1, seed_words_desc[d]);
    put_word5_win((uint8_t)(hvx + 6), 1, seed_words_noun[n]);
    for (x = 11; x < GRID_W; x++) { win_put_space((uint8_t)(hvx + x), 1); }
    put_word5_win(hvx,       2, seed_words_place[p]);
    for (x = 5; x < GRID_W; x++) { win_put_space((uint8_t)(hvx + x), 2); }
}

static uint16_t input_seed_words_screen(uint16_t initial_seed, uint16_t entropy_hint) { // interactive seed picker
    uint8_t  word_pos = 0, prev_j = 0; // word_pos 0..2 selects which word the caret edits
    uint16_t frame_counter = 0;              // feeds entropy mixer each frame
    uint16_t s = initial_seed ? initial_seed : 1u;
    if (s > 64000u) s = 64000u;
    s--;
    uint8_t d = (uint8_t)(s % 40u);             // current indices
    uint8_t n = (uint8_t)((s / 40u) % 40u);
    uint8_t p = (uint8_t)((s / 1600u) % 40u);

    wait_vbl_done();
    lcd_clear_display();

    while (1) {
        gotoxy(3, 1); printf("SEED WORDS");
        put_word5(1,  3, seed_words_desc[d]);
        gotoxy(6,  3); setchar(' ');
        put_word5(7,  3, seed_words_noun[n]);
        gotoxy(12, 3); setchar(' ');
        put_word5(13, 3, seed_words_place[p]);
        gotoxy(1,  4); printf("     ");
        gotoxy(6,  4); printf("     ");
        gotoxy(11, 4); printf("     ");
        gotoxy(1 + word_pos * 6, 4); setchar('^'); // caret under active word block
        gotoxy(1, 6); printf("L/R word");
        gotoxy(1, 7); printf("U/D scroll");
        gotoxy(1, 8); printf("New seed: (A)");
        gotoxy(1, 9); printf("START=play");
        {
            uint8_t j    = joypad();
            uint8_t edge = (uint8_t)(j & (uint8_t)~prev_j); // buttons newly pressed this frame
            if (edge & J_START) {
                uint16_t fs = (uint16_t)(1u + (uint16_t)d + 40u*(uint16_t)n + 1600u*(uint16_t)p); // pack triple back to seed
                if (!fs) fs = 1;
                return fs;
            }
            if (edge & J_A) {
                uint16_t t = seed_entropy_random_seed(entropy_hint, frame_counter); // reroll all three indices
                if (t > 64000u) t = 64000u; t--;
                d = (uint8_t)(t % 40u); n = (uint8_t)((t/40u) % 40u); p = (uint8_t)((t/1600u) % 40u);
            }
            if (edge & J_LEFT)  word_pos = (word_pos == 0) ? 2 : (uint8_t)(word_pos-1); // wrap caret
            if (edge & J_RIGHT) word_pos = (uint8_t)((word_pos+1) % 3);
            if (edge & J_UP) {
                if      (word_pos == 0) d = (uint8_t)((d+1) % SEED_WORDS_N);
                else if (word_pos == 1) n = (uint8_t)((n+1) % SEED_WORDS_N);
                else                    p = (uint8_t)((p+1) % SEED_WORDS_N);
            }
            if (edge & J_DOWN) {
                if      (word_pos == 0) d = (uint8_t)(d == 0 ? SEED_WORDS_N-1 : d-1);
                else if (word_pos == 1) n = (uint8_t)(n == 0 ? SEED_WORDS_N-1 : n-1);
                else                    p = (uint8_t)(p == 0 ? SEED_WORDS_N-1 : p-1);
            }
            prev_j = j;
        }
        frame_counter++;
        wait_vbl_done();
    }
}

uint16_t title_screen(uint16_t entropy_hint) { // blocking until START or SELECT path returns a seed
    uint8_t  blink_counter = 0, blink_visible = 1, prev_j = 0;
    uint16_t frame_counter = 0;

    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    gotoxy(4,  7); printf("Mara's Abyss");
    gotoxy(3, 12); printf("SELECT=seed words");

    while (1) {
        uint8_t  j    = joypad();
        uint8_t  edge = (uint8_t)(j & (uint8_t)~prev_j);
        uint16_t mixed = seed_entropy_random_seed(entropy_hint, frame_counter); // fresh candidate each frame for START
        if (edge & J_START) return mixed; // instant play with time-varying seed
        if (edge & J_SELECT) {
            uint16_t seed = input_seed_words_screen(mixed, entropy_hint); // user-defined mnemonic seed
            if (!seed) seed = 1;
            return seed;
        }
        blink_counter++;
        if (blink_counter >= 30) { // ~0.5s at 60Hz VBlank loop
            blink_counter = 0; blink_visible ^= 1;
            gotoxy(5, 10);
            if (blink_visible) printf("PRESS START");
            else               printf("           ");
        }
        frame_counter++;
        wait_vbl_done();
        prev_j = j;
    }
}

void ui_start_menu_screen(void) { // placeholder full-screen START menu; expand later
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    gotoxy(4,  8); printf("START MENU");
    gotoxy(2, 10); printf("START=resume");
    while (1) {
        if (joypad() & J_START) break;
        wait_vbl_done();
    }
}

void game_over_screen(void) { // blocks until START
    uint8_t d, n, p;
    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    gotoxy(5,  6); printf("GAME OVER");
    run_seed_to_triple(run_seed, &d, &n, &p);
    put_word5(0, 9, seed_words_desc[d]);
    put_word5(6, 9, seed_words_noun[n]);
    put_word5(0, 10, seed_words_place[p]);
    gotoxy(4, 13); printf("START=again");
    while (1) {
        if (joypad() & J_START) break; // no edge detect: holding START still works
        wait_vbl_done();
    }
}
