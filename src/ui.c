#include "ui.h"
#include "debug_bank.h"
#include "globals.h"
#include "defs.h"
#include "lcd.h"         // lcd_gameplay_active for title vs play raster
#include "music.h"       // mute BGM + footfalls during floor generation
#include "render.h"      // load_palettes — restore sprite CRAM after title fire uses OCP7
#include "seed_entropy.h" // deterministic-ish random seed from hardware jitter
#include "enemy.h"       // inspect: enemy_defs, enemy_hp, enemy_level, trait/name
#include <gb/cgb.h>
#include <gb/gb.h>
#include <gb/hardware.h> // DEVICE_SPRITE_PX_OFFSET_* — same convention as entity_sprites OAM X

#define SEED_WORDS_N 40 // vocabulary size per category; seed maps to triple index
#define COMBAT_LOG_LINES 3u
#define COMBAT_LOG_LEN   20u

#define UI_TITLE_TORCH_OAM_L  20u // one 8×8 sprite per torch (no 2×2 upscale)
#define UI_TITLE_TORCH_OAM_R  21u
#define UI_TITLE_FIRE_FIRST   22u
#define UI_TITLE_FIRE_COUNT   8u
#define PAL_TITLE_FIRE        7u  // OCP7: orange flame; gameplay restores via load_palettes in ui_title_style_end

static const palette_color_t ui_title_bkg_pal[] = { // BKG pal 0: dark red field + light text (font pen 3 / paper 0)
    RGB(5, 0, 1),
    RGB(12, 0, 4),
    RGB(20, 6, 8),
    RGB(30, 28, 26),
};
static const palette_color_t ui_default_bkg_pal0[] = { RGB(0, 0, 0), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31) };
static const palette_color_t ui_title_fire_pal[] = { // OCP7 during menu only
    RGB(0, 0, 0), RGB(26, 6, 0), RGB(31, 16, 2), RGB(31, 26, 8),
};

static uint8_t ui_title_torch_lx, ui_title_torch_rx, ui_title_torch_ty; // fire spawns from torch tops
static uint8_t ui_title_fire_y[UI_TITLE_FIRE_COUNT];
static uint8_t ui_title_fire_x[UI_TITLE_FIRE_COUNT];
static uint8_t ui_title_fire_ttl[UI_TITLE_FIRE_COUNT]; // 0 = slot free

static void ui_title_torch_hide(void) {
    uint8_t i;
    move_sprite(UI_TITLE_TORCH_OAM_L, 0u, 0u);
    move_sprite(UI_TITLE_TORCH_OAM_R, 0u, 0u);
    for (i = UI_TITLE_FIRE_FIRST; i < UI_TITLE_FIRE_FIRST + UI_TITLE_FIRE_COUNT; i++) move_sprite(i, 0u, 0u);
}

static void ui_title_fire_init(void) {
    uint8_t i;
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) ui_title_fire_ttl[i] = 0u;
}

static void ui_title_menu_border_put(uint8_t x, uint8_t y, uint16_t *seq) { // J7/L4 alternate on BKG pal 0 (title ramp)
    uint8_t off = ((*seq) & 1u) ? TILE_FLOOR_DECO_4 : TILE_LOADING_SKULL;
    uint8_t v = (uint8_t)(TILESET_VRAM_OFFSET + off);
    (*seq)++;
    gotoxy(x, y);
    set_bkg_tiles(x, y, 1, 1, &v);
    set_bkg_attribute_xy(x, y, 0u);
    VBK_REG = VBK_TILES;
}

static void ui_title_menu_border_draw(void) { // 20×18 view: one tile deep; clockwise from top-left
    uint16_t seq = 0u;
    uint8_t x;
    int8_t y;
    for (x = 0u; x < 20u; x++) ui_title_menu_border_put(x, 0u, &seq);
    for (y = 1; y <= 16; y++) ui_title_menu_border_put(19u, (uint8_t)y, &seq);
    for (x = 19u;; x--) {
        ui_title_menu_border_put(x, 17u, &seq);
        if (x == 0u) break;
    }
    for (y = 16; y >= 1; y--) ui_title_menu_border_put(0u, (uint8_t)y, &seq);
}

static void ui_title_torch_place(uint8_t bkg_text_row) {
    uint8_t tt = (uint8_t)(TILESET_VRAM_OFFSET + TILE_LIGHT_3); // C3 torch art
    uint8_t ty = (uint8_t)((uint16_t)bkg_text_row * 8u + 4u + 16u); // same baseline as original single-torch title
    uint8_t lx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + 24u); // OAM X = 8+screen px; 24px from left = well right of skull border
    uint8_t rx = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (160u - 8u - 24u)); // mirror: 24px margin from right visible edge
    uint8_t p = (uint8_t)(PAL_PLAYER & 7u);
    ui_title_torch_lx = lx;
    ui_title_torch_rx = rx;
    ui_title_torch_ty = ty;
    set_sprite_tile(UI_TITLE_TORCH_OAM_L, tt);
    set_sprite_tile(UI_TITLE_TORCH_OAM_R, tt);
    set_sprite_prop(UI_TITLE_TORCH_OAM_L, p);
    set_sprite_prop(UI_TITLE_TORCH_OAM_R, (uint8_t)(p | S_FLIPX));
    move_sprite(UI_TITLE_TORCH_OAM_L, lx, ty);
    move_sprite(UI_TITLE_TORCH_OAM_R, rx, ty);
}

static void ui_title_style_begin(uint8_t bkg_text_row) {
    set_bkg_palette(0u, 1u, ui_title_bkg_pal);
    set_sprite_palette(PAL_TITLE_FIRE, 1u, ui_title_fire_pal);
    ui_title_fire_init();
    ui_title_torch_place(bkg_text_row);
    SHOW_SPRITES;
}

static void ui_title_style_end(void) {
    uint8_t sb;
    set_bkg_palette(0u, 1u, ui_default_bkg_pal0);
    ui_title_torch_hide();
    sb = _current_bank;
    BANK_DBG("UI_pal_to2");
    SWITCH_ROM(2);
    BANK_DBG("UI_pal_at2");
    load_palettes();
    SWITCH_ROM(sb);
    BANK_DBG("UI_pal_ret");
}

static void ui_title_try_spawn_fire(uint8_t from_right, uint16_t fc) {
    uint8_t i;
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) {
        if (ui_title_fire_ttl[i] != 0u) continue;
        {
            uint8_t base_x = from_right
                ? (uint8_t)(ui_title_torch_rx + 3u + (uint8_t)(fc & 7u))
                : (uint8_t)(ui_title_torch_lx + 3u + (uint8_t)((fc >> 1) & 7u));
            ui_title_fire_x[i] = base_x;
            ui_title_fire_y[i] = (uint8_t)(ui_title_torch_ty - 2u);
            ui_title_fire_ttl[i] = (uint8_t)(24u + (uint8_t)(DIV_REG & 11u));
            return;
        }
    }
}

static void ui_title_menu_anim_tick(uint16_t frame_counter) {
    uint8_t i;
    uint8_t ft = (uint8_t)(TILESET_VRAM_OFFSET + TILE_TITLE_FIRE);
    uint8_t fp = (uint8_t)(PAL_TITLE_FIRE & 7u);
    for (i = 0u; i < UI_TITLE_FIRE_COUNT; i++) {
        if (ui_title_fire_ttl[i] == 0u) continue;
        {
            uint8_t y = ui_title_fire_y[i];
            int16_t nx = (int16_t)ui_title_fire_x[i];
            if (((frame_counter + (uint16_t)i) & 3u) == 1u) nx++;
            else if (((frame_counter + (uint16_t)i) & 3u) == 3u) nx--;
            if (nx < 8) nx = 8;
            if (nx > 152) nx = 152;
            y = (uint8_t)((uint16_t)y - 1u);
            if (((frame_counter + i) & 1u) == 0u) y = (uint8_t)((uint16_t)y - 1u); // ~2 px/frame average rise
            ui_title_fire_ttl[i] = (uint8_t)(ui_title_fire_ttl[i] - 1u);
            if (ui_title_fire_ttl[i] == 0u || y < 20u) {
                ui_title_fire_ttl[i] = 0u;
                move_sprite((uint8_t)(UI_TITLE_FIRE_FIRST + i), 0u, 0u);
            } else {
                ui_title_fire_y[i] = y;
                ui_title_fire_x[i] = (uint8_t)nx;
                set_sprite_tile((uint8_t)(UI_TITLE_FIRE_FIRST + i), ft);
                set_sprite_prop((uint8_t)(UI_TITLE_FIRE_FIRST + i), fp);
                move_sprite((uint8_t)(UI_TITLE_FIRE_FIRST + i), (uint8_t)nx, y);
            }
        }
    }
    if ((frame_counter & 3u) == 0u) ui_title_try_spawn_fire(0, frame_counter);
    if ((frame_counter & 3u) == 2u) ui_title_try_spawn_fire(1, frame_counter);
}

static char combat_log[COMBAT_LOG_LINES][COMBAT_LOG_LEN];

UIPanelMode ui_panel_mode = UI_PANEL_COMBAT;
static uint8_t panel_inspect_slot;

static uint8_t combat_log_any(void) {
    return combat_log[0][0] || combat_log[1][0] || combat_log[2][0];
}

void ui_combat_log_clear(void) {
    uint8_t i, j;
    for (i = 0; i < COMBAT_LOG_LINES; i++)
        for (j = 0; j < COMBAT_LOG_LEN; j++) combat_log[i][j] = 0;
}

void ui_combat_log_push(const char *line) {
    uint8_t r, i;
    for (r = 0; r < COMBAT_LOG_LINES - 1u; r++) // shift lines up (drop oldest)
        for (i = 0; i < COMBAT_LOG_LEN; i++)
            combat_log[r][i] = combat_log[r + 1u][i];
    for (i = 0; i < COMBAT_LOG_LEN; i++) combat_log[COMBAT_LOG_LINES - 1u][i] = 0;
    for (i = 0; i < COMBAT_LOG_LEN - 1u && line[i]; i++)
        combat_log[COMBAT_LOG_LINES - 1u][i] = line[i];
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

static void win_puts_row_pad_cols(uint8_t y, const char *s, uint8_t pal, uint8_t cols) {
    uint8_t x = 0;
    while (*s && x < cols) win_putc_pal(x++, y, *s++, pal);
    while (x < cols) win_put_space(x++, y);
}

static void win_clear_row(uint8_t win_y, uint8_t pal) {
    uint8_t x;
    for (x = 0; x < UI_PANEL_COLS; x++) win_putc_pal(x, win_y, ' ', pal);
}

static void ui_draw_top_hud(void) { // bottom window row: L:♥×5 HP% XP% FLOORdd
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
            win_putc_pal(tx, hy, '_', PAL_LIFE_UI);
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
    win_putc_pal(tx++, hy, (char)('0' + xp_pct / 10u), PAL_XP_UI);
    win_putc_pal(tx++, hy, (char)('0' + xp_pct % 10u), PAL_XP_UI);
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

static void ui_draw_combat_panel(void) {
    uint8_t i;
    if (!combat_log_any()) {
        ui_draw_seed_words(run_seed, UI_PANEL_WIN_Y0, UI_PANEL_WIN_Y1);
        win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
    } else {
        for (i = 0; i < COMBAT_LOG_LINES; i++)
            win_puts_row_pad_cols((uint8_t)(UI_PANEL_WIN_Y0 + i), combat_log[i], PAL_UI, UI_PANEL_COLS);
    }
}

static void ui_draw_inspect_panel(void) {
    uint8_t slot = panel_inspect_slot;
    uint8_t t, x;
    const char *nm;
    if (slot >= num_enemies || !enemy_alive[slot]) {
        win_clear_row(UI_PANEL_WIN_Y0, PAL_UI);
        win_clear_row(UI_PANEL_WIN_Y1, PAL_UI);
        win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
        return;
    }
    t = enemy_type[slot];
    nm = enemy_type_short_name(t);
    win_puts_row_pad_cols(UI_PANEL_WIN_Y0, nm, PAL_UI, UI_PANEL_COLS);
    { // HP N/M — one line under name (text rows 0–2; HUD draws on row 3 after)
        uint8_t hp = enemy_hp[slot], mhp = enemy_effective_max_hp(t);
        x = 0;
        win_putc_pal(x++, UI_PANEL_WIN_Y1, 'H', PAL_UI);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, 'P', PAL_UI);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, ' ', PAL_UI);
        win_put_uint8(x, UI_PANEL_WIN_Y1, hp, 3, PAL_UI); x = (uint8_t)(x + 3u);
        win_putc_pal(x++, UI_PANEL_WIN_Y1, '/', PAL_UI);
        win_put_uint8(x, UI_PANEL_WIN_Y1, mhp, 3, PAL_UI); x = (uint8_t)(x + 3u);
        while (x < UI_PANEL_COLS) win_putc_pal(x++, UI_PANEL_WIN_Y1, ' ', PAL_UI);
    }
    win_clear_row(UI_PANEL_WIN_Y2, PAL_UI);
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

void window_ui_show(void) { // 3 text rows + bottom HUD row; WIN from UI_WINDOW_Y_START down
    uint8_t wx, wy;
    WX_REG = 7u;
    WY_REG = UI_WINDOW_Y_START;
    fill_win_rect(0, 0, 32, 4u, 0u); // rows 0..3
    for (wy = 0; wy < 4u; wy++)
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
    music_loading_screen_set(1u);
    gotoxy(5, 8);
    printf("Descending");
    set_sprite_tile(UI_LOAD_SKULL_OAM_L, tt);
    set_sprite_tile(UI_LOAD_SKULL_OAM_R, tt);
    set_sprite_prop(UI_LOAD_SKULL_OAM_L, (uint8_t)(PAL_CORPSE & 7u)); // grey ramp slot 0 — keep PAL_UI (6) for non-enemy text only
    set_sprite_prop(UI_LOAD_SKULL_OAM_R, (uint8_t)(PAL_CORPSE & 7u));
    SHOW_SPRITES;
    move_sprite(UI_LOAD_SKULL_OAM_L, 32u, 80u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 136u, 80u);
}

void ui_loading_screen_end(void) {
    music_loading_screen_set(0u);
    ui_loading_active = 0u;
    move_sprite(UI_LOAD_SKULL_OAM_L, 0u, 0u);
    move_sprite(UI_LOAD_SKULL_OAM_R, 0u, 0u);
}

void ui_draw_bottom_rows(void) {
    switch (ui_panel_mode) {
        case UI_PANEL_COMBAT:   ui_draw_combat_panel();   break;
        case UI_PANEL_INSPECT:  ui_draw_inspect_panel();  break;
        default:                ui_draw_combat_panel();   break;
    }
    ui_draw_top_hud();
}

void ui_draw_seed_words(uint16_t seed, uint8_t win_y_desc_noun, uint8_t win_y_place) {
    uint8_t x, d, n, p;
    run_seed_to_triple(seed, &d, &n, &p);
    put_word5_win(0u, win_y_desc_noun, seed_words_desc[d]);
    put_word5_win(6u, win_y_desc_noun, seed_words_noun[n]);
    for (x = 11; x < UI_PANEL_COLS; x++) win_put_space(x, win_y_desc_noun);
    put_word5_win(0u, win_y_place, seed_words_place[p]);
    for (x = 5; x < UI_PANEL_COLS; x++) win_put_space(x, win_y_place);
}

void ui_panel_show_combat(void) { ui_panel_mode = UI_PANEL_COMBAT; }

void ui_panel_show_inspect(uint8_t enemy_slot) {
    panel_inspect_slot = enemy_slot;
    ui_panel_mode = UI_PANEL_INSPECT;
}

uint8_t ui_panel_inspect_slot(void) { return panel_inspect_slot; }

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
    BANK_DBG("UI:seed");
    ui_title_style_begin(1u);

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
                ui_title_style_end();
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
        ui_title_menu_anim_tick(frame_counter);
    }
}

uint16_t title_screen(uint16_t entropy_hint) { // blocking until START or SELECT path returns a seed
    uint8_t  blink_counter = 0, blink_visible = 1, prev_j = 0;
    uint16_t frame_counter = 0;

    lcd_gameplay_active = 0u;
    window_ui_hide();
    wait_vbl_done();
    lcd_clear_display();
    BANK_DBG("UI_title");
    ui_title_style_begin(7u);
    ui_title_menu_border_draw();
    gotoxy(4,  7); printf("Mara's Abyss");
    gotoxy(3, 12); printf("SELECT=seed words");

    while (1) {
        uint8_t  j    = joypad();
        uint8_t  edge = (uint8_t)(j & (uint8_t)~prev_j);
        uint16_t mixed = seed_entropy_random_seed(entropy_hint, frame_counter); // fresh candidate each frame for START
        if (edge & J_START) {
            ui_title_style_end();
            return mixed;
        }
        if (edge & J_SELECT) {
            ui_title_style_end();
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
        ui_title_menu_anim_tick(frame_counter);
        prev_j = j;
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
