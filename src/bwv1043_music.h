#ifndef BWV1043_MUSIC_H
#define BWV1043_MUSIC_H

#include <stdint.h>

typedef struct {
    uint16_t freq;
    uint8_t  dur;
} GBNote;

extern const GBNote bwv1043_dict[];
extern const uint8_t bwv1043_melody[];
extern const uint8_t bwv1043_bass[];

#define BWV1043_DICT_LEN 102u
#define BWV1043_SENTINEL 0xFFu

// Filled by tools/emit_bwv1043_c.py
#define BWV1043_NUM_MELODY_EVENTS 1404u
#define BWV1043_NUM_BASS_EVENTS 1261u
#define BWV1043_PRELUDE_END_MELODY 499u
#define BWV1043_PRELUDE_END_BASS 491u
#define BWV1043_GAME_START_MELODY BWV1043_PRELUDE_END_MELODY
#define BWV1043_GAME_START_BASS BWV1043_PRELUDE_END_BASS

#define GB_NOTE_REST 0u
#define GB_NOTE_END 0xFFFFu

#endif
