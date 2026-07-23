#pragma bank 27

/* Spell system core (bank 27) — the data-driven side of class spells.
   Effect code stays in the per-class banks 6-9 (ability_<class>_cast); this
   bank owns metadata (names/icons/gating/cooldown curves), the shared cooldown
   engine, and (later) the generic-scroll cast shim. HOME (ability_dispatch.c)
   routes; nothing here is on a per-frame hot path. */

#include "spells.h"
#include "ability_dispatch.h"
#include "globals.h"
#include "ui.h"
#include "defs.h"
#include <gbdk/platform.h>

typedef struct {
    uint8_t icon_tile;     // VRAM tile index for the belt/subscreen icon
    uint8_t icon_pal;      // ready-state palette (cooldown grey is PAL_CORPSE, applied by ui.c)
    uint8_t unlock_level;  // min player_level to spend the FIRST point (learn)
    uint8_t max_rank;      // training cap
    uint8_t base_cooldown; // turns at rank 1
    uint8_t cd_step;       // subtracted per rank above 1 (clamped at 0)
    const char *name;      // bank-27 literal — copy-out only
    const char *desc;      // one short line for the SPELL subscreen
} SpellDef;

#define NOSPELL {0u, 0u, 0u, 0u, 0u, 0u, 0, 0}

/* Indexed directly by global spell id (stride 8/class; gap rows = NOSPELL).
   Adding a class's spell #2..#6 = fill one row here + one case in its bank. */
static const SpellDef spell_defs[SPELL_ID_SPAN] = {
    /* ── Knight (0..7) ── */
    {TILE_KNIGHT_SHIELD_VRAM, PAL_WALL_BG, 1u, SPELL_RANK_MAX, 0u, 0u,
     "Holy Fire Shield", "Reflect dmg while up"},
    NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL,
    /* ── Scoundrel (8..15) ── */
    {TILE_FOX_J9_VRAM, PAL_XP_UI_BG, 1u, SPELL_RANK_MAX, 0u, 0u,
     "Call Fox", "Summon a fox ally"},
    NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL,
    /* ── Witch (16..23) ── */
    {TILE_WITCH_BOLT_VRAM, PAL_WALL_BG, 1u, SPELL_RANK_MAX, 0u, 0u,
     "Fetid Bolt", "Bolt nearest enemy"},
    {TILE_ROOT_ICON_VRAM, PAL_XP_UI_BG, 1u, SPELL_RANK_MAX, 10u, 2u,
     "Swamp Root", "Root visible enemies"},
    NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL,
    /* ── Zerker (24..31) ── */
    {TILE_ZERKER_WHIRLWIND_VRAM, PAL_WALL_BG, 1u, SPELL_RANK_MAX, 6u, 1u,
     "Whirlwind", "Hit all adjacent foes"},
    NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL, NOSPELL,
};

static void copy_capped(const char *s, char *out, uint8_t cap) {
    uint8_t i = 0u;
    if (cap == 0u) return;
    if (s) while (s[i] && (uint8_t)(i + 1u) < cap) { out[i] = s[i]; i++; }
    out[i] = 0;
}

void spells_name_copy(uint8_t spell_id, char *out, uint8_t cap) BANKED {
    copy_capped((spell_id < SPELL_ID_SPAN) ? spell_defs[spell_id].name : 0, out, cap);
}

void spells_desc_copy(uint8_t spell_id, char *out, uint8_t cap) BANKED {
    copy_capped((spell_id < SPELL_ID_SPAN) ? spell_defs[spell_id].desc : 0, out, cap);
}

uint8_t spells_icon(uint8_t spell_id, uint8_t *pal_out) BANKED {
    if (spell_id >= SPELL_ID_SPAN || !spell_defs[spell_id].name) {
        *pal_out = PAL_UI;
        return (uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY);
    }
    *pal_out = spell_defs[spell_id].icon_pal;
    return spell_defs[spell_id].icon_tile;
}

uint8_t spells_unlock_level(uint8_t spell_id) BANKED {
    return (spell_id < SPELL_ID_SPAN) ? spell_defs[spell_id].unlock_level : 255u;
}

uint8_t spells_max_rank(uint8_t spell_id) BANKED {
    return (spell_id < SPELL_ID_SPAN) ? spell_defs[spell_id].max_rank : 0u;
}

uint8_t spells_cooldown_for(uint8_t spell_id, uint8_t rank) BANKED {
    uint8_t base, dec;
    if (spell_id >= SPELL_ID_SPAN || rank == 0u) return 0u;
    base = spell_defs[spell_id].base_cooldown;
    dec  = (uint8_t)(spell_defs[spell_id].cd_step * (uint8_t)(rank - 1u));
    return (dec >= base) ? 0u : (uint8_t)(base - dec);
}

uint8_t spells_exists(uint8_t spell_id) BANKED {
    return (uint8_t)(spell_id < SPELL_ID_SPAN && spell_defs[spell_id].name != 0);
}

void spells_new_run_reset(void) BANKED {
    uint8_t i;
    player_spell_points = 0u;
    // Every class starts with NOTHING learned: the first spell point arrives at level 2,
    // and the first two DISTINCT spells trained become the run's locked-in pair
    // (spells_learned_count — enforced on the SPELL subscreen).
    for (i = 0u; i < SPELLS_PER_CLASS; i++) { spell_rank[i] = 0u; spell_cd[i] = 0u; }
    for (i = 0u; i < BELT_SLOT_COUNT; i++) belt_spell[i] = SPELL_IDX_NONE;
}

uint8_t spells_learned_count(void) BANKED { // distinct spells with rank > 0 (lock-in gate at 2)
    uint8_t i, n = 0u;
    for (i = 0u; i < SPELLS_PER_CLASS; i++)
        if (spell_rank[i] > 0u) n++;
    return n;
}

void spells_floor_reset(void) BANKED {
    uint8_t i;
    for (i = 0u; i < SPELLS_PER_CLASS; i++) spell_cd[i] = 0u;
    book_heal_cooldown_turns = 0u;
}

void spells_tick_cooldowns(void) BANKED {
    uint8_t i;
    for (i = 0u; i < SPELLS_PER_CLASS; i++)
        if (spell_cd[i] > 0u) spell_cd[i]--;
    if (book_heal_cooldown_turns > 0u) book_heal_cooldown_turns--;
}

/* Generic-scroll cast: rank 0 = the weak variant, castable by any class. Routes through
   the HOME dispatcher (always mapped — a plain call from here is legal) into the class
   bank that owns the effect, so scrolls share the exact spell cores with zero duplication.
   Deliberately never touches spell_cd[] — the cooldown set lives only in the belt path. */
void spells_cast_scroll(uint8_t spell_id, AbilityResult *out) BANKED {
    ability_dispatch_cast(spell_id, 0u, g_player_x, g_player_y, out);
}

void spells_notify_recharging(uint8_t turns) BANKED {
    char buf[20];
    const char *s = "Recharging: ";
    uint8_t i = 0u;
    while (*s) buf[i++] = *s++;
    buf[i++] = (char)('0' + (turns > 9u ? 9u : turns));
    s = " turns";
    while (*s) buf[i++] = *s++;
    buf[i] = 0;
    ui_combat_log_push(buf);
}
