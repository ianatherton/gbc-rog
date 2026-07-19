#pragma bank 22

// CGB palette setup, relocated out of the near-full render bank 2. These functions + their const
// ramps are co-located here so each reads its own consts while bank 22 is mapped; wall_palette_table
// is HOME (always mapped) and class_palettes/ui helpers are BANKED, so the cross-bank boundary is
// clean. Callers (draw_screen, state transitions, menus) reach these via the autobank trampoline.
#include "render.h"
#include "map.h"           // floor_biome via globals; wall_palette_index/pillar_palette_index
#include "globals.h"
#include "wall_palettes.h" // wall_palette_table (HOME), NUM_WALL_PALETTES
#include "biome.h"         // BIOME_OVERWORLD
#include "class_palettes.h"
#include "lcd.h"           // lcd_note_bkg0 — panic flash restores the live slot-0 ramp
#include <gbdk/platform.h>

static const palette_color_t pal_default[]  = { RGB(0,0,0),  RGB(8,8,8),   RGB(16,16,16), RGB(31,31,31) }; // slot 0: black field, corpses, blank floor; wall paper
static const palette_color_t pal_floor_deco[] = { RGB(0,0,0), RGB(5,5,5), RGB(11,11,11), RGB(17,17,17) }; // BKG PAL_FLOOR_BG: E3–E5 ground deco, dark grey on black
static const palette_color_t pal_green[]    = { RGB(0,0,0),  RGB(0,20,0),  RGB(0,26,0),   RGB(0,31,0)   }; // BKG+OCP1: serpent & adder only (snakes)
static const palette_color_t pal_ladder[]   = { RGB(0,0,0),  RGB(6,8,12),  RGB(31,16,2),  RGB(31,26,8) }; // BKG4 pit/ladder base with blue-grey shadow under warm highlights
static const palette_color_t pal_enemy_skeleton[] = { RGB(0,0,0), RGB(8,6,20),  RGB(16,10,26), RGB(22,16,31) }; // OCP4 violet / blue-purple bone
static const palette_color_t pal_enemy_rat[]      = { RGB(0,0,0), RGB(22,6,10), RGB(30,10,16), RGB(31,18,22) }; // OCP5 red–rose (BKG5 = life bar)
static const palette_color_t pal_enemy_goblin[]   = { RGB(0,0,0), RGB(18,4,18), RGB(26,8,24),  RGB(31,14,28) }; // OCP6 magenta–pink (BKG6 = HUD text)
static const palette_color_t pal_life_ui[]  = { RGB(0,0,0),  RGB(18,0,0),  RGB(25,2,2),   RGB(31,31,31) }; // slot 5: hearts/bar + all white HUD/UI text — bright = white
static const palette_color_t pal_xp_ui[]    = { RGB(0,0,0),  RGB(23,9,0), RGB(30,17,0), RGB(31,27,1) }; // OCP7 sprite gold ramp — low B keeps hue; steps stay dark/mid/bright

static uint8_t wall_palette_hw_iw = 255u, wall_palette_hw_ip = 255u; // 255 = out of band; invalidated in load_palettes
static uint8_t wall_palette_hw_biome = 255u; // tracks floor_biome so the field color follows hub<->dungeon transitions

BANKREF(apply_wall_palette)
void apply_wall_palette(void) BANKED { // PAL_WALL_BG bulk walls + PAL_PILLAR_BG column tiles (CGB BGP slots)
    uint8_t iw = wall_palette_index, ip = pillar_palette_index;
    palette_color_t wall_pal[4], pil_pal[4];
    // index-0 ("paper" behind wall/pillar art) matches the open field: dark green on the hub, else black
    palette_color_t bg0 = (floor_biome == BIOME_OVERWORLD) ? (palette_color_t)RGB(1, 6, 2) : pal_default[0];
    if (iw >= NUM_WALL_PALETTES) iw = 0;
    if (ip >= NUM_WALL_PALETTES) ip = 0;
    if (iw == wall_palette_hw_iw && ip == wall_palette_hw_ip && floor_biome == wall_palette_hw_biome) return; // skip CRAM when draw_screen repeats same ramp
    wall_palette_hw_iw = iw;
    wall_palette_hw_ip = ip;
    wall_palette_hw_biome = floor_biome;
    if (floor_biome == BIOME_OVERWORLD) {
        // PAL_OW_FOLIAGE (slot 6, freed from UI) = green pine ramp for interior c10 trees
        // (idx0 bg / idx1 foliage / idx2 trunk); idx0 matches the open-field color so trees
        // sit seamlessly on the field. Moving trees here frees PAL_WALL_BG (3) for future hub deco.
        palette_color_t tree_pal[4] = {
            RGB(12, 23, 5), RGB(6, 18, 4), RGB(10, 7, 2), RGB(12, 26, 6), // idx0 == pal_overworld_field[0]
        };
        // PAL_PILLAR_BG = water ramp for open sea (F10: idx3 bulk / idx2 wave specks) and the coast
        // tiles. idx0 is the green field color: open water never uses idx0, but the coast tiles use it
        // for their land bulk, so the shore reads as green land (idx0) with a blue water edge (idx2/3).
        palette_color_t water_pal[4] = {
            RGB(12, 23, 5), RGB(4, 8, 16), RGB(9, 16, 26), RGB(2, 7, 16),
        };
        // PAL_WALL_BG (freed in the hub) = icy snow ramp for the NW snowfield region: idx0 snow base
        // (open snow), idx1 blue shadow grain, idx2 mid, idx3 white. Frosted trees reuse it too.
        palette_color_t snow_pal[4] = {
            RGB(24, 27, 31), RGB(15, 19, 27), RGB(20, 24, 30), RGB(31, 31, 31),
        };
        set_bkg_palette(PAL_OW_FOLIAGE, 1u, tree_pal);
        set_bkg_palette(PAL_PILLAR_BG,  1u, water_pal);
        set_bkg_palette(PAL_WALL_BG,    1u, snow_pal);
        return;
    }
    if (floor_biome == BIOME_TOWN) {
        // Town interior: same grass field as the hub. Slot 6 = the hub's pine ramp (deco trees);
        // slot 3 = this floor's wall-table ramp on a green field (brick buildings sit on grass);
        // slot 1 = fixed stone ramp on green (fountain well, NPC "statues", any pillar art).
        palette_color_t tree_pal[4]  = { RGB(12, 23, 5), RGB(6, 18, 4), RGB(10, 7, 2), RGB(12, 26, 6) };
        palette_color_t stone_pal[4] = { RGB(12, 23, 5), RGB(9, 9, 11), RGB(17, 17, 19), RGB(27, 27, 29) };
        wall_pal[0] = RGB(12, 23, 5); // grass shows through the brick art's index-0 "paper"
        wall_pal[1] = wall_palette_table[iw][1];
        wall_pal[2] = wall_palette_table[iw][2];
        wall_pal[3] = wall_palette_table[iw][3];
        set_bkg_palette(PAL_OW_FOLIAGE, 1u, tree_pal);
        set_bkg_palette(PAL_WALL_BG,    1u, wall_pal);
        set_bkg_palette(PAL_PILLAR_BG,  1u, stone_pal);
        return;
    }
    wall_pal[0] = bg0; // field color — seamless with blank / pit-adjacent open cells
    wall_pal[1] = wall_palette_table[iw][1];
    wall_pal[2] = wall_palette_table[iw][2];
    wall_pal[3] = wall_palette_table[iw][3];
    pil_pal[0] = bg0;
    pil_pal[1] = wall_palette_table[ip][1];
    pil_pal[2] = wall_palette_table[ip][2];
    pil_pal[3] = wall_palette_table[ip][3];
    set_bkg_palette(PAL_WALL_BG, 1, wall_pal);
    set_bkg_palette(PAL_PILLAR_BG, 1, pil_pal);
    // PAL_WALL_BG sprite slot is reserved for gameplay fire particle tint; keep wall colors BG-only.
}

BANKREF(apply_field_palette)
void apply_field_palette(void) BANKED { // slot 0 (blank field) + floor-deco, per biome — restores after a menu blanks slot 0
    if (floor_biome == BIOME_OVERWORLD || floor_biome == BIOME_TOWN) { // towns share the hub's grass field
        // keep identical to biome_overworld.c pal_overworld_field / pal_overworld_floor_deco
        // (field idx1-3 = biome-border blend: sand stroke, dark snow line, white snow edge — see
        // ow_border; idx3 stays pure white for the loading screen's attr-0 pen-3 text)
        palette_color_t f[4]  = { RGB(12, 23, 5), RGB(29, 24, 13), RGB(15, 19, 27), RGB(31, 31, 31) };
        palette_color_t fd[4] = { RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17) };
        set_bkg_palette(0, 1, f);
        set_bkg_palette(PAL_FLOOR_BG, 1, fd);
        lcd_note_bkg0(f);
    } else {
        set_bkg_palette(0, 1, pal_default);
        set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco);
        lcd_note_bkg0(pal_default);
    }
    // A menu may have stomped PAL_WALL_BG/PAL_PILLAR_BG (e.g. inventory restores the metal ramp into
    // slot 3). Invalidate the cache so the next apply_wall_palette re-pushes the floor's true ramps —
    // dungeon walls/pillars, or hub water/snow/foliage.
    wall_palette_hw_iw = wall_palette_hw_ip = wall_palette_hw_biome = 255u;
}

BANKREF(load_palettes)
void load_palettes(void) BANKED { // slots 0–7 except walls: wall table entry 0 until apply_wall_palette runs
    set_bkg_palette(0, 1, pal_default);
    lcd_note_bkg0(pal_default); // hub/town loaders overwrite slot 0 (and the note) right after
    set_bkg_palette(PAL_PILLAR_BG, 1, wall_palette_table[0]); // slot 1 = pillars in gameplay (was unused BKG green)
    set_bkg_palette(PAL_FLOOR_BG, 1, pal_floor_deco); // ground deco tile only; blank floor uses slot 0
    set_bkg_palette(PAL_WALL_BG, 1, wall_palette_table[0]); // matches wall_palette_index default 0
    set_bkg_palette(PAL_LADDER, 1, pal_ladder); // static shared ladder+brazier fire tone — also BKG gold (PAL_XP_UI_BG)
    set_bkg_palette(PAL_LIFE_UI, 1, pal_life_ui); // hearts + all white HUD/UI text (index 3 = white)
    set_bkg_palette(PAL_ITEM_GOLD_BG, 1, pal_xp_ui); // slot 6: true orange-gold for dungeon ground items
    // (the hub overwrites slot 6 with foliage via apply_wall_palette; slot 7 stays free for biome use)
    set_sprite_palette(0, 1, pal_default);
    set_sprite_palette(1, 1, pal_green);
    class_palettes_sprite_player_apply();
    set_sprite_palette(PAL_WALL_BG, 1, pal_ladder); // gameplay fire particle ramp uses shared ladder fire tone
    set_sprite_palette(PAL_LADDER, 1, pal_enemy_skeleton);
    set_sprite_palette(PAL_ENEMY_RAT, 1, pal_enemy_rat);
    set_sprite_palette(PAL_ENEMY_GOBLIN, 1, pal_enemy_goblin);
    set_sprite_palette(PAL_XP_UI, 1, pal_xp_ui); // OBJ7 — belt M5 arrow + bats share XP gold ramp (PAL_ENEMY_BAT / PAL_XP_UI both 7)
    wall_palette_hw_iw = wall_palette_hw_ip = 255u; // load stomps wall BGP to table[0] — next apply must push true indices
}
