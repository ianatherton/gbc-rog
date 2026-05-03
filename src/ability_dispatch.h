#ifndef ABILITY_DISPATCH_H
#define ABILITY_DISPATCH_H

#include <stdint.h>

// Result struct so banked ability code never has to redraw / mutate gameplay state directly.
// state_gameplay (bank 2) reads these flags and does the BKG/UI follow-up itself.
typedef struct {
    uint8_t consumed_turn;    // 1 → enemies move, cooldowns tick, turn advances
    uint8_t did_kill;         // 1 → kill_x/kill_y holds the tile to redraw
    uint8_t kill_x;
    uint8_t kill_y;
    uint8_t lighting_refresh; // 1 → fog reveal radius changed (e.g. candle) — state_gameplay repaints BKG
} AbilityResult;

// HOME-resident dispatcher. Looks up player_class + belt slot, SWITCH_ROMs into
// the matching purple bank (6=knight, 7=scoundrel, 8=witch, 9=zerker), invokes
// the per-class ability entry, and returns. Result is zeroed if nothing happened.
void ability_dispatch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out);

// Lightweight predicate used by UI / belt rendering — does the active class+slot
// have an ability ready to fire? (e.g. cooldown clear). HOME-only; no bank switch.
uint8_t ability_dispatch_belt_ready(uint8_t belt_slot);

#endif
