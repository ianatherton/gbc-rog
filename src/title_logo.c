#include "defs.h"
#include "title_logo.h"
#include "tileset.h"
#include <gb/gb.h>
#include <gbdk/platform.h>

BANKREF_EXTERN(tileset)

#define TITLE_LOGO_ROM_RESTORE 106u // VRAM0 − TILESET_VRAM_OFFSET; 12 tiles contiguous in tileset_tiles
#define TITLE_LOGO_VRAM_NTILES 12u

void title_logo_bkg_vram_patch(void) { // sheet row 13–14 cols A–F (6 wide): not in VRAM [128..255] from boot upload
    static const uint8_t logo_top_rom[] = { 192u,193u,194u,195u,196u,197u };
    static const uint8_t logo_bot_rom[] = { 208u,209u,210u,211u,212u,213u };
    uint8_t sb = (uint8_t)_current_bank;
    uint8_t i;
    SWITCH_ROM(BANK(tileset));
    for (i = 0u; i < TITLE_LOGO_BKG_W; i++)
        set_bkg_data((uint8_t)(TITLE_LOGO_BKG_VRAM0 + i), 1u, tileset_tiles + (uint16_t)logo_top_rom[i] * 16u);
    for (i = 0u; i < TITLE_LOGO_BKG_W; i++)
        set_bkg_data((uint8_t)(TITLE_LOGO_BKG_VRAM0 + TITLE_LOGO_BKG_W + i), 1u, tileset_tiles + (uint16_t)logo_bot_rom[i] * 16u);
    SWITCH_ROM(sb);
}

void title_logo_bkg_vram_restore(void) { // put back ROM 106–117 so dungeon / emblem paths see expected glyphs
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    set_bkg_data(TITLE_LOGO_BKG_VRAM0, TITLE_LOGO_VRAM_NTILES, tileset_tiles + (uint16_t)TITLE_LOGO_ROM_RESTORE * 16u);
    SWITCH_ROM(sb);
}
