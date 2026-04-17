#ifndef CLASS_PALETTES_H
#define CLASS_PALETTES_H

#include <gbdk/platform.h>

BANKREF_EXTERN(class_palettes)

void class_palettes_bkg_emblem_init(void) BANKED;     // CGB: BKG 4–7 char-select emblem ramps (optional menu)
void class_palettes_sprite_player_apply(void) BANKED; // CGB: OCP PAL_PLAYER from player_class

#endif
