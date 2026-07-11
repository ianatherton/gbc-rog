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
| 2 | 16,189 | 99% | gameplay kernel: state_gameplay (incl. zone-confirm latch), map, render, camera, enemy. **~195 B free — chronically full; evict before adding (axe/mace extras went to bank 19 2026-07-06)** |
| 3 | 6,077 | 37% | all 9 UI states (title → game_over) + class_palettes |
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
| 17 | 10,184 | 62% | entity_sprites, scoundrel_fox |
| 18 | 5,460 | 33% | bwv527 music data (moved out of bank 5) |
| 19 | 1,958 | 12% | combat (moved out of bank 2; per-turn, far-call boundary is cheap) + `combat_player_melee_extras` (axe cleave / mace stun, evicted from bank 2) |
| 20 | — | — | equipment (`EquipStatDef` table, `items_equip_apply`, `items_equip_slot`, `equipped_kind_in_slot`) |
| 21 | 132 | 1% | biome_boss |
| 22 | 9,826 | 60% | biome_overworld (top-level hub, floor 0; no enemies/items) + render_palettes (incl. town field/brick/stone/foliage branches) + batched strip classifiers (`overworld_classify_col/row_strip` — one banked entry per camera strip, mask bytes via `wram2_read_byte`) + town render overlay/step features |
| 24 | ~600 | 4% | biome_boss2 (Sphinx roster/art; overlaid onto any dungeon's boss floor by biome_apply_floor_kind) + bosses (png2asset sphinx sheet, res/bosses.png). 10-tile sprite uploaded to VRAM scratch + re-uploaded per frame by sphinx_anim_tick for a 2-frame leg cycle + faster wingbeat |
| 28 | ~460 | 3% | dungeon_floors (miniboss elite art: runtime 2x pixel-doubler of elite_base_type's sprite → quadrant VRAM slots; floor-kind scheme in src/dungeon.h) |
| 29 | 1,836 | 11% | biome_town (town interiors, floors 46–48 = `TOWN_FLOOR_BASE`+0..2: 20×20 overworld-look safe zone — grass field, sand road cross, brick buildings housing NPCs, deco pines, heal fountain; fully lit like the hub; enter/exit via the zone-confirm gate) |
| 23, 25–27, 30–31 | 0 | 0% | empty — ~130 KB free (27 freed 2026-07: biome_miniboss + enemies_miniboss retired — miniboss/boss are floor kinds now, one biome per dungeon) |

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

Flow: `Boot → TITLE → CHAR_CREATE → GAMEPLAY ⇄ modals(STATS↔ABILITY, INVENTORY, MAP, PICKUP) → TRANSITION → (next floor | GAME_OVER → TITLE)`

| State | Primary bank | Far-calls into |
|-------|--------------|----------------|
| Boot (`main`) | 0 | 1 (tileset→VRAM), 17 (sprites), 4/18 (music data) |
| TITLE | 3 | 5 (title fx), 0 (title_logo), 4/18 (music) |
| CHAR_CREATE | 3 | 3 (class_palettes), 5 (ui) |
| GAMEPLAY enter | 2 | 14 (story crawl, first floor), 10 (level_init/map_gen), 10/11/12 (biome roster), 17 (sprites), 5 (HUD) |
| GAMEPLAY tick | 2 | 19 (combat), 0→6/7/8/9 (abilities by class), 13 (items), 15/16 (scrolls), 5 (ui/log), 17 (sprites), 0 (ally/lighting/targeting) |
| STATS / ABILITY | 3 | 5 (ui) |
| INVENTORY / PICKUP | 3 | 5 (ui), 13 (items), 20 (equipment), 17 (cursor) |
| MAP | 3 | 5 (ui), fog via lighting.c (bank 0) |
| TRANSITION | 3 | pit → 10/11/12 regen |
| GAME_OVER | 3 | 5 (ui) |

## WRAM budget

Fixed WRAM (0xC000–0xDFFF): `_DATA` ≈ 6,647 B (+ 250 B for per-floor tracking) + `_INITIALIZED` 26 B.
Stack runs down from 0xDFFF → **~1,350 B headroom**.

Top consumers:
- 3 × 1,152 B map bitsets (`floor_bits`, `pit_bits`, `enemy_occ`) = 3,456 B
- `nav_nodes` 288 B; enemy parallel arrays ~250 B + corpses; file statics ~1.5 KB (ui log,
  lcd/render scratch, map_gen temporaries)
- `floor_bits` doubles as story-crawl scratch before the first `generate_level` (story_ui.c)

### CGB banked WRAM (SVBK)

| WRAM bank | Range (mapped at 0xD000) | Contents |
|-----------|--------------------------|----------|
| 1 (default) | 0xD000–0xDFFF | tail of `_DATA` + stack — SVBK must always be restored to 1 |
| 2 | 0xD000–0xD47F | explored/fog bits (1,152 B) — access ONLY via `lighting.c` |
| 2 | 0xD480–0xD8FF | hub water mask (1,152 B) — `overworld_water_*` in `lighting.c`; hub-only, never aliases fog |
| 2 | 0xD900–0xDD7F | hub road mask (1,152 B) — `road_*` in `lighting.c`; hub-only, rendered as open sand |
| 2 | 0xDD80–0xDFFF | free (~640 B; keep data below ~0xDF00) |
| 3–7 | — | free (20 KB) — future per-floor data, bigger maps, more creature stats |

Rules for adding banked-WRAM data (the `exp2_*` accessors in `src/lighting.c` are the template):
1. Accessors are `__naked` asm in a HOME file: nothing may touch the stack while SVBK ≠ 1
   (the stack itself lives in banked memory at 0xDFxx).
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
