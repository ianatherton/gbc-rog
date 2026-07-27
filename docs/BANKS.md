# ROM bank map, WRAM budget, and scaling policy

Measured from `build/gbc/gbc-rog.map` / `.noi` (**banks 0/10/22/23/29 re-measured 2026-07-26** after the
2-tile-tall trees landed; the rest **re-measured 2026-07-25**, after the '?' encounter
system landed; the previous numbers were dated 2026-06-10 and had drifted badly — bank 0 was listed
as ~450 B free when it was ~160, and banks 6/8/10/20/24 were all understated).
Regenerate the numbers after any layout change: `make`, then check the `_CODE_n` / `_DATA`
area sizes at the bottom of the map file.

Cart: **MBC5 + RAM + battery (0x1B), 512 KB, 32 banks of 16,384 B, CGB-only (`-Wm-yC`)**.
CGB-only is required: explored/fog bits live in CGB banked WRAM (SVBK), which DMG lacks.
SRAM (battery RAM) is currently unused — free for saves later.

## ROM bank usage

| Bank | Used | % | Contents |
|------|------|----|----------|
| 0 (fixed) | 15,832 of 16,368 usable | 97% | main loop, ability_dispatch, ally, biome dispatch + `enemy_defs` HOME cache, enemy_extras, lcd, lighting (incl. SVBK fog/water/road accessors + `wram2_read_byte` batch reader), music driver + SFX, perf, seed_entropy, targeting, tileset_io, title_logo, ui_loading_isr, wall_palettes, SDCC runtime. **536 B free (re-measured 2026-07-26 as _CODE+_HOME+_GSINIT/_GSFINAL/_INITIALIZER; the older "~137 B" figure used a different, unreproducible reckoning) — keep HOME lean.** The 2-tall tree's two half-tile uploads in `main.c` cost 26 B of that, and the hero's 3rd walk frame added a 5th `set_sprite_data` to `biome_load_active` for 13 B. The lit-floor gates in `lighting.c` were rewritten from `floor_biome == BIOME_OVERWORLD \|\| floor_biome == BIOME_TOWN` to the range test `floor_kind == FLOORKIND_HUB \|\| floor_kind >= FLOORKIND_TOWN` — one compare instead of two, and it picks up encounters for free. `dungeon.h` keeps TOWN/ENCOUNTER adjacent specifically so that idiom stays valid; put any future lit outdoor kind at the top of that list. |
| 1 | 4,360 | 27% | tileset (png2asset output) |
| 2 | 16,116 | 98% | gameplay kernel: state_gameplay, map, render, camera, enemy. **268 B free (2026-07-25)** — recovered by evicting the per-move zone classifiers to bank 30 (`zone_confirm_at` / `zone_bump_at` in `gameplay_cold.c`, −268 B) before adding any encounter hooks; `enemy_stat_scale()` also collapsed to a `monster_level` byte read (the global was named `zone_stat_scale` until the 2026-07-27 monster-level revamp; it is now the number the HUD's skull shows, `DUNGEON_MONSTER_LEVEL` in `dungeon.h` being the single source of the dungeon ladder x2/x4/x6, encounters x1/x3/x5). **Still the tightest code bank — treat any addition as requiring an eviction first: do not add to this bank without evicting something first (tried hoisting a shared condition into a local var for the item-palette fix — SDCC generated basically the same size either way, ~36-37 B, so "write it cleverly" is not a lever here). Past eviction examples: axe/mace extras → bank 19 (2026-07-06), belt-description helpers → bank 30 `gameplay_cold.c` (2026-07-18); spell-cooldown reset/tick bodies → bank 27 `spells.c` (2026-07-23, freed ~20 B)** |
| 3 | 12,332 | 75% | all 10 UI states (title → game_over, incl. talk/trade — state_ability is now the full SPELL training/loadout screen) + class_palettes |
| 4 | 2,971 | 18% | bwv1043 music data |
| 5 | 13,163 | 80% | ui.c (incl. `ui_confirm_prompt_push` zone-confirm text). **HUD border art (2026-07-24)**: sheet tiles M1/M2 are now `TILE_UI_BORDER_V`/`TILE_UI_BORDER_H` — M2 is drawn by hand, M1 is *generated* as `rot90cw(M2)` by `tools/prep_assets.py` (the GBC can only X/Y-flip, never rotate, so a vertical rail needs its own tile; don't hand-draw M1, it gets overwritten). Both are sheet index < 128, so they ride main.c's bulk `set_bkg_data` — no boot copy, no borrowed VRAM slot. The belt row's dead tail (cols 16–19, freed when `BELT_SLOT_COUNT` went 4→2) is a horizontal M2 run; log rows 1–3 are framed by vertical rails at cols 0/19 (left rail = right rail with `S_FLIPX\|S_FLIPY`, since rot-CCW is the 180° of rot-CW). Panel text therefore lives in `UI_PANEL_TEXT_X`..+`UI_PANEL_TEXT_COLS` (cols 1–18, was 0–19) — the combat-log enemy-name cap dropped 9→8 chars to keep the worst-case line at 18. |
| 6 | 1,160 | 7% | abilities_knight (`ability_knight_cast(spell_idx, rank, ...)` — pure effect cores, no gating) |
| 7 | 724 | 4% | abilities_scoundrel (same shape) |
| 8 | 1,239 | 7% | abilities_witch (bolt + root, rank-scaling tables; cooldowns now central in bank 27/HOME) |
| 9 | 1,290 | 8% | abilities_zerker (whirlwind) |
| 10 | 8,361 | 51% | level_init, map_gen, biome_dungeon |
| 11 | 132 | 1% | biome_crypt |
| 12 | 132 | 1% | biome_cavern |
| 13 | 5,596 | 34% | items (kinds + affixes; incl. the 32 generic spell-scroll kinds 46..77 — `spell_id = kind - ITEM_KIND_SPELL_SCROLL_FIRST`) — incl. `town_barrel_try_drop_item` (30% loot roll, separate function from `enemy_try_drop_item`'s 10% so the two can never accidentally cross-tune) |
| 14 | 5,840 | 36% | story_ui + names (deterministic town/dungeon/NPC name generator, `src/names.c`) |
| 15 | 157 | 1% | scroll_blast |
| 16 | 644 | 3% | scroll_root, debuff_icon, bow_shoot |
| 17 | 13,993 | 85% | entity_sprites (incl. `refresh_town_npcs_oam` — wandering villager OAM, borrows the town's always-empty enemy run; the hero's 3-frame walk loop — phase advanced on a free-running clock in `entity_sprites_vbl_tick`, tile picked from `player_walk_tiles[]`), scoundrel_fox |
| 18 | 5,460 | 33% | bwv527 music data (moved out of bank 5) |
| 19 | 1,995 | 12% | combat (moved out of bank 2; per-turn, far-call boundary is cheap) + `combat_player_melee_extras` (axe cleave / mace stun, evicted from bank 2) |
| 20 | 1,370 | 8% | equipment (`EquipStatDef` table, `items_equip_apply`, `items_equip_slot`, `equipped_kind_in_slot`) |
| 21 | 132 | 1% | biome_boss |
| 22 | 11,794 | 72% | biome_overworld (top-level hub, floor 0; no enemies/items) + render_palettes (incl. town field/brick/stone/foliage branches) + batched strip classifiers (`overworld_classify_col/row_strip` — one banked entry per camera strip, mask bytes via `wram2_read_byte`) + town render overlay/step features (villagers are OAM now, not drawn here; doorway G1/G2 + barrel props drawn here) |
| 24 | 4,846 | 29% | biome_boss2 (Sphinx roster/art; overlaid onto any dungeon's boss floor by biome_apply_floor_kind) + bosses (png2asset sphinx sheet, res/bosses.png). 10-tile sprite uploaded to VRAM scratch + re-uploaded per frame by sphinx_anim_tick for a 2-frame leg cycle + faster wingbeat |
| 28 | 459 | 3% | dungeon_floors (miniboss elite art: runtime 2x pixel-doubler of elite_base_type's sprite → quadrant VRAM slots; floor-kind scheme in src/dungeon.h) |
| 29 | 5,563 | 34% | biome_town (town interiors, floors 46–48 = `TOWN_FLOOR_BASE`+0..2: 59..96-square safe zone sized by building count — pine border ring + brick town wall, 2-tile-wide sand road cross exiting N/S/E/W (`town_exit_at` = border cell + road bit, no stored table), 5–20 buildings — first `MAX_TOWN_NPCS` (8) open (G1 doorway, roofed walkable interior, villager, `SIGN_KIND_BUILDING` door sign), the rest closed/decorative (G2 door, never carved, no villager, still signed) — deco pines + destructible + persistent barrels (F2, ~half the buildings plus rare stray ones; `town_barrel_try_break` — 1 hit, 30% loot roll via `town_barrel_try_drop_item`, same poof art via bank-17 `entity_sprites_run_barrel_poof`; each barrel's placement-order ordinal rides in `OwFeature.aux` and keys a bit in `town_barrels_broken` (globals.h, `TOWN_COUNT*3` B) so a broken barrel never re-spawns on re-entry this run — same trick as `floor_items_picked` for dungeon ground items, viable because town layout is fully deterministic from (run_seed, town_id)), heal fountain; roofs = fog buffer reused (`townroof_*`), lifted per-building by `town_roof_update`; villagers wander + slide via `town_npcs_tick`/`town_npc_blocks` (real OAM sprites, drawn+glided in bank 17) — lazy random walk, solid collision on their tile (bumping one starts a conversation via `overworld_signpost_read`, or opens STATE_TALK for the trader), warp home past `TOWN_NPC_ROAM_RADIUS`; fully lit like the hub) |
| 30 | 3,432 | 20% | auto_explore (A-button auto-explore: cached-path BFS, DCSS-style stop-on-sight/stop-on-hit, auto-pickup, walk-to-ladder when explored; private SVBK bank-3 accessors) + gameplay_cold (belt-description helpers, plus the per-move zone classifiers `zone_confirm_at` (which CONFIRM_* a step arms: pit/boss-exit/stairs/town mouth/encounter border/hub feature/'?' marker) and `zone_bump_at` (0 pass / 1 blocked / 2 breakable destroyed — town+encounter barrels, encounter chests). Both were evicted from bank 2 in 2026-07 to make room for encounters; they run once per attempted move, which is cheap against a turn that already far-calls combat/sprites/ui) |
| 27 | 1,638 | 9% | spells.c — spell system core: `SpellDef` metadata table (names/icons/gating/cooldown curves, copy-out string API), cooldown engine (`spells_floor_reset/tick_cooldowns`), `spells_cast_scroll` (rank-0 generic cast for any class, routes through HOME `ability_dispatch_cast` into banks 6-9) |
| 23 | 3,625 | 22% | biome_encounter — hub '?' encounters. Two halves: (a) MARKERS on floor 0, a set that is a pure hash of (run_seed, `world_tick`, region) with positions parked in the hub's always-empty enemy arrays (`num_enemies` stays 0 so nothing else sees them) — `world_tick` bumps on every hub arrival (`level_init.c`), which both consumes the marker just entered and reshuffles the rest, so the whole system stores 7 bytes and no arrays; (b) INTERIORS on `ENCOUNTER_FLOOR` (49 — the one index `MAX_FLOORS` left free, reused by every encounter, persistence keys wiped on entry). `ENC_DEFS[]` templates are terrain-free (size/enemies/barrels/chest/clutter/roster); terrain art, palette AND difficulty tier all come from `enc_region`, the hub region the '?' stood in — the two axes are independent so any template works in any region. Adding an encounter is one table row. |
| 25–26, 31 | 0 | 0% | empty — 48 KB free |

Total ROM used ≈ 133 KB of 512 KB (~26%). ROM is not the constraint. If it ever is,
MBC5 goes to 8 MB: bump `-Wl-yo32` in the Makefile (64/128/…), nothing else changes.

## Bank allocation policy (where new content goes)

- **6–9** class abilities/spells (one bank per class, each ~99% free — pure effect cores: `ability_<class>_cast(spell_idx, rank, ...)`; adding a spell = 1 `SpellDef` row in bank 27 + 1 case here)
- **27** spell system core (metadata/cooldowns/scroll shim — see table)
- **10–12, 21 (boss), 22 (overworld), 24 (boss2/sphinx), 28 (dungeon_floors); reserve 29+** biomes + map gen (a new
  biome is one bank file + one row in `biome_table` in `src/biome.c` + a `BIOME_*` id in
  `src/biome.h`)
- **13, reserve 23** items (consumables, inventory management) + affixes
- **20, reserve 21** equipment (stat table, slot system)
- **15/16, reserve 24** scrolls / castable item effects
- **17, reserve 25** entity sprite data (VRAM slots are the real creature cap — see below)
- **4, 18, reserve 26+** music data (one track per bank; keep ui.c alone in bank 5)
- **14** story/text
- Never share a bank between an engine module that grows (ui, gameplay kernel) and asset data.

Bank discipline: `#pragma bank N` at the top of the file is the single source of truth.
Cross-bank calls must be `BANKED` (or dispatched like `biome_load_active`, which switches
ROM around a plain call). The linker will NOT catch a near call into the wrong bank.

## Game states → banks

Flow: `Boot → TITLE → CHAR_CREATE → GAMEPLAY ⇄ modals(STATS↔ABILITY, INVENTORY, MAP, PICKUP, TALK) → TRANSITION → (next floor | GAME_OVER → TITLE)`

| State | Primary bank | Far-calls into |
|-------|--------------|----------------|
| Boot (`main`) | 0 | 1 (tileset→VRAM), 17 (sprites), 4/18 (music data) |
| TITLE | 3 | 5 (title fx), 0 (title_logo), 4/18 (music) |
| CHAR_CREATE | 3 | 3 (class_palettes), 5 (ui) |
| GAMEPLAY enter | 2 | 14 (story crawl, first floor), 10 (level_init/map_gen), 10/11/12 (biome roster), 17 (sprites), 5 (HUD) |
| GAMEPLAY tick | 2 | 19 (combat), 0→6/7/8/9 (abilities by class), 27 (spell cooldowns/metadata), 13 (items), 15/16 (scrolls), 5 (ui/log), 17 (sprites), 0 (ally/lighting/targeting) |
| STATS / ABILITY | 3 | 5 (ui); ABILITY (SPELL screen) also 27 (spell names/descs/training data) |
| INVENTORY / PICKUP | 3 | 5 (ui), 13 (items), 20 (equipment), 17 (cursor) |
| TALK (trade) | 3 | 5 (ui/log), 13 (items + drop table), 17 (cursor); entered from 29 (`town_npc_blocks` sets `next_state` — bank 2 is full) |
| MAP | 3 | 5 (ui), fog via lighting.c (bank 0) |
| TRANSITION | 3 | pit → 10/11/12 regen |
| GAME_OVER | 3 | 5 (ui) |

## WRAM budget

Fixed WRAM (0xC000–0xDFFF): `_DATA` = 7,533 B + `_INITIALIZED` 45 B, ending 0xDE3A (measured
2026-07-25) → **~454 B stack headroom — this is load-bearing, treat it as a floor**:
`class_emblem_draw` (state_char_create.c) alone puts ~336 B of buffers on the stack, plus
banked-call depth and the audio ISR. Adding ~90 B of town tables to globals.c once pushed `_DATA`
to 0xDE3C and the char-create screen overran the stack into `_DATA` → garbled tiles from the class
symbols onward.

**The encounter system deliberately spent 10 B of this** (`world_tick`, `enc_marker_count`,
`enc_template`, `enc_region`, `enc_return_x/y`, `monster_level`) and that was the whole design
constraint: marker POSITIONS ride the hub's idle `enemy_x/enemy_y/enemy_type` arrays (`num_enemies`
stays 0, so combat/`move_enemies`/`enemy_occ` never see them), and the marker SET is recomputed from
`hash(run_seed, world_tick, region)` instead of being stored. Any future addition here should look
for a comparable idle resource first — the town building/villager tables OVERLAY `nav_nodes[]`
(288 B) via `town_state` (map.h) on the same reasoning, and encounter floors likewise set
`num_nav_nodes = 0` (open ground: `step_nav_chase` in enemy.c falls back to `step_direct`, which is
the correct behaviour there anyway), so that 288 B is free on encounter floors too if ever needed.
If real headroom is required: auto_explore's BFS queue + path buffer (256 + 48 B) can move to SVBK
bank 3 at 0xDD80+.

Top consumers:
- 3 × 1,152 B map bitsets (`floor_bits`, `pit_bits`, `enemy_occ`) = 3,456 B
- `nav_nodes` 288 B (doubles as `town_state` on town floors — building/exit/roof-owner tables);
  enemy parallel arrays ~250 B + corpses; file statics ~1.5 KB (ui log, lcd/render scratch,
  map_gen temporaries)
- `floor_bits` doubles as story-crawl scratch before the first `generate_level` (story_ui.c)

### CGB banked WRAM (SVBK)

| WRAM bank | Range (mapped at 0xD000) | Contents |
|-----------|--------------------------|----------|
| 1 (default) | 0xD000–0xDFFF | tail of `_DATA` + stack — SVBK must always be restored to 1 |
| 2 | 0xD000–0xD47F | explored/fog bits (1,152 B) — access ONLY via `lighting.c`. **Town floors reuse this buffer as the building ROOF bitmask** (`townroof_set/clear_all` aliases in map.h; render reads bytes via `wram2_read_byte`) — never aliases fog: towns don't read fog, and every dungeon floor re-clears the buffer in `lighting_reset` |
| 2 | 0xD480–0xD8FF | hub water mask (1,152 B) — `overworld_water_*` in `lighting.c`; hub-only, never aliases fog |
| 2 | 0xD900–0xDD7F | road mask (1,152 B) — `road_*` in `lighting.c`; hub roads AND town roads (each gen `road_clear_all()`s first), rendered as open sand |
| 2 | 0xDD80–0xDFFF | free (~640 B; keep data below ~0xDF00) |
| 3 | 0xD000–0xD47F | auto-explore BFS visited bitmap (1,152 B) — private `av_*` accessors in `auto_explore.c`; scratch, valid only within one `ax_bfs()` call |
| 3 | 0xD480–0xDD7F | auto-explore BFS parent-direction map, 2 bits/tile (2,304 B) — private `ap_*` accessors; never cleared (only current-flood tiles are read back), used to extract the cached path |
| 3 | 0xDD80–0xDFFF | free (~640 B; keep data below ~0xDF00) |
| 4–7 | — | free (16 KB) — future per-floor data, bigger maps, more creature stats |

Rules for adding banked-WRAM data (the `exp2_*` accessors in `src/lighting.c` are the template):
1. Accessors are `__naked` asm: nothing may touch the stack while SVBK ≠ 1
   (the stack itself lives in banked memory at 0xDFxx). Put them in a HOME file when callers
   span banks; private `static` accessors may live in the owning banked file (near calls only —
   `av_*` in `auto_explore.c` does this).
2. `di` before switching, `ei` after restoring — an ISR push while switched lands in the wrong bank.
3. Never call the accessors from an ISR (the `ei` would re-enable interrupts inside it).
4. Keep banked-WRAM data below ~0xDF00 so it can never collide with stack pushes during the window.

## Known caps to watch

- **VRAM sprite tile slots** cap creature variety before ROM does — most entity sprite tiles are
  still uploaded at boot (`main.c`) into borrowed slots. The per-biome path now exists: a biome's
  enemy art can live in its own `res/enemies_<biome>.png` (png2asset → `src/enemies_<biome>.c`,
  one bank per sheet) and be uploaded into a VRAM scratch region by `biome_load_active()` on floor
  entry. Currently only BIOME_MINIBOSS uses it (the 2x slime, frame-1 in 4 dead BG cells +
  frame-2 in Skeleton/Rat/BigSkell slots, restored on other floors). Migrating the rest lets the
  boot-time borrowed slots be reclaimed — that's the real "plenty of space" win, deferred.
- **BG VRAM slots ≥182** (title-safe: `title_logo_bkg_vram_slot[]` only patches/restores 128–181)
  are the scarce resource for hub/town BG art. 2026-07-26 towns and '?' encounters got a 2-tile-tall
  tree (L15 canopy → **193 (B5)**, L16 trunk → **212 (E6)**); the hub keeps its 1-tile C10 pine on
  213 (F6), so hub forests are unchanged. **A free slot has to clear three tests, not one:** (a) no
  `TILE_*` constant equals the sheet index and no `_VRAM` constant equals the slot; (b) it is ≥182,
  outside `title_logo_bkg_vram_slot[]`; (c) it is outside every multi-tile START+COUNT range — today
  that means **176..191**, the char-create 2x class emblem (`state_char_create.c`), which restores
  those 16 slots from sheet tiles 48..63 on exit. Test (c) is the one that bites: the canopy was
  first put on 191 (P4) and came back as P4's dither after character creation. No verified-free
  title-safe slot is left after this change — freeing one means evicting boot-copied art (see the
  per-biome sprite-sheet path below).
- **Test (c) does NOT apply to art re-uploaded per floor**, only to art boot-copied once. 2026-07-26
  the hero's 3rd walk frame (K16) took **186 (K4)**, which is inside the 176..191 emblem range:
  `biome_load_active` re-uploads the whole hero set on every floor entry and always runs after char
  create, so the emblem restore can't outlive it — the same reason `TILE_PLAYER_HELMET_VRAM` (177)
  has always worked. If you need another slot and can pay a per-floor `set_sprite_data` (13 B of
  bank 0), the rest of 176..191 is reachable this way; for boot-copied art it stays off-limits.
- **Bank 0 (~77%)** grows with every new HOME dispatcher/driver. Candidates to evict if
  needed: lighting reveal logic (keep only the asm accessors HOME), perf, title_logo.
- **Bank 2 (78%)** is the gameplay kernel. Next eviction candidate: enemy AI behaviors into
  a banked `enemy_ai` module (per-turn, like combat).
- **enemy_defs HOME cache** (49 B per 7 types) grows with NUM_ENEMY_TYPES; fine to ~16 types,
  then consider keeping only active-roster defs cached.
