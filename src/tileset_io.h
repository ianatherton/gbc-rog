#ifndef TILESET_IO_H
#define TILESET_IO_H

#include <stdint.h>

// HOME-resident wrappers for any code outside bank 1 that needs to read tileset ROM
// or push tileset bytes to VRAM. They save/restore _current_bank around the
// SWITCH_ROM(BANK(tileset)) so callers in bank 2/3/etc. don't unmap themselves.
//
// rom_first / tile_index are sheet indices (each tile = 16 bytes). count is in tiles.

void tileset_read_tiles(uint8_t *dst, uint8_t tile_index, uint8_t count);
void tileset_load_bkg_tiles(uint8_t vram_first, uint8_t count, uint8_t rom_first);

#endif
