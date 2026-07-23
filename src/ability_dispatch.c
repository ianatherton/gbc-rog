#include "ability_dispatch.h"
#include "abilities_class.h"
#include "spells.h"
#include "globals.h"
#include <string.h>

BANKREF_EXTERN(ability_knight_cast)
BANKREF_EXTERN(ability_scoundrel_cast)
BANKREF_EXTERN(ability_witch_cast)
BANKREF_EXTERN(ability_zerker_cast)

BANKREF_EXTERN(abilities_knight_new_run_init)
BANKREF_EXTERN(abilities_scoundrel_new_run_init)
BANKREF_EXTERN(abilities_witch_new_run_init)
BANKREF_EXTERN(abilities_zerker_new_run_init)

void ability_dispatch_cast(uint8_t spell_id, uint8_t rank, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t idx = SPELL_ID_IDX(spell_id);
    memset(out, 0, sizeof *out);
    switch (SPELL_ID_CLASS(spell_id)) {
        case 0u: ability_knight_cast(idx, rank, px, py, out);    break;
        case 1u: ability_scoundrel_cast(idx, rank, px, py, out); break;
        case 2u: ability_witch_cast(idx, rank, px, py, out);     break;
        case 3u: ability_zerker_cast(idx, rank, px, py, out);    break;
        default: break;
    }
}

void ability_dispatch_cast_belt(uint8_t belt_slot, uint8_t px, uint8_t py, AbilityResult *out) {
    uint8_t idx = belt_spell[belt_slot];
    uint8_t rank, id;
    memset(out, 0, sizeof *out);
    if (idx >= SPELLS_PER_CLASS) return;      // SPELL_IDX_NONE / garbage → empty slot
    rank = spell_rank[idx];
    if (rank == 0u) return;                   // unlearned
    if (spell_cd[idx] > 0u) {
        spells_notify_recharging((uint8_t)(spell_cd[idx] - 1u));
        return;
    }
    id = SPELL_ID(player_class, idx);
    ability_dispatch_cast(id, rank, px, py, out);
    if (out->consumed_turn) spell_cd[idx] = spells_cooldown_for(id, rank);
}

void ability_dispatch_new_run_init(void) {
    switch (player_class) {
        case 0u: abilities_knight_new_run_init();   break;
        case 1u: abilities_scoundrel_new_run_init(); break;
        case 2u: abilities_witch_new_run_init();    break;
        case 3u: abilities_zerker_new_run_init();   break;
        default: break;
    }
}
