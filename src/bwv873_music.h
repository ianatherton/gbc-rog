#ifndef BWV873_MUSIC_H
#define BWV873_MUSIC_H

#include <stdint.h>

typedef struct {
    uint16_t freq;
    uint8_t  dur;
} GBNote;

extern const GBNote bwv873_melody[];
extern const GBNote bwv873_bass[];

// 482 scored notes + trailing {END_NOTE, END_DUR} sentinel per track.
#define BWV873_NUM_EVENTS 483

// Prelude/fugue split (~38% of total melody ticks; re-run tools/emit_bwv873_c.py to retune).
#define BWV873_PRELUDE_END_MELODY 193u
#define BWV873_PRELUDE_END_BASS  182u
#define BWV873_FUGUE_START_MELODY BWV873_PRELUDE_END_MELODY
#define BWV873_FUGUE_START_BASS  BWV873_PRELUDE_END_BASS

#define GB_NOTE_REST 0u
#define GB_NOTE_END  0xFFFFu

#endif
