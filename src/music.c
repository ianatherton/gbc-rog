#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/platform.h>
#include <stdint.h>

#include "bwv1043_music.h"
#include "music.h"

BANKREF_EXTERN(bwv1043_music)

static uint8_t  mode_title;
static uint16_t mel_i, bas_i;
static uint16_t mel_rem, bas_rem;
static uint8_t  title_slow_vbl; // prelude: skip every other VBlank (~½ tempo)
static uint16_t game_tempo_accum; // fractional BGM steps/VBlank in dungeon (dur_steps_per_vbl)

static uint8_t     jingle_active;
static uint8_t     jingle_idx;
static uint16_t    jingle_rem;
static uint16_t    jingle_sav_mel_i, jingle_sav_bas_i;
static uint16_t    jingle_sav_mel_rem, jingle_sav_bas_rem;
static uint8_t     jingle_sav_mode_title;

static uint8_t     loading_audio;   // mutes CH1/CH3 advance; CH4 footfalls only
static uint8_t     loading_step_i;  // 0..6 — six hits then idle
static uint8_t     loading_vbl_gap; // vblanks until next hit
static uint8_t     whirlwind_burst_remaining; // queued extra whooshes after initial cast pulse
static uint8_t     whirlwind_burst_gap;       // VBlank spacing between queued whooshes

// C5→E6 rising major line; .dur in VBlanks (~1 s total)
static const GBNote levelup_jingle[10] = {
    { 1797u, 6u }, { 1825u, 6u }, { 1849u, 6u }, { 1860u, 6u }, { 1881u, 6u },
    { 1899u, 6u }, { 1915u, 6u }, { 1923u, 6u }, { 1936u, 7u }, { 1949u, 14u },
};

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
    if (jingle_active) {
        return 1u; // jingle at written tempo (BGM uses fractional advance below)
    }
    game_tempo_accum = (uint16_t)(game_tempo_accum + 100u); // ~15% slow vs 1 step/VBlank: 1/1.15 ≈ 100/115
    {
        uint8_t steps = 0u;
        while (game_tempo_accum >= 115u) {
            game_tempo_accum = (uint16_t)(game_tempo_accum - 115u);
            steps++;
        }
        return steps;
    }
}

static void ch1_play(uint16_t f) {
    if (f == GB_NOTE_REST) {
        NR12_REG = 0x08;
        NR14_REG = 0x80;
        return;
    }
    NR10_REG = 0x00u;
    NR11_REG = 0x3Fu; // 12.5% duty
    NR12_REG = mode_title ? 0x77u : 0x75u; // ~40% quieter than 12/15 — env period unchanged in low nibble
    NR13_REG = (uint8_t)(f & 0xFFu);
    NR14_REG = (uint8_t)(0x80u | ((f >> 8) & 0x07u));
}

static uint16_t ch3_octave_up(uint16_t f) { // GB freq reg: x' = 1024 + x/2 doubles pitch (~+12 semitones)
    return (uint16_t)(1024u + (f >> 1u));
}

static void ch3_play(uint16_t f) {
    if (f == GB_NOTE_REST) {
        NR32_REG = 0x00u;
        return;
    }
    f = ch3_octave_up(f);
    NR32_REG = 0x20u; // wave out level 100% (was 0x40 = 50%)
    NR33_REG = (uint8_t)(f & 0xFFu);
    NR34_REG = (uint8_t)(0x80u | ((f >> 8) & 0x07u));
}

static void silence_ch2(void) {
    NR22_REG = 0x08;
    NR24_REG = 0x80;
}

static void silence_bgm_channels(void) { // square + wave off; leave CH4 for SFX
    NR12_REG = 0x08;
    NR14_REG = 0x80;
    NR32_REG = 0x00u;
}

static void resume_bgm_hw(void) { // after loading or jingle — re-arm held notes (bwv1043 in own ROM bank)
    uint8_t sb = _current_bank;
    SWITCH_ROM(BANK(bwv1043_music));
    if (mel_rem > 0u && mel_i > 0u) {
        uint8_t idx = bwv1043_melody[mel_i - 1u];
        ch1_play(bwv1043_dict[idx].freq);
    }
    if (bas_rem > 0u && bas_i > 0u) {
        uint8_t idx = bwv1043_bass[bas_i - 1u];
        ch3_play(bwv1043_dict[idx].freq);
    }
    SWITCH_ROM(sb);
}

static void sfx_ch4_hit_noise(void) { // shared CH4 patch — lunge/deciding hit + loading steps
    NR41_REG = 0x22u;   // medium-short length for a chunkier body
    NR42_REG = 0xF2u;   // loud attack, decay envelope for punch
    NR43_REG = 0x36u;   // lower-frequency noise gives a crunchier impact
    NR44_REG = 0x80u;
}

static void sfx_ch4_hit_noise_alt(void) { // alternate crunchy profile to vary repeated combat impacts
    NR41_REG = 0x24u;   // similar body length to primary hit
    NR42_REG = 0xC2u;   // strong attack but less bright than zap-like patches
    NR43_REG = 0x62u;   // close to legacy hit noise color, just a touch different from primary
    NR44_REG = 0x80u;
}

static void sfx_ch4_whirlwind_woosh(uint8_t phase) { // phase 0..2 for tiny timbral drift across the three-pulse burst
    static const uint8_t n43[3] = { 0x38u, 0x3Cu, 0x48u }; // lower-pitched whoosh set
    NR41_REG = 0x10u;
    NR42_REG = 0xC3u;   // bright attack + short decay
    NR43_REG = n43[phase % 3u];
    NR44_REG = 0x80u;
}

static void loading_fire_footfall(void) { // same patch as hit but envelope start vol steps down (loading_step_i 0..5)
    static const uint8_t env[6] = { 0x71u, 0x61u, 0x51u, 0x41u, 0x31u, 0x21u }; // high nibble 7→2, low=1 like hit
    NR41_REG = 0x3Fu;
    NR42_REG = env[loading_step_i];
    NR43_REG = 0x68u;
    NR44_REG = 0x80u;
}

static void loading_vbl_tick(void) {
    if (loading_vbl_gap > 0u) {
        loading_vbl_gap--;
    }
    if (loading_vbl_gap > 0u) {
        return;
    }
    if (loading_step_i < 6u) {
        loading_fire_footfall();
        loading_step_i++;
        loading_vbl_gap = 30u; // 5×30 + 1st @0 ≈150 VBlanks ≈2.5s for all six steps @60Hz
    }
}

static void jingle_finish(void) {
    jingle_active = 0;
    mel_i      = jingle_sav_mel_i;
    bas_i      = jingle_sav_bas_i;
    mel_rem    = jingle_sav_mel_rem;
    bas_rem    = jingle_sav_bas_rem;
    mode_title = jingle_sav_mode_title;
    resume_bgm_hw();
}

static void push_jingle(uint8_t *adv) {
    while (*adv > 0u && jingle_active) {
        if (jingle_rem == 0u) {
            if (jingle_idx >= 10u) {
                jingle_finish();
                break;
            }
            {
                uint16_t f = levelup_jingle[jingle_idx].freq;
                uint8_t d  = levelup_jingle[jingle_idx].dur;
                jingle_idx++;
                if (f == GB_NOTE_REST) {
                    NR12_REG = 0x08;
                    NR14_REG = 0x80;
                } else {
                    NR10_REG = 0x00u;
                    NR11_REG = 0x3Fu;
                    NR12_REG = 0xC5u; // fanfare level — slightly brighter than gameplay lead
                    NR13_REG = (uint8_t)(f & 0xFFu);
                    NR14_REG = (uint8_t)(0x80u | ((f >> 8) & 0x07u));
                }
                jingle_rem = d;
            }
        }
        {
            uint16_t take = (jingle_rem <= *adv) ? jingle_rem : *adv;
            jingle_rem = (uint16_t)(jingle_rem - take);
            *adv       = (uint8_t)(*adv - (uint8_t)take);
        }
    }
}

static void push_melody(uint8_t adv) {
    while (adv > 0u) {
        if (mel_rem == 0u) {
            if (mode_title && mel_i >= BWV1043_PRELUDE_END_MELODY) {
                mel_i   = 0;
                bas_i   = 0;
                bas_rem = 0;
            }
            uint8_t idx = bwv1043_melody[mel_i];
            if (idx == BWV1043_SENTINEL) {
                mel_i   = mode_title ? 0u : BWV1043_GAME_START_MELODY;
                bas_i   = mode_title ? 0u : BWV1043_GAME_START_BASS;
                bas_rem = 0u;
                continue;
            }
            ch1_play(bwv1043_dict[idx].freq);
            mel_rem = bwv1043_dict[idx].dur;
            mel_i++;
            if (mel_i >= BWV1043_NUM_MELODY_EVENTS) {
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
            uint8_t idx = bwv1043_bass[bas_i];
            if (idx == BWV1043_SENTINEL) {
                bas_i = mode_title ? 0u : BWV1043_GAME_START_BASS;
                continue;
            }
            ch3_play(bwv1043_dict[idx].freq);
            bas_rem = bwv1043_dict[idx].dur;
            bas_i++;
            if (bas_i >= BWV1043_NUM_BASS_EVENTS) {
                bas_i = 0;
            }
        }
        uint16_t take = (bas_rem <= (uint16_t)adv) ? bas_rem : (uint16_t)adv;
        bas_rem = (uint16_t)(bas_rem - take);
        adv     = (uint8_t)(adv - (uint8_t)take);
    }
}

static void music_vbl(void) {
    if (loading_audio) {
        loading_vbl_tick();
        return;
    }
    if (whirlwind_burst_remaining > 0u) {
        if (whirlwind_burst_gap > 0u) whirlwind_burst_gap--;
        if (whirlwind_burst_gap == 0u) {
            uint8_t phase = (uint8_t)(3u - whirlwind_burst_remaining); // 1,2 for queued pulses (0 is fired immediately)
            sfx_ch4_whirlwind_woosh(phase);
            whirlwind_burst_remaining--;
            whirlwind_burst_gap = 6u; // ~100ms at 60Hz between whooshes
        }
    }
    {
        uint8_t sb = _current_bank;
        SWITCH_ROM(BANK(bwv1043_music));
        {
            uint8_t adv = dur_steps_per_vbl();
            if (jingle_active) {
                push_jingle(&adv);
            }
            if (!jingle_active) {
                push_melody(adv);
                push_bass(adv);
            }
        }
        SWITCH_ROM(sb);
    }
}

void sfx_lunge_hit(void) {
    if ((DIV_REG & 3u) == 0u) sfx_ch4_hit_noise_alt(); // ~25% chance for alternate strike color
    else                      sfx_ch4_hit_noise();
}

void sfx_spell_zap(void) {
    NR21_REG = 0x80u; // 50% duty, short length
    NR22_REG = 0xD2u; // loud start, quick decay
    NR23_REG = 0x40u; // lower bits of pitch
    NR24_REG = 0x86u; // trigger + high bits of pitch
}

void sfx_whirlwind_cast(void) {
    sfx_ch4_whirlwind_woosh(0u);    // immediate first whoosh
    whirlwind_burst_remaining = 2u; // queue two more whooshes for a 3-pulse cast signature
    whirlwind_burst_gap = 6u;       // ~100ms before next pulse
}

void sfx_shield_sparkle(void) {
    NR21_REG = 0x40u; // 25% duty for a bell-like tone
    NR22_REG = 0xB3u; // bright attack with brief tail
    NR23_REG = 0xD0u; // high pitch
    NR24_REG = 0x87u; // trigger + high bits
}

void music_play_title(void) {
    mode_title = 1u;
    mel_i = bas_i = 0;
    mel_rem = bas_rem = 0;
    game_tempo_accum = 0u;
    title_slow_vbl = 0;
    jingle_active = 0;
    loading_audio = 0;
    whirlwind_burst_remaining = 0u;
    whirlwind_burst_gap = 0u;
}

void music_play_game(void) {
    mode_title = 0u;
    mel_i      = BWV1043_GAME_START_MELODY;
    bas_i      = BWV1043_GAME_START_BASS;
    mel_rem = bas_rem = 0;
    game_tempo_accum = 0u;
    title_slow_vbl = 0;
    jingle_active = 0;
    loading_audio = 0;
    whirlwind_burst_remaining = 0u;
    whirlwind_burst_gap = 0u;
}

void music_loading_screen_set(uint8_t on) {
    if (on) {
        loading_audio    = 1u;
        loading_step_i   = 0u;
        loading_vbl_gap  = 0u;
        whirlwind_burst_remaining = 0u;
        whirlwind_burst_gap = 0u;
        silence_bgm_channels();
        return;
    }
    loading_audio   = 0u;
    loading_step_i  = 0u;
    loading_vbl_gap = 0u;
    whirlwind_burst_remaining = 0u;
    whirlwind_burst_gap = 0u;
    resume_bgm_hw();
}

void music_play_levelup_jingle(void) {
    if (jingle_active || mode_title || loading_audio) {
        return;
    }
    jingle_sav_mel_i       = mel_i;
    jingle_sav_bas_i       = bas_i;
    jingle_sav_mel_rem     = mel_rem;
    jingle_sav_bas_rem     = bas_rem;
    jingle_sav_mode_title  = mode_title;
    NR32_REG = 0x00u; // mute wave bass for fanfare
    jingle_idx    = 0;
    jingle_rem    = 0;
    jingle_active = 1;
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
