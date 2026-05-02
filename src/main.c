#include "defs.h"
#include "debug_bank.h"
#include "globals.h"
#include "game_state.h"
#include "states_banked.h"
#include <gbdk/platform.h>

BANKREF_EXTERN(state_title_enter)
BANKREF_EXTERN(state_char_create_enter)
BANKREF_EXTERN(state_gameplay_enter)
BANKREF_EXTERN(state_gameplay_tick)
BANKREF_EXTERN(state_game_over_enter)
BANKREF_EXTERN(state_stats_enter)
BANKREF_EXTERN(state_stats_tick)
BANKREF_EXTERN(state_inventory_enter)
BANKREF_EXTERN(state_inventory_tick)
BANKREF_EXTERN(state_ability_enter)
BANKREF_EXTERN(state_ability_tick)
BANKREF_EXTERN(state_pickup_enter)
BANKREF_EXTERN(state_pickup_tick)
BANKREF_EXTERN(state_transition_enter)
BANKREF_EXTERN(tileset)
#include "lcd.h"
#include "entity_sprites.h"
#include "music.h"
#include "tileset.h"
#include <gb/gb.h>
#include <gbdk/font.h>
#include <string.h>

int main(void) {
    DISPLAY_OFF;
    if (DEVICE_SUPPORTS_COLOR) cpu_fast(); // CGB double-speed for whole run; LCD off per speed-switch timing
    LCDC_REG |= LCDCF_WIN9C00;
    set_default_palette(); // CGB greyscale; title sets menu CRAM, then ui_title_style_end -> load_palettes before char create
    font_init();
    font_load(font_ibm);
    font_color(3, 0);
    {
        uint8_t sb = _current_bank;
        SWITCH_ROM(BANK(tileset));
        set_bkg_data(TILESET_VRAM_OFFSET, TILESET_NTILES_VRAM, tileset_tiles);
        set_bkg_data((uint8_t)(TILESET_VRAM_OFFSET + TILE_UI_SLOT_EMPTY), 1u, // K1 VRAM ← M14 empty-slot art (ROM past first 128)
            tileset_tiles + (uint16_t)TILE_SHEET_M14 * 16u);
        set_bkg_data(TILE_WITCH_BOLT_VRAM, 1u, // dedicated VRAM tile for witch icon/projectile (M12 source is outside first 128)
            tileset_tiles + (uint16_t)TILE_SHEET_M12 * 16u);
        set_sprite_data(TILE_WITCH_BOLT_VRAM, 1u,
            tileset_tiles + (uint16_t)TILE_SHEET_M12 * 16u);
        set_bkg_data(TILE_ZERKER_WHIRLWIND_VRAM, 1u, // dedicated VRAM tile for zerker Whirlwind icon (I10 source exceeds first 128 upload)
            tileset_tiles + (uint16_t)TILE_ITEM_10 * 16u);
        set_sprite_data(TILE_ZERKER_WHIRLWIND_VRAM, 1u,
            tileset_tiles + (uint16_t)TILE_ITEM_10 * 16u);
        set_bkg_data(TILE_KNIGHT_SHIELD_VRAM, 1u, // I9 source for knight shield belt icon + HUD buff sprite
            tileset_tiles + (uint16_t)TILE_ITEM_9 * 16u);
        set_sprite_data(TILE_KNIGHT_SHIELD_VRAM, 1u,
            tileset_tiles + (uint16_t)TILE_ITEM_9 * 16u);
        set_bkg_data(TILE_FOX_J9_VRAM, 1u,
            tileset_tiles + (uint16_t)TILE_FOX_J9 * 16u);
        set_sprite_data(TILE_FOX_J9_VRAM, 1u,
            tileset_tiles + (uint16_t)TILE_FOX_J9 * 16u);
        {
            uint8_t buf[16];
            memcpy(buf, tileset_tiles + (uint16_t)TILE_PLAYER_AURA_ROM_A * 16u, 16u); // sheet uses idx0 as clear; 0→3 remaps made a solid 8×8
            set_bkg_data(TILE_PLAYER_AURA_VRAM_A, 1u, buf);
            set_sprite_data(TILE_PLAYER_AURA_VRAM_A, 1u, buf);
            memcpy(buf, tileset_tiles + (uint16_t)TILE_PLAYER_AURA_ROM_B * 16u, 16u);
            set_bkg_data(TILE_PLAYER_AURA_VRAM_B, 1u, buf);
            set_sprite_data(TILE_PLAYER_AURA_VRAM_B, 1u, buf);
        }
        SWITCH_ROM(sb);
    }
    BANK_DBG("boot_tiles");
    SCX_REG = 0;
    SCY_REG = 0;
    SHOW_BKG;
    DISPLAY_ON;
    BANK_DBG("boot_disp");
    lcd_init_raster();
    BANK_DBG("boot_lcd");
    entity_sprites_init();
    BANK_DBG("boot_spr");
    music_init();
    BANK_DBG("boot_mus");
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    enable_interrupts();

    g_run_entropy = (uint16_t)DIV_REG | ((uint16_t)DIV_REG << 8);

    while (1) {
        if (next_state != current_state) {
            GameState from_st = current_state;
            current_state = next_state;
            switch (current_state) {
            case STATE_TITLE:
                BANK_DBG("m_in_title");
                state_title_enter();
                BANK_DBG("m_out_title");
                break;
            case STATE_CHAR_CREATE:
                BANK_DBG("m_in_class");
                state_char_create_enter();
                BANK_DBG("m_out_class");
                break;
            case STATE_GAMEPLAY:
                if (from_st != STATE_STATS && from_st != STATE_INVENTORY && from_st != STATE_ABILITY
                        && from_st != STATE_PICKUP) {
                    BANK_DBG("m_in_play");
                    state_gameplay_enter();
                    BANK_DBG("m_out_play");
                } else {
                    BANK_DBG("m_same_play");
                }
                break;
            case STATE_STATS:
                BANK_DBG("m_in_stats");
                state_stats_enter();
                BANK_DBG("m_out_stats");
                break;
            case STATE_INVENTORY:
                BANK_DBG("m_in_inv");
                state_inventory_enter();
                BANK_DBG("m_out_inv");
                break;
            case STATE_ABILITY:
                BANK_DBG("m_in_abil");
                state_ability_enter();
                BANK_DBG("m_out_abil");
                break;
            case STATE_PICKUP:
                BANK_DBG("m_in_pu");
                state_pickup_enter();
                BANK_DBG("m_out_pu");
                break;
            case STATE_TRANSITION:
                BANK_DBG("m_in_tr");
                state_transition_enter();
                BANK_DBG("m_out_tr");
                break;
            case STATE_GAME_OVER:
                BANK_DBG("m_in_over");
                state_game_over_enter();
                BANK_DBG("m_out_over");
                break;
            default:
                break;
            }
        }

        switch (current_state) {
        case STATE_GAMEPLAY:
            state_gameplay_tick();
            break;
        case STATE_TRANSITION:
            wait_vbl_done();
            break;
        case STATE_STATS:
            state_stats_tick();
            break;
        case STATE_INVENTORY:
            state_inventory_tick();
            break;
        case STATE_ABILITY:
            state_ability_tick();
            break;
        case STATE_PICKUP:
            state_pickup_tick();
            break;
        default:
            wait_vbl_done();
            break;
        }
    }
}
