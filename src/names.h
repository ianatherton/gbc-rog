#ifndef NAMES_H
#define NAMES_H

#include <stdint.h>
#include <gbdk/platform.h>

// Deterministic name generator for towns/dungeons/NPCs. All names are re-derived
// from (run_seed, entity id) on demand — nothing is stored. Same idiom as
// biome_pick_for_floor / tg_hash / ow_feat_hash: hash, never rand(), so a name
// stays identical every time the same entity is revisited in a run.
//
// out must be a caller-owned RAM buffer of at least `cap` bytes; NUL-terminated,
// truncated safely if `cap` is too small. Composed names are <= 14 chars, so a
// 16-byte buffer is always enough headroom.

void town_name_copy(uint8_t town_id, char *out, uint8_t cap) BANKED;      // town_id 0..2
void dungeon_name_copy(uint8_t dungeon_id, char *out, uint8_t cap) BANKED; // dungeon_id 0..8 (DUNGEON_COUNT)
void npc_name_copy(uint8_t town_id, uint8_t npc_i, char *out, uint8_t cap) BANKED; // town_id 0..2, npc_i 0..2

#endif // NAMES_H
