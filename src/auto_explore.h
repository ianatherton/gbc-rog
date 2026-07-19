#ifndef AUTO_EXPLORE_H
#define AUTO_EXPLORE_H

#include <stdint.h>
#include <gbdk/platform.h>

/* A-button auto-explore (bank 30). Each gameplay tick while active, auto_explore_step()
   synthesizes a single dpad bit that state_gameplay_tick feeds through its normal walk/
   bump machinery — no turn logic is duplicated. States:
     0 = off
     1 = running
     2 = armed, waiting for the arming A press to release
     3 = stopping, swallowing input until all buttons release (prevents the cancel
         press from firing a belt/menu edge on the next tick)                        */
extern uint8_t auto_explore_active;

void    auto_explore_try_start(void) BANKED;     // A pressed with no confirm armed
uint8_t auto_explore_step(uint8_t j) BANKED;     // returns one J_* dpad bit, or 0
void    auto_explore_take_pending(void) BANKED;  // auto-pickup instead of STATE_PICKUP

#endif // AUTO_EXPLORE_H
