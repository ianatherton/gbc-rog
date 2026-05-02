#ifndef ALLY_H
#define ALLY_H

#include <stdint.h>
#include "defs.h"
#include <gbdk/platform.h>

void    ally_clear_all(void);
void    ally_clear_slot(uint8_t slot);
uint8_t ally_find_free_slot(void); // 255 = no free slot
uint8_t ally_has_type(uint8_t type);
void    ally_summon_fox(uint8_t px, uint8_t py); // picks a free slot; no-op if full

void    ally_fox_summon(uint8_t slot, uint8_t px, uint8_t py) BANKED;
uint8_t ally_fox_turn_tick(uint8_t slot, uint8_t px, uint8_t py) BANKED;
void    ally_fox_run_glide(uint8_t slot, uint8_t old_fx, uint8_t old_fy) BANKED;

#endif
