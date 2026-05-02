#ifndef SCOUNDREL_FOX_H
#define SCOUNDREL_FOX_H

#include <stdint.h>
#include <gbdk/platform.h>

void scoundrel_fox_clear(void) BANKED;
void scoundrel_fox_summon(uint8_t px, uint8_t py) BANKED;
uint8_t scoundrel_fox_turn_tick(uint8_t px, uint8_t py) BANKED;
void scoundrel_fox_run_glide(uint8_t old_fx, uint8_t old_fy) BANKED; // pixel ease via direct OAM trampolined from gameplay (bank 2 → bank 3)

#endif
