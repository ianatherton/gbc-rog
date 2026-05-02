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
    STATE_PICKUP,     // modal: walked onto ground item — get/discard prompt
    STATE_TRANSITION, // bounce: floor-down, death→game over, future fades
    STATE_GAME_OVER,
    STATE_NONE = 255,
} GameState;

typedef enum {
    TRANS_NONE = 0,
    TRANS_FLOOR_PIT,    // next floor via pit; runs level_init_display(1)+generate
    TRANS_TO_GAME_OVER, // defer to STATE_GAME_OVER enter (reliable far-call path)
} TransitionKind;

extern volatile GameState current_state;
extern volatile GameState next_state;
extern volatile TransitionKind pending_transition;

#endif // GAME_STATE_H
