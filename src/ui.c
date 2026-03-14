#include "ui.h"
#include "seed_entropy.h"

#define SEED_WORDS_N 40

static const char *const seed_words_desc[SEED_WORDS_N] = {
    "ASHEN","BLEAK","BLIND","BLOOD","BLUNT","BONED","BURNT","COLD","CRUEL","CURST",
    "DARK","DEAD","DEEP","DENSE","DIRE","DREAD","DREAR","DULL","DUSK","DUSTY",
    "EMBER","FELL","FETID","FOUL","GAUNT","GRIM","IRON","LOST","LUNAR","MURKY",
    "PALE","ROT","SHADE","SHORN","STARK","STILL","SUNK","TOXIC","VILE","VOID"
};
static const char *const seed_words_noun[SEED_WORDS_N] = {
    "ASH","BANE","BLOT","BONE","BRIAR","BRIER","COIL","CROW","CRYPT","DUST",
    "EMBER","FANG","FLAME","FROND","FROST","FUNGI","HORN","IRON","LARVA","MIRE",
    "MIST","MOSS","MOTH","MURK","PITH","ROOT","RUIN","SHADE","SKULL","SLIME",
    "SMOKE","SPINE","SPORE","STONE","THORN","TIDE","TOMB","VENOM","WISP","WORM"
};
static const char *const seed_words_place[SEED_WORDS_N] = {
    "ABYSS","BOG","BRINK","CAIRN","CAVES","CHASM","CRAGS","CRYPT","DEEP","DELL",
    "DELVE","DUNES","FELLS","FLATS","GORGE","GULCH","HEATH","KEEP","MARSH","MIRE",
    "MOORS","MOUND","NOOK","PEAKS","PITS","RIFT","RUINS","SANDS","SHELF","SHORE",
    "SLOPE","SPIRE","STEPS","TOMB","VALE","VAULT","WASTE","WEALD","WILDS","WOOD"
};

static void put_word5(uint8_t x, uint8_t y, const char *s) {
    uint8_t i;
    for (i = 0; i < 5; i++) { gotoxy((uint8_t)(x+i), y); setchar(s[i] ? s[i] : ' '); }
}

static uint16_t input_seed_words_screen(uint16_t initial_seed, uint16_t power_on_ticks) {
    uint8_t  x, y, word_pos = 0, prev_j = 0;
    uint16_t frame_counter = 0;
    uint16_t s = initial_seed ? initial_seed : 1u;
    if (s > 64000u) s = 64000u;
    s--;
    uint8_t d = (uint8_t)(s % 40u);
    uint8_t n = (uint8_t)((s / 40u) % 40u);
    uint8_t p = (uint8_t)((s / 1600u) % 40u);

    for (y = 0; y < 18; y++)
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); }

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
        gotoxy(1 + word_pos * 6, 4); setchar('^');
        gotoxy(1, 6); printf("L/R word");
        gotoxy(1, 7); printf("U/D scroll");
        gotoxy(1, 8); printf("New seed: (A)");
        gotoxy(1, 9); printf("START=play");
        {
            uint8_t j    = joypad();
            uint8_t edge = (uint8_t)(j & (uint8_t)~prev_j);
            if (edge & J_START) {
                uint16_t fs = (uint16_t)(1u + (uint16_t)d + 40u*(uint16_t)n + 1600u*(uint16_t)p);
                if (!fs) fs = 1;
                return fs;
            }
            if (edge & J_A) {
                uint16_t t = seed_entropy_random_seed(power_on_ticks, frame_counter);
                if (t > 64000u) t = 64000u; t--;
                d = (uint8_t)(t % 40u); n = (uint8_t)((t/40u) % 40u); p = (uint8_t)((t/1600u) % 40u);
            }
            if (edge & J_LEFT)  word_pos = (word_pos == 0) ? 2 : (uint8_t)(word_pos-1);
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

uint16_t title_screen(uint16_t power_on_ticks) {
    uint8_t  x, y, blink_counter = 0, blink_visible = 1, prev_j = 0;
    uint16_t frame_counter = 0;

    for (y = 0; y < 18; y++)
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); }
    gotoxy(4,  7); printf("Mara's Abyss");
    gotoxy(3, 12); printf("SELECT=seed words");

    while (1) {
        uint8_t  j    = joypad();
        uint8_t  edge = (uint8_t)(j & (uint8_t)~prev_j);
        uint16_t mixed = seed_entropy_random_seed(power_on_ticks, frame_counter);
        if (edge & J_START) return mixed;
        if (edge & J_SELECT) {
            uint16_t seed = input_seed_words_screen(mixed, power_on_ticks);
            if (!seed) seed = 1;
            return seed;
        }
        blink_counter++;
        if (blink_counter >= 30) {
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

void game_over_screen(void) {
    uint8_t x, y;
    for (y = 0; y < 18; y++)
        for (x = 0; x < GRID_W; x++) { gotoxy(x, y); setchar(' '); }
    gotoxy(6,  8); printf("GAME OVER");
    gotoxy(5, 10); printf("START=again");
    while (1) {
        if (joypad() & J_START) break;
        wait_vbl_done();
    }
}

