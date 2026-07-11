#pragma bank 14

#include "names.h"
#include "biome.h"
#include "dungeon.h"
#include "globals.h"

// ── Word tables ─────────────────────────────────────────────────────────────
// Same shape as story_ui.c's continent-name tables (cpref/csuf): short prefix +
// suffix word lists, one flavor pair per dungeon biome so a name always matches
// what the dungeon actually looks like. All entries kept short so a composed
// name never exceeds ~13 chars.

static const char *const town_pref[] = {
    "OAK","MILL","RIVER","BRIGHT","ASH","ELM","BIRCH","FOX","SILVER","STONE",
    "GREEN","WOOD","BROOK","MEAD","BARLEY","ROSE","HOLLY","CROSS","KIRK","MARSH",
    "VALE","HEATH","WREN","FERN",
};
static const char *const town_suf[] = {
    "TON","FIELD","HAVEN","BROOK","FORD","BURY","WICK","STEAD","GATE","HOLLOW",
    "DALE","MOOR","RIDGE","MARKET","WELL","BRIDGE","HAM","GLEN","CROFT","SHIRE",
};

static const char *const dgn_pref[] = {
    "IRON","GRIM","STONE","GREY","BLACK","DEEP","OLD","BROKEN","RUST","COLD",
    "DARK","LOST","SUNK","FORGE","WARD","HOLLOW","GLOOM","DREAD","ASH","BONE",
};
static const char *const dgn_suf[] = {
    "HOLD","VAULT","KEEP","HALL","RUINS","DEPTHS","WARREN","MAZE","CELLS","SPIRE",
    "GATE","PIT","CHASM","FORT","BASTION","REACH","HOLLOW","DEN",
};

static const char *const crypt_pref[] = {
    "BONE","GRAVE","HOLLOW","WITHER","ASHEN","PALE","SILENT","ROTTEN","CURSED","SORROW",
    "BLACK","GHOST","SHROUD","MOURN","DUSK","GLOOM","DECAY","HALLOW","WRAITH","TOMB",
};
static const char *const crypt_suf[] = {
    "CRYPT","TOMB","BARROW","GRAVE","VAULT","RUIN","PIT","WARREN","HALL","DEPTHS",
    "HOLLOW","NOOK","WARD","GATE","MAZE","CELLS","SHRINE","REST",
};

static const char *const cavern_pref[] = {
    "DEEP","STONE","ECHO","DARK","DRIP","DAMP","JAGGED","HOLLOW","GLIMMER","SHADE",
    "CRYSTAL","AMBER","MOSS","FUNGAL","BLIND","WHISPER","RUMBLE","GLOW","MURK","ROOT",
};
static const char *const cavern_suf[] = {
    "CAVERN","HOLLOW","GROTTO","DELVE","DEPTHS","DEN","MAW","WARREN","CHASM","TUNNEL",
    "RIFT","HOLE","PIT","VEIN","LAIR","ECHO","ROOST","CAVE",
};

static const char *const people_names[] = {
    "GRETA","OSWALD","MABEL","BRUNO","HILDA","WALTER","AGNES","EDWIN","MARGE","CORWIN",
    "FENWICK","ROWAN","BRIGID","DUNCAN","PIPPA","GARRICK","NELL","AMBROSE","TILDA","WREN",
    "GIDEON","MYRA","ELSPETH","FINN","HAZEL","IVO","JORAH","KESTREL","LOTTIE","BARTHOLD",
};

#define TOWN_PREF_N   (sizeof(town_pref)   / sizeof(town_pref[0]))
#define TOWN_SUF_N    (sizeof(town_suf)    / sizeof(town_suf[0]))
#define DGN_PREF_N    (sizeof(dgn_pref)    / sizeof(dgn_pref[0]))
#define DGN_SUF_N     (sizeof(dgn_suf)     / sizeof(dgn_suf[0]))
#define CRYPT_PREF_N  (sizeof(crypt_pref)  / sizeof(crypt_pref[0]))
#define CRYPT_SUF_N   (sizeof(crypt_suf)   / sizeof(crypt_suf[0]))
#define CAVERN_PREF_N (sizeof(cavern_pref) / sizeof(cavern_pref[0]))
#define CAVERN_SUF_N  (sizeof(cavern_suf)  / sizeof(cavern_suf[0]))
#define PEOPLE_N      (sizeof(people_names)/ sizeof(people_names[0]))

// ── Hash (never rand() — see biome.c:127: floor layouts must not shift) ─────
static uint16_t nm_hash(uint16_t seed, uint8_t a, uint8_t b, uint8_t salt) {
    uint16_t h = (uint16_t)(seed ^ (uint16_t)((uint16_t)(a + 1u) * 2053u));
    h ^= (uint16_t)(h >> 8);
    h ^= (uint16_t)((uint16_t)(b + salt + 1u) * 6361u);
    h ^= (uint16_t)(h >> 7);
    return h;
}

static void copy1(const char *s, char *out, uint8_t cap) {
    uint8_t i = 0u;
    if (cap == 0u) return;
    while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
}

static void compose2(const char *a, const char *b, char *out, uint8_t cap) {
    uint8_t i = 0u, j;
    if (cap == 0u) return;
    while (a[i] && (uint8_t)(i + 1u) < cap) { out[i] = a[i]; i++; }
    for (j = 0u; b[j] && (uint8_t)(i + 1u) < cap; j++) { out[i] = b[j]; i++; }
    out[i] = 0;
}

// pi/si drawn from two independent hash rounds; si folds pi back in (same
// decorrelation trick story_ui.c uses for its continent-name suffix draw).
static void pick_pref_suf(uint16_t seed, uint8_t kind, uint8_t id,
                           uint8_t pn, uint8_t sn, uint8_t *pi, uint8_t *si) {
    uint16_t h1 = nm_hash(seed, kind, id, 1u);
    uint16_t h2;
    *pi = (uint8_t)(h1 % pn);
    h2 = nm_hash(seed, kind, id, 2u) ^ (uint16_t)((uint16_t)(*pi) * 13u);
    *si = (uint8_t)(h2 % sn);
}

BANKREF(town_name_copy)
void town_name_copy(uint8_t town_id, char *out, uint8_t cap) BANKED {
    uint8_t pi, si;
    pick_pref_suf(run_seed, 0xA0u, town_id, (uint8_t)TOWN_PREF_N, (uint8_t)TOWN_SUF_N, &pi, &si);
    compose2(town_pref[pi], town_suf[si], out, cap);
}

// Flavor follows the dungeon's actual rolled biome (same hash biome_apply_floor_kind
// and biome_pick_for_floor already use), so the name never mismatches the visuals.
BANKREF(dungeon_name_copy)
void dungeon_name_copy(uint8_t dungeon_id, char *out, uint8_t cap) BANKED {
    uint8_t biome = biome_pick_for_floor(DUNGEON_BASE_FLOOR(dungeon_id), run_seed);
    const char *const *pref = dgn_pref;
    const char *const *suf  = dgn_suf;
    uint8_t pn = (uint8_t)DGN_PREF_N, sn = (uint8_t)DGN_SUF_N;
    uint8_t pi, si;
    if (biome == BIOME_CRYPT)       { pref = crypt_pref;  suf = crypt_suf;  pn = (uint8_t)CRYPT_PREF_N;  sn = (uint8_t)CRYPT_SUF_N; }
    else if (biome == BIOME_CAVERN) { pref = cavern_pref; suf = cavern_suf; pn = (uint8_t)CAVERN_PREF_N; sn = (uint8_t)CAVERN_SUF_N; }
    pick_pref_suf(run_seed, 0xD0u, dungeon_id, pn, sn, &pi, &si);
    compose2(pref[pi], suf[si], out, cap);
}

BANKREF(npc_name_copy)
void npc_name_copy(uint8_t town_id, uint8_t npc_i, char *out, uint8_t cap) BANKED {
    uint16_t h = nm_hash(run_seed, (uint8_t)(0xE0u + town_id), npc_i, 3u);
    copy1(people_names[h % PEOPLE_N], out, cap);
}
