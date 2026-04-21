#ifndef TITLE_LOGO_H
#define TITLE_LOGO_H

#include <stdint.h>

#define TITLE_LOGO_BKG_W    6u   // "Mara" wordmark: 6×2 map tiles
#define TITLE_LOGO_BKG_VRAM0 234u // scratch BKG tiles (12 slots; sheet idx ≥128 not in boot VRAM upload)

void title_logo_bkg_vram_patch(void);   // HOME: read tileset ROM safely (same idea as main.c boot tiles)
void title_logo_bkg_vram_restore(void); // HOME: restore VRAM slots before gameplay / char create

#endif
