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

// HOME-resident dispatcher (always mapped, so ANY bank may plain-call these).
// Routes a global spell id (spells.h SPELL_ID encoding) into the matching purple
// bank (6=knight, 7=scoundrel, 8=witch, 9=zerker). rank 0 = generic-scroll
// strength. Zeroes *out first; no gating — caller decides castability.
void ability_dispatch_cast(uint8_t spell_id, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out);

// Belt path (B button, state_gameplay). Resolves belt_spell[belt_slot] for the
// active class, centralizes the cooldown gate + "Recharging" log + cooldown set,
// then routes through ability_dispatch_cast at the spell's trained rank.
void ability_dispatch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out);

// Called once on a fresh run (after inventory_clear_all) to give each class its starting items.
// Switches into the class's bank and calls abilities_<class>_new_run_init().
void ability_dispatch_new_run_init(void);

#endif
