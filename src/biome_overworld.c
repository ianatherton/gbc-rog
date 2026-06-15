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
    RGB(13, 26, 6), RGB(8, 8, 8), RGB(16, 16, 16), RGB(31, 31, 31),
};
static const palette_color_t pal_overworld_floor_deco[] = {
    RGB(13, 26, 6), RGB(5, 5, 5), RGB(11, 11, 11), RGB(17, 17, 17),
};

BANKREF(biome_overworld_load_palettes)
void biome_overworld_load_palettes(void) {
    set_bkg_palette(0, 1u, pal_overworld_field);
    set_bkg_palette(PAL_FLOOR_BG, 1u, pal_overworld_floor_deco);
}

BANKREF(biome_overworld_copy_defs)
void biome_overworld_copy_defs(EnemyDef *out, uint8_t *out_active, uint8_t *out_count) {
    (void)out;
    (void)out_active;
    *out_count = 0u; // empty roster — spawn_enemies() early-returns for the hub
}
