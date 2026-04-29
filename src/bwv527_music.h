#ifndef BWV527_MUSIC_H
#define BWV527_MUSIC_H

#include <stdint.h>

#include "bwv1043_music.h"

extern const GBNote bwv527_dict[];
extern const uint8_t bwv527_melody[];
extern const uint8_t bwv527_bass[];

#define BWV527_DICT_LEN 107u
#define BWV527_SENTINEL 0xFFu

// Filled by tools/emit_bwv527_from_mid.py
#define BWV527_NUM_MELODY_EVENTS 3009u
#define BWV527_NUM_BASS_EVENTS 2130u
#define BWV527_PRELUDE_END_MELODY 1046u
#define BWV527_PRELUDE_END_BASS 720u
#define BWV527_GAME_START_MELODY BWV527_PRELUDE_END_MELODY
#define BWV527_GAME_START_BASS BWV527_PRELUDE_END_BASS

#endif
