#pragma bank 3

#include "class_palettes.h"
#include "defs.h"
#include "globals.h"
#include <gb/cgb.h>
#include <gb/gb.h>
#include <stdint.h>

/* BKG 4–7: large 2×2 menu emblems — high contrast on cleared nametable */
static const palette_color_t s_bkg_emblem_knight[] = {
    RGB(0, 0, 0),
    RGB(6, 12, 28),   // blue
    RGB(31, 31, 31),  // white
    RGB(31, 25, 4),   // gold
};
static const palette_color_t s_bkg_emblem_scoundrel[] = {
    RGB(0, 0, 0),
    RGB(18, 10, 4),   // brown
    RGB(28, 22, 14),  // tan
    RGB(10, 22, 8),   // green
};
static const palette_color_t s_bkg_emblem_witch[] = {
    RGB(0, 0, 0),
    RGB(20, 8, 24),   // purple
    RGB(29, 24, 16),  // beige
    RGB(8, 22, 10),   // green
};
static const palette_color_t s_bkg_emblem_zerker[] = {
    RGB(0, 0, 0),
    RGB(31, 31, 31),  // white
    RGB(14, 14, 14),  // grey
    RGB(30, 4, 4),    // red
};

/* OCP PAL_PLAYER: the 2-tile hero shares ONE ramp across all classes — grey / dark-blue / gold. */
static const palette_color_t s_ocp_player_shared[] = {
    RGB(0, 0, 0),     // outline / shadow black
    RGB(6, 10, 18),   // dark blue
    RGB(18, 19, 22),  // grey
    RGB(28, 24, 6),   // gold
};

BANKREF(class_palettes)

void class_palettes_bkg_emblem_init(void) BANKED {
    if (!DEVICE_SUPPORTS_COLOR) return;
    set_bkg_palette(PAL_CLASS_EMBLEM_KNIGHT, 1u, s_bkg_emblem_knight);
    set_bkg_palette(PAL_CLASS_EMBLEM_SCOUNDREL, 1u, s_bkg_emblem_scoundrel);
    set_bkg_palette(PAL_CLASS_EMBLEM_WITCH, 1u, s_bkg_emblem_witch);
    set_bkg_palette(PAL_CLASS_EMBLEM_ZERKER, 1u, s_bkg_emblem_zerker);
}

void class_palettes_sprite_player_apply(void) BANKED {
    if (!DEVICE_SUPPORTS_COLOR) return;
    set_sprite_palette(PAL_PLAYER, 1u, s_ocp_player_shared); // shared hero ramp — class no longer changes it
}
