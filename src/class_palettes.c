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

/* OCP PAL_PLAYER: one hardware slot; pick one of four ramps from player_class.
   Gameplay-only contrast push (menu emblems above keep the original values): each ramp's
   dark tone is 50% darker and its bright tone 25% brighter (channel-wise, clamped to 31). */
static const palette_color_t s_ocp_player_knight[] = {
    RGB(0, 0, 0),
    RGB(3, 5, 9),     // steel shadow (was 6,10,18)
    RGB(18, 20, 24),  // plate mid
    RGB(31, 31, 10),  // gold crest (was 31,28,8; R clamped)
};
static const palette_color_t s_ocp_player_scoundrel[] = {
    RGB(0, 0, 0),
    RGB(9, 5, 2),     // brown (was 18,10,4)
    RGB(31, 28, 18),  // tan (was 28,22,14; R clamped)
    RGB(10, 22, 8),   // green
};
static const palette_color_t s_ocp_player_witch[] = {
    RGB(0, 0, 0),
    RGB(10, 4, 12),   // purple (was 20,8,24)
    RGB(31, 30, 20),  // beige (was 29,24,16; R clamped)
    RGB(8, 22, 10),   // green
};
static const palette_color_t s_ocp_player_zerker[] = {
    RGB(0, 0, 0),
    RGB(31, 31, 31),  // white — already max, can't go 25% brighter
    RGB(7, 7, 7),     // grey (was 14,14,14)
    RGB(30, 4, 4),    // red
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
