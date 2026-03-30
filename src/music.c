#include <gb/gb.h>
#include <stdint.h>

#include "bwv873_music.h"
#include "music.h"

// Tomita GBDK export: .dur is hold time in VBlank periods (one decrement per music_update() call), not raw MIDI ticks.

static uint8_t  mode_title;
static uint16_t mel_i, bas_i;
static uint16_t mel_rem, bas_rem;
static uint8_t  title_slow_vbl; // prelude: skip every other VBlank (~½ tempo)

static void wave_load_soft(void) {
    static const uint8_t w[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    };
    uint8_t i;
    NR30_REG = 0x00u;
    for (i = 0u; i < 16u; i++) {
        AUD3WAVE[i] = w[i];
    }
    NR30_REG = 0x80u;
}

static uint8_t dur_steps_per_vbl(void) {
    if (mode_title) {
        title_slow_vbl++;
        if ((title_slow_vbl & 1u) == 0u) {
            return 0u;
        }
        return 1u;
    }
    return 1u;
}

static void ch1_play(uint16_t f) {
    if (f == GB_NOTE_REST) {
        NR12_REG = 0x08;
        NR14_REG = 0x80;
        return;
    }
    NR10_REG = 0x00u;
    NR11_REG = 0x3Fu; // 12.5% duty
    NR12_REG = mode_title ? 0xC7u : 0xB5u; // lead envelope vol down one notch vs prior
    NR13_REG = (uint8_t)(f & 0xFFu);
    NR14_REG = (uint8_t)(0x80u | ((f >> 8) & 0x07u));
}

static void ch3_play(uint16_t f) {
    if (f == GB_NOTE_REST) {
        NR32_REG = 0x00u;
        return;
    }
    NR32_REG = 0x20u; // wave out level 100% (was 0x40 = 50%)
    NR33_REG = (uint8_t)(f & 0xFFu);
    NR34_REG = (uint8_t)(0x80u | ((f >> 8) & 0x07u));
}

static void silence_ch2(void) {
    NR22_REG = 0x08;
    NR24_REG = 0x80;
}

static void push_melody(uint8_t adv) {
    while (adv > 0u) {
        if (mel_rem == 0u) {
            if (mode_title && mel_i >= BWV873_PRELUDE_END_MELODY) {
                mel_i   = 0;
                bas_i   = 0;
                bas_rem = 0;
            }
            uint16_t f = bwv873_melody[mel_i].freq;
            uint8_t d  = bwv873_melody[mel_i].dur;
            if (f == GB_NOTE_END) {
                mel_i   = mode_title ? 0u : BWV873_FUGUE_START_MELODY;
                bas_i   = mode_title ? 0u : BWV873_FUGUE_START_BASS;
                bas_rem = 0u; // resync bass on full loop
                continue;
            }
            ch1_play(f);
            mel_rem = d;
            mel_i++;
            if (mel_i >= BWV873_NUM_EVENTS) {
                mel_i = 0;
            }
        }
        uint16_t take = (mel_rem <= (uint16_t)adv) ? mel_rem : (uint16_t)adv;
        mel_rem = (uint16_t)(mel_rem - take);
        adv     = (uint8_t)(adv - (uint8_t)take);
    }
}

static void push_bass(uint8_t adv) {
    while (adv > 0u) {
        if (bas_rem == 0u) {
            uint16_t f = bwv873_bass[bas_i].freq;
            uint8_t d  = bwv873_bass[bas_i].dur;
            if (f == GB_NOTE_END) {
                bas_i = mode_title ? 0u : BWV873_FUGUE_START_BASS;
                continue;
            }
            ch3_play(f);
            bas_rem = d;
            bas_i++;
            if (bas_i >= BWV873_NUM_EVENTS) {
                bas_i = 0;
            }
        }
        uint16_t take = (bas_rem <= (uint16_t)adv) ? bas_rem : (uint16_t)adv;
        bas_rem = (uint16_t)(bas_rem - take);
        adv     = (uint8_t)(adv - (uint8_t)take);
    }
}

static void music_vbl(void) {
    uint8_t adv = dur_steps_per_vbl();
    push_melody(adv);
    push_bass(adv);
}

void sfx_lunge_hit(void) {
    NR41_REG = 0x3Fu;   // min length — cuts off quickly (64−t1)/256 s
    NR42_REG = 0x41u;   // start vol 4/15, decay, envelope step 1 — quiet + short tail
    NR43_REG = 0x68u;   // lighter noise than old hit
    NR44_REG = 0x80u;
}

void music_play_title(void) {
    mode_title = 1u;
    mel_i = bas_i = 0;
    mel_rem = bas_rem = 0;
    title_slow_vbl = 0;
}

void music_play_game(void) {
    mode_title = 0u;
    mel_i      = BWV873_FUGUE_START_MELODY;
    bas_i      = BWV873_FUGUE_START_BASS;
    mel_rem = bas_rem = 0;
    title_slow_vbl = 0;
}

void music_init(void) {
    NR52_REG = 0x80u;
    NR50_REG = 0x77u;
    NR51_REG = 0xFFu;
    wave_load_soft();
    NR31_REG = 0xFFu;
    silence_ch2();
    NR12_REG = 0x08;
    NR14_REG = 0x80;
    NR32_REG = 0x00u;
    music_play_title();
    add_VBL(music_vbl);
}
