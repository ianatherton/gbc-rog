#ifndef SCOUNDREL_FOX_H
#define SCOUNDREL_FOX_H

#include <stdint.h>
#include <gbdk/platform.h>

void scoundrel_fox_clear(void);
void scoundrel_fox_summon(uint8_t px, uint8_t py) BANKED;
uint8_t scoundrel_fox_turn_tick(uint8_t px, uint8_t py);

#endif
