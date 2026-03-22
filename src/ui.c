#include "ui.h"          // title_screen, game_over_screen, playfield HUD
#include "seed_entropy.h" // deterministic-ish random seed from hardware jitter

#define UI_HUD_VX   ((uint8_t)(CAM_TX & 31u)) // HUD X aligns with viewport left in 32-wide ring
#define UI_HUD_VY   ((uint8_t)((CAM_TY + 31u) & 31u)) // row above dungeon band; SCY-8 exposes it as screen row 0
#define UI_BOT1_VY  ((uint8_t)((CAM_TY + GRID_H)      & 31u)) // first bottom UI row in ring
#define UI_BOT2_VY  ((uint8_t)((CAM_TY + GRID_H + 1u) & 31u)) // second bottom UI row (seed words line 2)

#define SEED_WORDS_N 40 // vocabulary size per category; seed maps to triple index

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
    "SMOKE","SPINE","SPORE","STONE","THORN","TIDE","TOMB","VENOM","WISP","WORM"
};
static const char *const seed_words_place[SEED_WORDS_N] = { // row 2 place name
    "ABYSS","BOG","BRINK","CAIRN","CAVES","CHASM","CRAGS","CRYPT","DEEP","DELL",
    "DELVE","DUNES","FELLS","FLATS","GORGE","GULCH","HEATH","KEEP","MARSH","MIRE",
    "MOORS","MOUND","NOOK","PEAKS","PITS","RIFT","RUINS","SANDS","SHELF","SHORE",
    "SLOPE","SPIRE","STEPS","TOMB","VALE","VAULT","WASTE","WEALD","WILDS","WOOD"
};

static void put_word5(uint8_t x, uint8_t y, const char *s) { // fixed 5-char word into tilemap via setchar
    uint8_t i;
    for (i = 0; i < 5; i++) { gotoxy((uint8_t)(x+i), y); setchar(s[i] ? s[i] : ' '); } // pad short strings
}

void ui_draw_top_hud(void) { // floor label + life bar in one ring row (coords follow camera scroll)
    uint8_t hvx = UI_HUD_VX, hvy = UI_HUD_VY;
    uint8_t x;
    gotoxy(hvx, hvy);
    printf("FLR:%02d", floor_num); // two-digit floor
    for (x = 0; x < 6; x++) { // palette for "FLR:NN"
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), hvy, PAL_UI);
        VBK_REG = 0;
    }
    gotoxy((uint8_t)((hvx + 6u) & 31u), hvy);
    setchar(' ');
    set_bkg_attribute_xy((uint8_t)((hvx + 6u) & 31u), hvy, PAL_UI);
    VBK_REG = 0;
    gotoxy((uint8_t)((hvx + 7u) & 31u), hvy);
    setchar('L'); // start "L[===] pct" life cluster
    set_bkg_attribute_xy((uint8_t)((hvx + 7u) & 31u), hvy, PAL_LIFE_UI);
    VBK_REG = 0;
    gotoxy((uint8_t)((hvx + 8u) & 31u), hvy);
    setchar('[');
    set_bkg_attribute_xy((uint8_t)((hvx + 8u) & 31u), hvy, PAL_UI);
    VBK_REG = 0;
    {
        uint8_t k, pct = (uint8_t)((uint16_t)player_hp * 100u / PLAYER_HP_MAX); // 0..100 for bar thresholds
        for (k = 0; k < LIFE_BAR_LEN; k++) { // LIFE_BAR_LEN segments × 20% each
            char c; uint8_t pal;
            if      (pct >= (uint8_t)(20u*(k+1u))) { c='='; pal=PAL_LIFE_UI; } // full segment
            else if (pct >= (uint8_t)(20u*k+10u))  { c='-'; pal=PAL_LIFE_UI; } // half segment
            else                                    { c='_'; pal=PAL_UI; }     // empty segment (dim)
            gotoxy((uint8_t)((hvx + 9u + k) & 31u), hvy);
            setchar(c);
            set_bkg_attribute_xy((uint8_t)((hvx + 9u + k) & 31u), hvy, pal);
            VBK_REG = 0;
        }
    }
    gotoxy((uint8_t)((hvx + 9u + LIFE_BAR_LEN) & 31u), hvy);
    setchar(']');
    set_bkg_attribute_xy((uint8_t)((hvx + 9u + LIFE_BAR_LEN) & 31u), hvy, PAL_UI);
    VBK_REG = 0;
    gotoxy((uint8_t)((hvx + 10u + LIFE_BAR_LEN) & 31u), hvy);
    printf("%3d%%", (uint16_t)player_hp * 100u / PLAYER_HP_MAX); // numeric percent after bar
    for (x = 10u + LIFE_BAR_LEN; x < GRID_W; x++) { // pad rest of row to UI palette to end of viewport width
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), hvy, PAL_UI);
        VBK_REG = 0;
    }
}

void ui_draw_bottom_rows(void) { // seed word lines + attribute fill
    uint8_t x;
    uint8_t hvx  = UI_HUD_VX;
    uint8_t b1vy = UI_BOT1_VY;
    uint8_t b2vy = UI_BOT2_VY;

    ui_draw_seed_words(run_seed, hvx, b1vy, b2vy); // prints words then spaces to GRID_W
    for (x = 0; x < GRID_W; x++) { // ensure palette even where printf left gaps
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), b1vy, PAL_UI);
        VBK_REG = 0;
    }
    for (x = 0; x < GRID_W; x++) { // second bottom row attributes
        set_bkg_attribute_xy((uint8_t)((hvx + x) & 31u), b2vy, PAL_UI);
        VBK_REG = 0;
    }
}

void ui_draw_seed_words(uint16_t seed, uint8_t hvx, uint8_t b1vy, uint8_t b2vy) { // inverse of input_seed_words_screen packing
    uint16_t s = seed;
    uint8_t x, d, n, p;
    if (s < 1u) s = 1u;       // clamp to valid encoded range
    if (s > 64000u) s = 64000u; // 40*40*40 + 1 max
    s--;
    d = (uint8_t)(s % 40u);                    // descriptor index
    n = (uint8_t)((s / 40u) % 40u);             // noun index
    p = (uint8_t)((s / 1600u) % 40u);           // place index (40*40 = 1600 combos per p)
    put_word5(hvx,       b1vy, seed_words_desc[d]);
    put_word5((uint8_t)(hvx + 6), b1vy, seed_words_noun[n]); // gap column between words on row 1
    for (x = 11; x < GRID_W; x++) { gotoxy((uint8_t)(hvx + x), b1vy); setchar(' '); } // clear tail of row
    put_word5(hvx,       b2vy, seed_words_place[p]);
    for (x = 5; x < GRID_W; x++) { gotoxy((uint8_t)(hvx + x), b2vy); setchar(' '); } // place word is 5 wide at x
}

static uint16_t input_seed_words_screen(uint16_t initial_seed, uint16_t entropy_hint) { // interactive seed picker
    uint8_t  x, y, word_pos = 0, prev_j = 0; // word_pos 0..2 selects which word the caret edits
    uint16_t frame_counter = 0;              // feeds entropy mixer each frame
    uint16_t s = initial_seed ? initial_seed : 1u;
    if (s > 64000u) s = 64000u;
    s--;
    uint8_t d = (uint8_t)(s % 40u);             // current indices
    uint8_t n = (uint8_t)((s / 40u) % 40u);
    uint8_t p = (uint8_t)((s / 1600u) % 40u);

    for (y = 0; y < 18; y++) // GBC text rows used by this game layout
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); } // clear playfield width only

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
    uint8_t  x, y, blink_counter = 0, blink_visible = 1, prev_j = 0;
    uint16_t frame_counter = 0;

    for (y = 0; y < 18; y++)
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); }
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

void game_over_screen(void) { // blocks until START
    uint8_t x, y;
    for (y = 0; y < 18; y++)
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); }
    gotoxy(6,  8); printf("GAME OVER");
    gotoxy(5, 10); printf("START=again");
    while (1) {
        if (joypad() & J_START) break; // no edge detect: holding START still works
        wait_vbl_done();
    }
}
