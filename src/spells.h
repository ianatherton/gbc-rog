#ifndef SPELLS_H
#define SPELLS_H

#include <stdint.h>
#include <gbdk/platform.h>
#include "ability_dispatch.h" // AbilityResult

/* ── Spell identity ──────────────────────────────────────────────────────────
   Global spell id = (class << 3) | idx, idx 0..SPELLS_PER_CLASS-1 (2 spare per
   class). Stride 8 (not ×6) keeps HOME dispatch a shift (no SDCC division
   helper) and makes the scroll item-kind range → spell_id an identity map.
   WRAM arrays (spell_rank / spell_cd / belt_spell) hold the LOCAL idx of the
   active class only — a player can never learn a foreign class's spell; only
   generic scrolls carry full cross-class ids. */
#define SPELLS_PER_CLASS   6u
#define SPELL_ID(cls, idx) ((uint8_t)(((uint8_t)(cls) << 3) | (uint8_t)(idx)))
#define SPELL_ID_CLASS(id) ((uint8_t)((id) >> 3))
#define SPELL_ID_IDX(id)   ((uint8_t)((id) & 7u))
#define SPELL_ID_SPAN      32u   // 4 classes × stride 8; spell-def table length
#define SPELL_IDX_NONE     0xFFu // empty belt_spell[] slot
#define SPELL_RANK_MAX     3u    // default max_rank (per-spell override in SpellDef)

/* ── Bank-27 spell-info API. Strings live in bank 27 — copy-out ONLY (same
   trap items.c documents for bank 13); there are no raw-pointer getters. ── */
void    spells_name_copy(uint8_t spell_id, char *out, uint8_t cap) BANKED;
void    spells_desc_copy(uint8_t spell_id, char *out, uint8_t cap) BANKED;
uint8_t spells_icon(uint8_t spell_id, uint8_t *pal_out) BANKED; // VRAM tile idx + ready-state palette
uint8_t spells_unlock_level(uint8_t spell_id) BANKED;           // min player_level to spend the FIRST point
uint8_t spells_max_rank(uint8_t spell_id) BANKED;
uint8_t spells_cooldown_for(uint8_t spell_id, uint8_t rank) BANKED; // base - step*(rank-1); rank 0 → 0
uint8_t spells_exists(uint8_t spell_id) BANKED;                 // 0 for gap rows / not-yet-designed spells

void    spells_new_run_reset(void) BANKED;  // pts=0, ALL ranks 0 (nothing learned), belt all NONE, cds=0
uint8_t spells_learned_count(void) BANKED;  // distinct spells with rank > 0; learning locks in at 2 (pick-2 rule)
void    spells_floor_reset(void) BANKED;    // per-floor: zero spell_cd[] + book_heal_cooldown_turns
void    spells_tick_cooldowns(void) BANKED; // one turn: decrement spell_cd[] + book_heal_cooldown_turns
void    spells_notify_recharging(uint8_t turns) BANKED; // "Recharging: N turns" combat-log line
void    spells_cast_scroll(uint8_t spell_id, AbilityResult *out) BANKED; // rank-0 cast, any class; no cooldown touched

#endif // SPELLS_H
