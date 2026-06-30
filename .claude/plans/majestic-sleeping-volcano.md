# Plan: Animated flag sprite in the middle of overworld towns

## Context
The overworld hub (floor 0) places seed-stable prefab features, including one **town** — a
3×3 wall ring with a grass courtyard center (`OW_FEAT_TOWN`, see `src/map_gen.c`
`OW_FEATURE_PLAN` and `overworld_cell_render` in `src/biome_overworld.c`). The courtyard
center currently draws as plain grass. We want a small **animated flag** standing in that
center cell to give towns life. It should be a hardware **OBJ sprite** (the user specified a
sprite and an *enemy* palette — enemy palettes are OBJ palettes), 2-frame animated, tinted
with a randomly chosen enemy color ramp (seed-stable per run).

Tileset art lives at sheet cells **B7** (ROM tile 97) and **C7** (ROM tile 98) — the two
animation frames.

## Key facts established during exploration
- **Town location**: scan `ow_features[0..ow_feature_count)` for `type == OW_FEAT_TOWN`. The town
  is 3×3, so the courtyard center cell is `(feature.x + 1, feature.y + 1)`. There is exactly one
  town in `OW_FEATURE_PLAN`, so a single OBJ sprite suffices.
- **Sheet→VRAM conflict**: B7/C7 (ROM 97/98) land in VRAM 225/226 via the boot upload
  (`set_bkg_data(TILESET_VRAM_OFFSET, 128, tileset_tiles)` in `src/main.c`), but the hub overwrites
  those slots with **coast** art (`COAST_VRAM_SW`/`COAST_VRAM_S`) in `biome_load_active`. So the flag
  needs its **own** VRAM slots. VRAM **253 and 254** are permanently free (0 refs across the codebase)
  — copy the two frames there once at boot.
- **Free OAM slot**: slot **38** (`SP_INV_CURSOR` / `SP_GORGON_HEAD_R`) is untouched on the hub
  (inventory cursor only renders in the inventory state; gorgon head only on the boss floor). It sits
  above the enemy/head hide sweep (`refresh_oam_only` sweeps 3..26, heads 27..30, allies 31..34). This
  is the same context-sharing pattern already used for slot 38. `entity_sprites_init()` hides all 40
  OAM slots on every floor load, so no stale flag lingers when leaving the hub.
- **Animation timer**: reuse the existing `enemy_anim_toggle` global (ticked by `enemy_anim_update()`
  in `src/state_gameplay.c:516`, which runs every gameplay frame on the hub too). When it flips, the
  loop calls `draw_enemy_cells` → `entity_sprites_refresh_oam_only`, so adding the flag refresh there
  gives free 2-frame animation with zero new timers.
- **Enemy palettes** (OBJ slots): `PAL_LADDER`(skeleton, 4), `PAL_ENEMY_RAT`(5),
  `PAL_ENEMY_GOBLIN`(6), `PAL_XP_UI`(bat, 7) — defined/loaded in `render.c load_palettes`.

## Decisions (from clarification)
- Palette is **seed-stable per run** (derived from `run_seed`).
- Pick from the **4 dedicated enemy ramps** (skeleton/rat/goblin/bat), not the green snake.

## Implementation

### 1. `src/defs.h` — tile constants
Add near the other borrowed-slot defines (e.g. by the `TILE_SHEET_*` block ~line 530-544):
```c
#define TILE_SHEET_FLAG_1  97u   /* B7 — flag anim frame 1 (shares ROM cell with unused gorgon-head borrow) */
#define TILE_SHEET_FLAG_2  98u   /* C7 — flag anim frame 2 */
#define TILE_FLAG_F1_VRAM 253u   /* permanently-free OBJ slot — boot-copied from B7 */
#define TILE_FLAG_F2_VRAM 254u   /* permanently-free OBJ slot — boot-copied from C7 */
```

### 2. `src/main.c` — boot upload
In the boot block that pages in the tileset bank and copies past-first-128 sprites (~line 58-87),
add two copies (these slots need no per-floor restore since nothing else uses them):
```c
set_sprite_data(TILE_FLAG_F1_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_FLAG_1 * 16u);
set_sprite_data(TILE_FLAG_F2_VRAM, 1u, tileset_tiles + (uint16_t)TILE_SHEET_FLAG_2 * 16u);
```

### 3. `src/entity_sprites.c` — draw the flag OBJ
Add a static helper, modeled on `refresh_allies_oam` / `refresh_belt_selector_oam`:
```c
#define SP_TOWN_FLAG SP_INV_CURSOR  // slot 38 — idle on the hub (inv cursor / gorgon head share it elsewhere)

static uint8_t town_flag_pal(void) { // seed-stable pick from the 4 enemy OBJ ramps
    static const uint8_t pals[4] = { PAL_LADDER, PAL_ENEMY_RAT, PAL_ENEMY_GOBLIN, PAL_XP_UI };
    return pals[(uint8_t)(run_seed & 3u)];
}

static void refresh_town_flag_oam(void) {
    uint8_t i;
    if (floor_biome != BIOME_OVERWORLD) return; // only the hub owns slot 38; leave it alone elsewhere
    for (i = 0u; i < ow_feature_count; i++) {
        if (ow_features[i].type != OW_FEAT_TOWN) continue;
        {
            uint8_t mx = (uint8_t)(ow_features[i].x + 1u); // 3x3 courtyard center
            uint8_t my = (uint8_t)(ow_features[i].y + 1u);
            if (mx < g_cam_tx || mx >= g_cam_tx_end || my < g_cam_ty || my >= g_cam_ty_end
                    || !lighting_is_revealed(mx, my)) { oam_hide(SP_TOWN_FLAG); return; }
            move_entity_oam(SP_TOWN_FLAG, (int16_t)mx * 8, (int16_t)my * 8,
                enemy_anim_toggle ? TILE_FLAG_F2_VRAM : TILE_FLAG_F1_VRAM, town_flag_pal());
            return;
        }
    }
    oam_hide(SP_TOWN_FLAG); // no town placed
}
```
Call `refresh_town_flag_oam();` inside `entity_sprites_refresh_oam_only()` (after the
belt/buff refresh, before/after the hide sweep — slot 38 is outside the swept range so order
is not critical). `g_cam_tx..g_cam_ty_end` are already set at the top of that function.

`run_seed`, `ow_features`, `ow_feature_count`, `OW_FEAT_TOWN`, `enemy_anim_toggle`,
`lighting_is_revealed`, and the `PAL_*` constants are all already reachable from this bank
(globals.h / defs.h / enemy.h, all included).

### 4. Tileset art (prerequisite)
The two flag frames must exist in `res/tileset.png` at cells **B7** and **C7** (these ROM cells
are currently blank). After drawing them, regenerate `src/tileset.c` with the project's normal
tileset→C step so `tileset_tiles[]` carries the new pixels.

## Notes / limits
- One town ⇒ one OAM slot (38). If `OW_FEATURE_PLAN` ever gains a second town that can be on-screen
  simultaneously, additional free OAM slots would be needed (only one is reserved here).
- No `biome_load_active` change is needed — the flag VRAM is boot-loaded into permanently-free slots,
  and the OAM slot is only written on the hub.

## Verification
1. Build the ROM (normal build). Confirm it compiles (HOME/bank-2 budgets unaffected — only bank 17
   gains a small helper; bank 0 boot adds two `set_sprite_data` calls).
2. Run in an emulator (or `sameboy_tester` headless per docs/BANKS.md). On the title screen start a run,
   land on the hub (floor 0), and walk to the town (3×3 walled structure). Confirm:
   - a flag sits in the courtyard center cell,
   - it animates between the two frames at the enemy-glyph cadence,
   - it is tinted with one of the four enemy ramps and stays the same color across hub re-entries in the
     same run (seed-stable),
   - it appears/disappears correctly as it scrolls on/off screen and under fog,
   - descending into a dungeon and returning shows no stale flag elsewhere (init clears OAM).
3. Sanity-check a couple of different `run_seed`s to see palette variety.
