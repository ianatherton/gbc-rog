#pragma bank 2

#include "class_palettes.h"
#include "defs.h"
#include "globals.h"
#include <gb/cgb.h>
#include <gb/gb.h>
#include <stdint.h>

/* BKG 4–7: large 2×2 menu emblems — high contrast on cleared nametable */
static const palette_color_t s_bkg_emblem_knight[] = {
    RGB(0, 0, 0),
    RGB(6, 10, 18),   // steel shadow
    RGB(18, 20, 24),  // plate mid
    RGB(31, 28, 8),   // gold crest
};
static const palette_color_t s_bkg_emblem_scoundrel[] = {
    RGB(0, 0, 0),
    RGB(2, 14, 10),   // deep teal
    RGB(8, 22, 18),   // sea glass
    RGB(26, 20, 6),   // leather / coin
};
static const palette_color_t s_bkg_emblem_witch[] = {
    RGB(0, 0, 0),
    RGB(12, 4, 18),   // void violet
    RGB(22, 6, 24),   // robe
    RGB(28, 22, 10),  // bone / wax
};
static const palette_color_t s_bkg_emblem_zerker[] = {
    RGB(0, 0, 0),
    RGB(14, 6, 6),    // dried blood
    RGB(28, 6, 4),    // fresh red
    RGB(31, 28, 26),  // fur / tooth
};

/* OCP PAL_PLAYER: one hardware slot; pick one of four ramps from player_class */
static const palette_color_t s_ocp_player_knight[] = {
    RGB(0, 0, 0),
    RGB(6, 10, 18),   // steel shadow
    RGB(16, 18, 22),  // plate
    RGB(28, 24, 6),   // gold trim
};
static const palette_color_t s_ocp_player_scoundrel[] = {
    RGB(0, 0, 0),
    RGB(2, 14, 10),   // deep teal
    RGB(10, 20, 14),  // leather green
    RGB(24, 18, 8),   // leather / brass
};
static const palette_color_t s_ocp_player_witch[] = {
    RGB(0, 0, 0),
    RGB(12, 4, 18),   // void violet
    RGB(22, 8, 24),   // robe
    RGB(12, 22, 8),   // toxic accent
};
static const palette_color_t s_ocp_player_zerker[] = {
    RGB(0, 0, 0),
    RGB(31, 28, 26),  // near-white fur/cloth
    RGB(14, 14, 14),  // cool grey
    RGB(28, 4, 4),    // blood red
};

static const palette_color_t *sprite_player_pal_ptr(uint8_t c) {
    if (c == 1u) return s_ocp_player_scoundrel;
    if (c == 2u) return s_ocp_player_witch;
    if (c == 3u) return s_ocp_player_zerker;
    return s_ocp_player_knight;
}

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
    set_sprite_palette(PAL_PLAYER, 1u, sprite_player_pal_ptr(player_class));
}
