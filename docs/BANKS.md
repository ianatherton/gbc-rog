# ROM bank map, WRAM budget, and scaling policy

Measured from `build/gbc/gbc-rog.map` / `.noi` (2026-06-10, after the bank reorganization).
Regenerate the numbers after any layout change: `make`, then check the `_CODE_n` / `_DATA`
area sizes at the bottom of the map file.

Cart: **MBC5 + RAM + battery (0x1B), 512 KB, 32 banks of 16,384 B, CGB-only (`-Wm-yC`)**.
CGB-only is required: explored/fog bits live in CGB banked WRAM (SVBK), which DMG lacks.
SRAM (battery RAM) is currently unused — free for saves later.

## ROM bank usage

| Bank | Used | % | Contents |
|------|------|----|----------|
| 0 (fixed) | ~15,900 of 16,368 usable | ~97% | main loop, ability_dispatch, ally, biome dispatch + `enemy_defs` HOME cache, enemy_extras, lcd, lighting (incl. SVBK fog/water/road accessors + `wram2_read_byte` batch reader), music driver + SFX, perf, seed_entropy, targeting, tileset_io, title_logo, ui_loading_isr, wall_palettes, SDCC runtime. **~450 B free (2026-07-06) — keep HOME lean** |
| 1 | 4,360 | 27% | tileset (png2asset output) |
| 2 | 16,347 | 99.8% | gameplay kernel: state_gameplay (incl. zone-confirm latch + town exit/roof/villager/barrel hooks), map, render (incl. the grass-biome ground-item palette fix), camera, enemy. **~37 B free (2026-07-21) — SEVERELY CRITICAL: do not add to this bank without evicting something first (tried hoisting a shared condition into a local var for the item-palette fix — SDCC generated basically the same size either way, ~36-37 B, so "write it cleverly" is not a lever here). Past eviction examples: axe/mace extras → bank 19 (2026-07-06), belt-description helpers → bank 30 `gameplay_cold.c` (2026-07-18)** |
| 3 | 10,964 | 67% | all 10 UI states (title → game_over, incl. talk/trade) + class_palettes |
| 4 | 2,971 | 18% | bwv1043 music data |
| 5 | 13,000 | 79% | ui.c (incl. `ui_confirm_prompt_push` zone-confirm text) |
| 6 | 122 | 1% | abilities_knight |
| 7 | 89 | 1% | abilities_scoundrel |
| 8 | 617 | 4% | abilities_witch |
| 9 | 526 | 3% | abilities_zerker |
| 10 | 2,879 | 18% | level_init, map_gen, biome_dungeon |
| 11 | 132 | 1% | biome_crypt |
| 12 | 132 | 1% | biome_cavern |
| 13 | 2,242 | 14% | items (kinds + affixes) |
| 14 | 5,838 | 36% | story_ui + names (deterministic town/dungeon/NPC name generator, `src/names.c`) |
| 15 | 157 | 1% | scroll_blast |
| 16 | 357 | 2% | scroll_root, debuff_icon, bow_shoot |
| 17 | 13,334 | 81% | entity_sprites (incl. `refresh_town_npcs_oam` — wandering villager OAM, borrows the town's always-empty enemy run), scoundrel_fox |
| 18 | 5,460 | 33% | bwv527 music data (moved out of bank 5) |
| 19 | 1,958 | 12% | combat (moved out of bank 2; per-turn, far-call boundary is cheap) + `combat_player_melee_extras` (axe cleave / mace stun, evicted from bank 2) |
| 20 | — | — | equipment (`EquipStatDef` table, `items_equip_apply`, `items_equip_slot`, `equipped_kind_in_slot`) |
| 21 | 132 | 1% | biome_boss |
| 22 | 11,102 | 68% | biome_overworld (top-level hub, floor 0; no enemies/items) + render_palettes (incl. town field/brick/stone/foliage branches) + batched strip classifiers (`overworld_classify_col/row_strip` — one banked entry per camera strip, mask bytes via `wram2_read_byte`) + town render overlay/step features (villagers are OAM now, not drawn here; doorway G1/G2 + barrel props drawn here) |
| 24 | ~600 | 4% | biome_boss2 (Sphinx roster/art; overlaid onto any dungeon's boss floor by biome_apply_floor_kind) + bosses (png2asset sphinx sheet, res/bosses.png). 10-tile sprite uploaded to VRAM scratch + re-uploaded per frame by sphinx_anim_tick for a 2-frame leg cycle + faster wingbeat |
| 28 | ~460 | 3% | dungeon_floors (miniboss elite art: runtime 2x pixel-doubler of elite_base_type's sprite → quadrant VRAM slots; floor-kind scheme in src/dungeon.h) |
| 29 | 5,017 | 31% | biome_town (town interiors, floors 46–48 = `TOWN_FLOOR_BASE`+0..2: 59..96-square safe zone sized by building count — pine border ring + brick town wall, 2-tile-wide sand road cross exiting N/S/E/W (`town_exit_at` = border cell + road bit, no stored table), 5–20 buildings — first `MAX_TOWN_NPCS` (8) open (G1 doorway, roofed walkable interior, villager, `SIGN_KIND_BUILDING` door sign), the rest closed/decorative (G2 door, never carved, no villager, still signed) — deco pines + destructible barrels (F2, ~half the buildings plus rare stray ones; `town_barrel_try_break` — 1 hit, same loot roll as an enemy kill via `enemy_try_drop_item`, same poof art via bank-17 `entity_sprites_run_barrel_poof`), heal fountain; roofs = fog buffer reused (`townroof_*`), lifted per-building by `town_roof_update`; villagers wander + slide via `town_npcs_tick`/`town_npc_blocks` (real OAM sprites, drawn+glided in bank 17) — lazy random walk, solid collision on their tile (bumping one starts a conversation via `overworld_signpost_read`), warp home past `TOWN_NPC_ROAM_RADIUS`; fully lit like the hub) |
| 30 | 3,124 | 19% | auto_explore (A-button auto-explore: cached-path BFS, DCSS-style stop-on-sight/stop-on-hit, auto-pickup, walk-to-ladder when explored; private SVBK bank-3 accessors) + gameplay_cold (SELECT-edge belt-description helpers evicted from bank 2) |
| 23, 25–27, 31 | 0 | 0% | empty — ~115 KB free (27 freed 2026-07: biome_miniboss + enemies_miniboss retired — miniboss/boss are floor kinds now, one biome per dungeon) |

Total ROM used ≈ 76 KB of 512 KB (~15%). ROM is not the constraint. If it ever is,
MBC5 goes to 8 MB: bump `-Wl-yo32` in the Makefile (64/128/…), nothing else changes.

## Bank allocation policy (where new content goes)

- **6–9** class abilities/spells (one bank per class, each ~99% free)
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
| GAMEPLAY tick | 2 | 19 (combat), 0→6/7/8/9 (abilities by class), 13 (items), 15/16 (scrolls), 5 (ui/log), 17 (sprites), 0 (ally/lighting/targeting) |
| STATS / ABILITY | 3 | 5 (ui) |
| INVENTORY / PICKUP | 3 | 5 (ui), 13 (items), 20 (equipment), 17 (cursor) |
| TALK (trade) | 3 | 5 (ui/log), 13 (items + drop table), 17 (cursor); entered from 29 (`town_npc_blocks` sets `next_state` — bank 2 is full) |
| MAP | 3 | 5 (ui), fog via lighting.c (bank 0) |
| TRANSITION | 3 | pit → 10/11/12 regen |
| GAME_OVER | 3 | 5 (ui) |

## WRAM budget

Fixed WRAM (0xC000–0xDFFF): `_DATA` ≈ 7,490 B (measured 2026-07-19; ends 0xDDE2).
Stack runs down from 0xDFFF → **~542 B headroom — this is load-bearing, treat it as a floor**:
`class_emblem_draw` (state_char_create.c) alone puts ~336 B of buffers on the stack, plus
banked-call depth and the audio ISR. Adding ~90 B of town tables to globals.c once pushed
`_DATA` to 0xDE3C and the char-create screen overran the stack into `_DATA` → garbled tiles
from the class symbols onward. The town tables now OVERLAY `nav_nodes[]` (288 B) via
`town_state` (map.h) — towns never build the nav graph, so the storage is free there.
If headroom is ever needed: auto_explore BFS queue + path buffer (256 + 48 B) can move to
SVBK bank 3 at 0xDD80+.

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
- **Bank 0 (~77%)** grows with every new HOME dispatcher/driver. Candidates to evict if
  needed: lighting reveal logic (keep only the asm accessors HOME), perf, title_logo.
- **Bank 2 (78%)** is the gameplay kernel. Next eviction candidate: enemy AI behaviors into
  a banked `enemy_ai` module (per-turn, like combat).
- **enemy_defs HOME cache** (49 B per 7 types) grows with NUM_ENEMY_TYPES; fine to ~16 types,
  then consider keeping only active-roster defs cached.
