#ifndef TITLE_LOGO_H
#define TITLE_LOGO_H

#include <stdint.h>

#define TITLE_LOGO_MAP_W     12u // 2× scale of 6-wide source (class-select style nearest-neighbor)
#define TITLE_LOGO_MAP_H     4u  // 2× scale of 2-tile-tall source
#define TITLE_LOGO_VRAM_NTILES 48u

/* Scattered VRAM indices: no contiguous 48-tile gap below 233 avoids 162/178 (brazier C3/C4), 180 (title fire), 187/233 (border), 248+ (aura/emblem), 138 (belt). */
extern const uint8_t title_logo_bkg_vram_slot[48];

void title_logo_bkg_vram_patch(void);   // HOME: scale 6×2 sheet tiles → 48 VRAM slots (tileset bank)
void title_logo_bkg_vram_restore(void); // HOME: restore each slot’s ROM tile before gameplay

#endif
