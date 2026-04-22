#include "defs.h"
#include "title_logo.h"
#include "tileset.h"
#include <gb/gb.h>
#include <gbdk/platform.h>
#include <string.h>

BANKREF_EXTERN(tileset)

#define TITLE_LOGO_SRC_W 6u
#define TITLE_LOGO_SRC_H 2u
#define TITLE_LOGO_SRC_PIX_W ((uint16_t)TITLE_LOGO_SRC_W * 8u)
#define TITLE_LOGO_SRC_PIX_H ((uint16_t)TITLE_LOGO_SRC_H * 8u)

const uint8_t title_logo_bkg_vram_slot[48] = {
    128u,129u,130u,131u,132u,133u,134u,135u,136u,137u,
    139u,140u,141u,142u,143u,144u,145u,146u,147u,148u,149u,150u,151u,152u,153u,154u,155u,156u,157u,158u,159u,160u,161u,
    163u,164u,165u,166u,167u,168u,169u,170u,171u,172u,173u,174u,175u,
    179u,181u,
};

static uint8_t tile2bpp_get_px(const uint8_t *tile, uint8_t x, uint8_t y) {
    uint8_t m = (uint8_t)(0x80u >> x);
    uint8_t lo = (tile[(uint8_t)(y * 2u)] & m) ? 1u : 0u;
    uint8_t hi = (tile[(uint8_t)(y * 2u + 1u)] & m) ? 2u : 0u;
    return (uint8_t)(lo | hi);
}

static void tile2bpp_set_px(uint8_t *tile, uint8_t x, uint8_t y, uint8_t c) {
    uint8_t m = (uint8_t)(0x80u >> x);
    uint8_t lo_i = (uint8_t)(y * 2u);
    uint8_t hi_i = (uint8_t)(lo_i + 1u);
    if (c & 1u) tile[lo_i] |= m; else tile[lo_i] &= (uint8_t)~m;
    if (c & 2u) tile[hi_i] |= m; else tile[hi_i] &= (uint8_t)~m;
}

void title_logo_bkg_vram_patch(void) { // same 2× nearest-neighbor idea as state_char_create class_emblem_draw
    static const uint8_t logo_top_rom[] = { 192u,193u,194u,195u,196u,197u };
    static const uint8_t logo_bot_rom[] = { 208u,209u,210u,211u,212u,213u };
    static uint8_t pack[192]; // BSS — stack cannot hold pack+out (~1k)
    static uint8_t out[768];
    uint8_t sb = (uint8_t)_current_bank;
    uint8_t i;
    uint16_t ox, oy;
    SWITCH_ROM(BANK(tileset));
    for (i = 0u; i < TITLE_LOGO_SRC_W; i++)
        memcpy(pack + (uint16_t)i * 16u, tileset_tiles + (uint16_t)logo_top_rom[i] * 16u, 16u);
    for (i = 0u; i < TITLE_LOGO_SRC_W; i++)
        memcpy(pack + (uint16_t)((TITLE_LOGO_SRC_W + i) * 16u), tileset_tiles + (uint16_t)logo_bot_rom[i] * 16u, 16u);
    SWITCH_ROM(sb);
    memset(out, 0, sizeof(out));
    for (oy = 0u; oy < (uint16_t)TITLE_LOGO_SRC_PIX_H * 2u; oy++) {
        for (ox = 0u; ox < (uint16_t)TITLE_LOGO_SRC_PIX_W * 2u; ox++) {
            uint8_t sx = (uint8_t)(ox >> 1u);
            uint8_t sy = (uint8_t)(oy >> 1u);
            uint8_t tile_col = (uint8_t)(sx >> 3u);
            uint8_t tile_row = (uint8_t)(sy >> 3u);
            uint8_t src_id = (uint8_t)(tile_row * TITLE_LOGO_SRC_W + tile_col);
            uint8_t tx = (uint8_t)(sx & 7u);
            uint8_t ty = (uint8_t)(sy & 7u);
            uint8_t c = tile2bpp_get_px(pack + (uint16_t)src_id * 16u, tx, ty);
            uint8_t txo = (uint8_t)(ox >> 3u);
            uint8_t tyo = (uint8_t)(oy >> 3u);
            uint8_t tid = (uint8_t)(tyo * TITLE_LOGO_MAP_W + txo);
            uint8_t dx = (uint8_t)(ox & 7u);
            uint8_t dy = (uint8_t)(oy & 7u);
            tile2bpp_set_px(out + (uint16_t)tid * 16u, dx, dy, c);
        }
    }
    for (i = 0u; i < TITLE_LOGO_VRAM_NTILES; i++)
        set_bkg_data(title_logo_bkg_vram_slot[i], 1u, out + (uint16_t)i * 16u);
}

void title_logo_bkg_vram_restore(void) { // each slot → ROM tile (VRAM − 128), same as boot layout for that index
    uint8_t sb = (uint8_t)_current_bank;
    uint8_t i;
    SWITCH_ROM(BANK(tileset));
    for (i = 0u; i < TITLE_LOGO_VRAM_NTILES; i++) {
        uint8_t v = title_logo_bkg_vram_slot[i];
        set_bkg_data(v, 1u, tileset_tiles + (uint16_t)(v - TILESET_VRAM_OFFSET) * 16u);
    }
    SWITCH_ROM(sb);
}
