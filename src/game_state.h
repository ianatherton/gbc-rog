#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdint.h>

typedef enum {
    STATE_TITLE = 0,
    STATE_CHAR_CREATE,
    STATE_GAMEPLAY,
    STATE_STATS,
    STATE_INVENTORY,
    STATE_ABILITY,
    STATE_GAME_OVER,
    STATE_NONE = 255,
} GameState;

extern volatile GameState current_state;
extern volatile GameState next_state;

#endif // GAME_STATE_H
