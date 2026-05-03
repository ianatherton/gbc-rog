#include "ally.h"
#include "globals.h"
#include "defs.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(ally_fox_summon)

BANKREF(ally_clear_slot)
void ally_clear_slot(uint8_t s) {
    if (s >= MAX_ALLIES) return;
    ally_active[s]   = 0u;
    ally_type[s]     = ALLY_TYPE_NONE;
    ally_chase_ei[s] = ENEMY_DEAD;
    ally_flip_x[s]   = 0u;
}

BANKREF(ally_clear_all)
void ally_clear_all(void) {
    uint8_t i;
    for (i = 0u; i < MAX_ALLIES; i++) ally_clear_slot(i);
}

BANKREF(ally_find_free_slot)
uint8_t ally_find_free_slot(void) {
    uint8_t i;
    for (i = 0u; i < MAX_ALLIES; i++) {
        if (!ally_active[i]) return i;
    }
    return 255u;
}

BANKREF(ally_has_type)
uint8_t ally_has_type(uint8_t t) {
    uint8_t i;
    for (i = 0u; i < MAX_ALLIES; i++) {
        if (ally_active[i] && ally_type[i] == t) return 1u;
    }
    return 0u;
}

BANKREF(ally_summon_fox)
void ally_summon_fox(uint8_t px, uint8_t py) {
    uint8_t s = ally_find_free_slot();
    if (s == 255u) return;
    ally_fox_summon(s, px, py);
}
