#pragma bank 22

#include "biome.h"
#include "enemy.h"
#include "defs.h"
#include <gb/cgb.h>

// Top-level hub floor (floor 0). No enemy roster, no items (the item scatter loop in
// map.c is skipped for BIOME_OVERWORLD). Future "areas" would add extra transition tiles
// here, routed by a position->destination lookup feeding pending_transition.

// Dark-green field: color 0 of BG slot 0 (open field / blank cells) and of PAL_FLOOR_BG
// (E3/E4 ground deco) replace the usual black. Remaining colors mirror render.c's
// pal_default / pal_floor_deco so non-hub visuals are unchanged.
static const palette_color_t pal_overworld_field[] = {
    RGB(12, 23, 5), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
};
static const palette_color_t pal_overworld_floor_deco[] = {
    RGB(12, 23, 5), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17),
};
// PAL_OW_ACCENT (slot 7, freed from UI): desert sand ramp for the SE corner region.
// idx0 = open sand base (shows on blank desert floor cells); idx1 = darker grain speckle;
// idx2 = mid sand; idx3 = bright highlight. Tree/rock tiles in the desert reuse this ramp so
// they read as sandy mounds. See overworld_is_desert() / draw_cell_terrain_only in render.c.
static const palette_color_t pal_overworld_accent[] = {
    RGB(29, 24, 13), RGB(20, 15, 7), RGB(26, 21, 11), RGB(31, 29, 20),
};

BANKREF(biome_overworld_load_palettes)
void biome_overworld_load_palettes(void) {
    set_bkg_palette(0, 1u, pal_overworld_field);
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_overworld_floor_deco);
    set_bkg_palette(PAL_OW_ACCENT, 1u, pal_overworld_accent); // slot 6 (foliage) is set by apply_wall_palette
}

BANKREF(biome_overworld_copy_defs)
void biome_overworld_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for the hub
}
