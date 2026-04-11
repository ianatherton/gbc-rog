#ifndef COMBAT_H
#define COMBAT_H

#include <stdint.h>

void push_combat_log(uint8_t type_idx, uint8_t dmg);
void grant_xp_from_kill(uint8_t enemy_damage);
uint8_t resolve_enemy_hits_and_animate(uint8_t px, uint8_t py);
void combat_player_attacks(uint8_t ei, uint8_t px, uint8_t py, uint8_t nx, uint8_t ny); // bump strike + kill bookkeeping

#endif // COMBAT_H
