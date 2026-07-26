#ifndef BIOME_ENCOUNTER_H
#define BIOME_ENCOUNTER_H

#include <stdint.h>
#include <gbdk/platform.h>

// Hub '?' encounters — bank 23. See biome_encounter.c for the two halves (hub markers, and the
// single reusable interior floor ENCOUNTER_FLOOR). The dispatch-table pair
// (biome_encounter_copy_defs / _load_palettes) is plain, not BANKED, and lives in biome.h beside
// the other biomes' rows; everything below is a normal far call.
void    encounter_markers_build(void) BANKED;              // hub gen: reroll the set from (run_seed, world_tick)
uint8_t encounter_marker_at(uint8_t x, uint8_t y) BANKED;  // marker ordinal at this hub cell, or 255
void    encounter_markers_tick(uint8_t px, uint8_t py) BANKED; // one drift step for mobile markers
void    encounter_enter(uint8_t ord) BANKED;               // latch enc_template/region/return before porting
uint8_t encounter_enemy_cap(void) BANKED;                  // fodder count for this encounter
void    encounter_generate(void) BANKED;                   // build the interior floor
uint8_t encounter_chest_open(uint8_t x, uint8_t y) BANKED; // 1 if a chest was there and opened

#endif // BIOME_ENCOUNTER_H
