#ifndef COMBAT_H
#define COMBAT_H

#include <stdint.h>
#include <gbdk/platform.h>

uint8_t resolve_enemy_hits_and_animate(uint8_t px, uint8_t py) BANKED;
uint8_t combat_player_attacks(uint8_t ei, uint8_t px, uint8_t py, uint8_t nx, uint8_t ny) BANKED; // 1 if enemy died (corpse on nx,ny); else 0
uint8_t combat_damage_enemy(uint8_t ei, uint8_t damage, uint8_t from_shield_burn) BANKED; // 1 if enemy died; shield_burn: holy fire log line
uint8_t combat_crit_roll(uint8_t base_damage) BANKED; // rolls player_crit_chance; returns base*2 on crit, base otherwise
uint8_t combat_player_melee_extras(uint8_t ei) BANKED; // axe cleave + mace stun after a melee hit on ei; 1 if a cleave kill happened

#endif // COMBAT_H
