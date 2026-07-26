#pragma bank 30

// Cold gameplay helpers evicted from bank 2 (chronically full — docs/BANKS.md).
// The belt helpers run on a SELECT edge only. The zone_* pair below runs once per attempted
// move instead — still far-call-cheap (one trampoline per turn, against a turn that already
// far-calls into combat/sprites/ui), and it buys back the bank-2 room the encounter hooks need.

#include <gbdk/platform.h>
#include <stdint.h>
#include "defs.h"
#include "globals.h"
#include "items.h"
#include "ui.h"
#include "ability_dispatch.h"
#include "gameplay_cold.h"
#include "map.h"
#include "biome.h"
#include "biome_encounter.h"
#include "dungeon.h"

// ── Zone interaction / transition classification (evicted from state_gameplay.c) ─────────────
// Both are pure classifiers: they never move the player and never consume a turn themselves —
// bank 2 keeps the turn bookkeeping and just acts on the returned code.

// What bumping (nx,ny) does when the cell is not occupied by an enemy.
//   0 = pass through (walk onto it)   1 = blocked, no turn   2 = breakable destroyed, turn spent
BANKREF(zone_bump_at)
uint8_t zone_bump_at(uint8_t nx, uint8_t ny, uint8_t t) BANKED {
    if (t == TILE_WALL) {
        // Barrels are blocking wall cells in both towns and encounters (FLOORKIND_ENCOUNTER > TOWN).
        if (floor_kind >= FLOORKIND_TOWN && town_barrel_try_break(nx, ny)) return 2u;
        if (floor_kind == FLOORKIND_ENCOUNTER && encounter_chest_open(nx, ny)) return 2u;
        return 1u;
    }
    if (floor_kind == FLOORKIND_TOWN && town_npc_blocks(nx, ny)) return 1u; // a villager's tile blocks like a wall
    return 0u;
}

// Whether (nx,ny) is a transition tile, and its CONFIRM_* kind. The walk itself always happens
// (full turn — enemies act); state_gameplay arms the prompt only after the player has landed.
BANKREF(zone_confirm_at)
uint8_t zone_confirm_at(uint8_t nx, uint8_t ny, uint8_t t, uint8_t *aux) BANKED {
    *aux = 0u;
    // Boss floor renders its pit as the exit portal once the boss is dead (boss_alive gates it).
    if (t == TILE_PIT && !boss_alive)
        return (floor_kind == FLOORKIND_BOSS) ? CONFIRM_BOSS_EXIT : CONFIRM_PIT;
    // An encounter has no wall ring: the border IS the way out, so every edge cell prompts. Checked
    // before the spawn-cell test because the spawn sits at the map centre, not on an edge.
    if (floor_kind == FLOORKIND_ENCOUNTER
        && (nx == 0u || ny == 0u
            || nx == (uint8_t)(active_map_w - 1u) || ny == (uint8_t)(active_map_h - 1u)))
        return CONFIRM_UP;
    if (((nx == player_spawn_x && ny == player_spawn_y)
         || (floor_kind == FLOORKIND_TOWN && town_exit_at(nx, ny))) // any of the 4 town road mouths
        && floor_num > 0u
        && !boss_alive) // boss_alive is only ever set on boss/miniboss floors
        return CONFIRM_UP;
    if (floor_kind == FLOORKIND_HUB) {
        // '?' encounter marker. Checked before the prefab features: markers never place on a
        // feature footprint (encounter_markers_build rejects those cells), so order is cosmetic,
        // but markers are the common case on an otherwise empty continent.
        uint8_t m = encounter_marker_at(nx, ny);
        if (m != 255u) { *aux = m; return CONFIRM_ENCOUNTER; }
    }
    if (floor_biome == BIOME_OVERWORLD) {
        // One trigger scan, then dispatch — the bank-2 original scanned ow_features twice per step.
        uint8_t ft = overworld_trigger_at(nx, ny);
        if (ft == OW_FEAT_ENTRANCE) {
            // Overworld cave-mouth: entry into that dungeon's guardroom via the port path.
            // Each of the 9 entrances is its own dungeon (dungeon.h floor scheme).
            uint8_t did = overworld_entrance_id_at(nx, ny);
            if (did < DUNGEON_COUNT) {
                if (dungeon_complete_mask & (uint16_t)((uint16_t)1u << did))
                    return CONFIRM_SEALED; // message only (deduped); A does nothing
                *aux = did;
                return CONFIRM_ENTRANCE;
            }
        } else if (ft == OW_FEAT_TOWN) {
            uint8_t tid = overworld_town_id_at(nx, ny);
            if (tid < TOWN_COUNT) { *aux = tid; return CONFIRM_TOWN; } // A → TOWN_FLOOR_BASE+tid
        }
    }
    return CONFIRM_NONE;
}

// Spell slots are data-driven: belt_spell[] holds the active class's local spell
// idx (assigned on the SPELL subscreen); names come out of bank 27 via copy-out.
static uint8_t belt_spell_learned(uint8_t slot) { // local idx if slot holds a learned spell, else SPELL_IDX_NONE
    uint8_t idx = belt_spell[slot];
    if (idx >= SPELLS_PER_CLASS || spell_rank[idx] == 0u) return SPELL_IDX_NONE;
    return idx;
}

static uint8_t belt_slot_nonempty(uint8_t slot) {
    if (slot < BELT_SLOT_COUNT) return belt_spell_learned(slot) != SPELL_IDX_NONE;
    {
        uint8_t kind = inventory_kind[slot - BELT_SLOT_COUNT];
        return kind != ITEM_KIND_NONE;
    }
}

BANKREF(belt_select_advance_skip_empty)
void belt_select_advance_skip_empty(void) BANKED {
    uint8_t i;
    for (i = 0u; i < BELT_TOTAL_SLOTS; i++) {
        selected_belt_slot = (uint8_t)((selected_belt_slot + 1u) % BELT_TOTAL_SLOTS);
        if (belt_slot_nonempty(selected_belt_slot)) break;
    }
}

BANKREF(push_selected_belt_description)
void push_selected_belt_description(void) BANKED {
    char buf[20];
    if (selected_belt_slot < BELT_SLOT_COUNT) {
        uint8_t idx = belt_spell_learned(selected_belt_slot);
        if (idx == SPELL_IDX_NONE) return;
        spells_name_copy(SPELL_ID(player_class, idx), buf, sizeof buf);
        ui_combat_log_push(buf);
    } else {
        uint8_t belt_idx = selected_belt_slot - BELT_SLOT_COUNT;
        uint8_t kind = inventory_kind[belt_idx];
        if (kind == ITEM_KIND_NONE) return;
        items_kind_display_name_copy(kind, inventory_mod_level[belt_idx], buf, sizeof buf);
        if (items_kind_category(kind) == ITEM_CAT_CONSUMABLE) {
            uint8_t p = 0u, split, cnt = inventory_count[selected_belt_slot - BELT_SLOT_COUNT];
            while (buf[p]) p++;
            split = (uint8_t)(p + 1u); // "x" starts after the space
            buf[p++] = ' '; buf[p++] = 'x';
            if (cnt >= 100u) { buf[p++] = (char)('0' + cnt / 100u); cnt = (uint8_t)(cnt % 100u); }
            if (cnt >= 10u)  { buf[p++] = (char)('0' + cnt / 10u);  cnt = (uint8_t)(cnt % 10u);  }
            buf[p++] = (char)('0' + cnt);
            buf[p] = 0;
            ui_combat_log_push_gold_suffix(buf, split);
        } else {
            ui_combat_log_push(buf);
        }
    }
}
