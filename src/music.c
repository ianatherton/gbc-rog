#include <gb/gb.h>
#include <stdint.h>

#include "defs.h" // player_hp/player_hp_max — game fugue tempo vs HP %
#include "music.h"

// GB period table (11-bit), same layout as gbdk/examples/gb/sound/sound.c — index = semitone from C0.
static const uint16_t freq[] = {
    44, 156, 262, 363, 457, 547, 631, 710, 786, 854, 923, 986,
    1046, 1102, 1155, 1205, 1253, 1297, 1339, 1379, 1417, 1452, 1486, 1517,
    1546, 1575, 1602, 1627, 1650, 1673, 1694, 1714, 1732, 1750, 1767, 1783,
    1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899, 1907, 1915,
    1923, 1930, 1936, 1943, 1949, 1954, 1959, 1964, 1969, 1974, 1978, 1982,
    1985, 1988, 1992, 1995, 1998, 2001, 2004, 2006, 2009, 2011, 2013, 2015
};

#define N_SIL 0xFF
#define N_D2  26
#define N_E2  28
#define N_A2  33
#define N_A3  45
#define N_B3  47
#define N_C4  48
#define N_D4  50
#define N_E4  52
#define N_F4  53
#define N_G4  55
#define N_A4  57
#define N_B4  59

// Title: A natural minor — i–iv–v (Am / Dm / Em arpeggios).
static const uint8_t title_mel[] = {
    N_A3, N_C4, N_E4, N_A4, N_E4, N_C4, N_E4, N_A3,
    N_D4, N_F4, N_A4, N_F4, N_D4, N_F4, N_A3, N_D4,
    N_E4, N_G4, N_B4, N_G4, N_E4, N_G4, N_B3, N_E4
};
static const uint8_t title_bas[] = {
    N_A2, N_A2, N_A2, N_A2, N_A2, N_A2, N_A2, N_A2,
    N_D2, N_D2, N_D2, N_D2, N_D2, N_D2, N_D2, N_D2,
    N_E2, N_E2, N_E2, N_E2, N_E2, N_E2, N_E2, N_E2
};

// Game: 128 eighths, A minor + G#/chromatic color; gestures echo BWV 784 / BWV 578 (novel mashup, not a lift).
static const uint8_t game_mel[] = {
    45, 48, 47, 45, 44, 40, 45, 47, 48, 50, 52, 50, 48, 47, 45, 44,
    45, 45, 45, 52, 48, 52, 45, 50, 48, 47, 45, 47, 48, 50, 48, 47,
    52, 50, 49, 48, 47, 46, 45, 44, 45, 47, 48, 50, 52, 50, 48, 47,
    48, 50, 52, 53, 55, 53, 52, 50, 47, 48, 50, 52, 53, 52, 50, 48,
    53, 55, 57, 59, 57, 55, 53, 52, 50, 48, 47, 45, 44, 45, 47, 48,
    50, 52, 53, 55, 57, 55, 53, 52, 50, 48, 50, 52, 53, 55, 57, 59,
    45, 48, 47, 45, 44, 40, 45, 52, 50, 48, 47, 45, 44, 45, 47, 48,
    47, 48, 50, 52, 53, 55, 52, 50, 48, 47, 45, 44, 45, 48, 45, 48,
};
static const uint8_t game_bas[] = {
    33, 40, 38, 36, 40, 33, 36, 38, 40, 41, 43, 41, 40, 38, 36, 35,
    33, 33, 33, 40, 36, 40, 33, 38, 36, 35, 33, 36, 38, 40, 38, 36,
    40, 38, 36, 35, 33, 35, 36, 38, 40, 38, 36, 35, 36, 38, 40, 41,
    36, 38, 40, 41, 43, 41, 40, 38, 36, 35, 36, 38, 40, 41, 40, 38,
    41, 40, 38, 36, 35, 36, 38, 40, 43, 41, 40, 38, 36, 35, 36, 38,
    33, 36, 33, 36, 40, 38, 40, 38, 36, 35, 33, 36, 33, 36, 40, 38,
    33, 40, 38, 36, 40, 33, 36, 40, 38, 40, 41, 43, 40, 38, 36, 35,
    41, 40, 38, 36, 35, 33, 36, 38, 40, 41, 40, 38, 36, 33, 36, 33,
};

typedef struct {
    const uint8_t *mel;
    const uint8_t *bas;
    uint16_t       len;
    uint8_t        ticks_per_step; // title only: VBlanks per eighth; game uses health_bpm_vblanks()
} Song;

static const Song song_title = { title_mel, title_bas, (uint16_t)(sizeof title_mel / sizeof title_mel[0]), 8u };
static const Song song_game  = { game_mel,  game_bas,  (uint16_t)(sizeof game_mel / sizeof game_mel[0]),  0u };

// BPM feel → VBlanks per eighth (≈60 Hz): 1800/BPM — stately when healthy, frantic when hurt.
static uint8_t health_bpm_vblanks(void) {
    uint8_t pct = (uint8_t)((uint16_t)player_hp * 100u / player_hp_max);
    if (pct >= 75u) {
        return 25u;
    } // 72 BPM
    if (pct >= 50u) {
        return 20u;
    } // 88
    if (pct >= 25u) {
        return 17u;
    } // 108
    return 14u; // 132 — 0–24% includes 0 HP edge before game-over music swap
}

static const Song *cur_song;
static uint16_t    step_i;
static uint8_t     tick_wait;

static void trigger_ch1(uint16_t period) {
    NR10_REG = 0x00;
    NR11_REG = 0x80;
    NR12_REG = (cur_song == &song_game) ? 0xC8u : 0xF4u; // game: softer + darker envelope
    NR13_REG = (uint8_t)(period & 0xFFu);
    NR14_REG = (uint8_t)(0x80u | ((period >> 8) & 0x07u));
}

static void trigger_ch2(uint16_t period) {
    NR21_REG = 0x80;
    NR22_REG = (cur_song == &song_game) ? 0x84u : 0x96u; // game: bass more subordinate
    NR23_REG = (uint8_t)(period & 0xFFu);
    NR24_REG = (uint8_t)(0x80u | ((period >> 8) & 0x07u));
}

static void silence_ch1(void) {
    NR12_REG = 0x08;
    NR14_REG = 0x80;
}

static void silence_ch2(void) {
    NR22_REG = 0x08;
    NR24_REG = 0x80;
}

static void music_vbl(void) {
    if (tick_wait) {
        tick_wait--;
        return;
    }
    {
        uint8_t span = (cur_song == &song_game) ? health_bpm_vblanks() : cur_song->ticks_per_step;
        tick_wait = (uint8_t)(span - 1u);
    }

    {
        uint8_t mi = cur_song->mel[step_i];
        uint8_t bi = cur_song->bas[step_i];
        if (mi != N_SIL) {
            trigger_ch1(freq[mi]);
        } else {
            silence_ch1();
        }
        if (bi != N_SIL) {
            trigger_ch2(freq[bi]);
        } else {
            silence_ch2();
        }
    }
    step_i++;
    if (step_i >= cur_song->len) {
        step_i = 0;
    }
}

void music_play_title(void) {
    cur_song = &song_title;
    step_i   = 0;
    tick_wait = 0;
}

void music_play_game(void) {
    cur_song = &song_game;
    step_i   = 0;
    tick_wait = 0;
}

void music_init(void) {
    NR52_REG = 0x80u;
    NR50_REG = 0x77u;
    NR51_REG = 0xFFu;
    music_play_title();
    add_VBL(music_vbl);
}
