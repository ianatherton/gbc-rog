#include "tileset_io.h"
#include "tileset.h"
#include <gb/gb.h>
#include <gbdk/platform.h>
#include <string.h>

BANKREF_EXTERN(tileset)

void tileset_read_tiles(uint8_t *dst, uint8_t tile_index, uint8_t count) {
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    memcpy(dst, tileset_tiles + (uint16_t)tile_index * 16u, (uint16_t)count * 16u);
    SWITCH_ROM(sb);
}

void tileset_load_bkg_tiles(uint8_t vram_first, uint8_t count, uint8_t rom_first) {
    uint8_t sb = (uint8_t)_current_bank;
    SWITCH_ROM(BANK(tileset));
    set_bkg_data(vram_first, count, tileset_tiles + (uint16_t)rom_first * 16u);
    SWITCH_ROM(sb);
}
