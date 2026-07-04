#ifndef DUNGEON_H
#define DUNGEON_H

#include <stdint.h>

// ── 9 independent dungeons off the overworld hub ───────────────────────────────
// floor_num stays the single source of truth; dungeon id / local floor are pure
// derivations. Global floor map:
//   0        hub (overworld)
//   1..36    dungeon k = (f-1)/4 (0-based), local floor ((f-1)%4)+1:
//              local 1 normal → local 2 miniboss → local 3 normal → local 4 boss
//   37..45   guardroom of dungeon f-37 (local 0: items, no monsters, no depth)
// Guardroom floors double as persistence keys: indices 36..44 < MAX_FLOORS(50),
// so floor_items_picked / floor_enemy_dead need no resizing.
#define DUNGEON_COUNT     9u
#define DUNGEON_FLOORS    4u
#define GUARD_FLOOR_BASE 37u
#define DUNGEON_NONE   0xFFu

// NOTE: macros evaluate args multiple times — call with plain variables only.
#define FLOOR_DUNGEON_ID(f) ((f) == 0u ? DUNGEON_NONE \
                             : ((f) >= GUARD_FLOOR_BASE ? (uint8_t)((f) - GUARD_FLOOR_BASE) \
                                                        : (uint8_t)(((f) - 1u) >> 2)))
#define FLOOR_LOCAL(f)      ((f) >= GUARD_FLOOR_BASE ? 0u : (uint8_t)((((f) - 1u) & 3u) + 1u))
#define DUNGEON_BASE_FLOOR(d)  (uint8_t)((d) * 4u + 1u)
#define DUNGEON_BOSS_FLOOR(d)  (uint8_t)((d) * 4u + 4u)
#define DUNGEON_GUARD_FLOOR(d) (uint8_t)(GUARD_FLOOR_BASE + (d))

// Floor kinds — orthogonal to floor_biome (which is DUNGEON/CRYPT/CAVERN for a
// whole dungeon, guardroom through boss). Set by floor_kind_for(floor_num).
#define FLOORKIND_HUB      0u
#define FLOORKIND_GUARD    1u
#define FLOORKIND_NORMAL   2u // locals 1 and 3
#define FLOORKIND_MINIBOSS 3u // local 2
#define FLOORKIND_BOSS     4u // local 4

#define FLOOR_KIND_FOR(f) ((f) == 0u ? FLOORKIND_HUB \
                           : (f) >= GUARD_FLOOR_BASE ? FLOORKIND_GUARD \
                           : (((f) - 1u) & 3u) == 1u ? FLOORKIND_MINIBOSS \
                           : (((f) - 1u) & 3u) == 3u ? FLOORKIND_BOSS \
                           : FLOORKIND_NORMAL)

#endif // DUNGEON_H
