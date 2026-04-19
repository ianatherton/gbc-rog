#ifndef ENTITY_SPRITES_H
#define ENTITY_SPRITES_H

#include "defs.h"

void entity_sprites_init(void); // palettes + SHOW_SPRITES; hide OAM slots
void entity_sprites_poof_clear_all(void); // new floor / reset — zero death-puff timers
void entity_sprites_enemy_poof_begin(uint8_t slot); // killed enemy: cloud until TTL (after corpse BG drawn)

/* Glide: sprite wx/wy includes walk bob; aura_wx/aura_y is smooth track without bob (same X as sprite here). */
void entity_sprites_set_player_world(int16_t spr_wx, int16_t spr_wy, int16_t aura_wx, int16_t aura_wy);
void entity_sprites_clear_player_world(void); // use px*8, py*8 after scroll ends
void entity_sprites_set_player_facing(int8_t dir_x); // -1 left (flip), +1 right (normal)
void entity_sprites_player_hurt_flash(void); // ~1 s red ↔ gold on OCP PAL_PLAYER (~8-frame beat)
void entity_sprites_vbl_tick(void); // 60Hz timers for palette flashes

void entity_sprites_refresh_player_only(uint8_t px, uint8_t py); // update cached player tile + player OAM only
void entity_sprites_refresh_enemy(uint8_t slot); // update one enemy OAM slot or hide it
void entity_sprites_refresh_oam_only(uint8_t px, uint8_t py); // player + enemies + belt/hide sweep; no pit ladder reprobe
void entity_sprites_refresh_all(uint8_t px, uint8_t py); // refresh_oam_only + ladder cache for pit-arrow VBL

#define ENTITY_LUNGE_FRAMES       10u // ~167ms at 60Hz: snappy but readable
#define ENTITY_LUNGE_HIT_FLASH_NONE 255u // 5th arg to run_player_lunge — skip mid-strike palette pop
void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dir_x, int8_t dir_y, uint8_t hit_enemy_slot);
void entity_sprites_enemy_hit_flash_clear(uint8_t slot); // stop hit ramp if enemy dies same turn
void entity_sprites_run_enemy_lunge(uint8_t px, uint8_t py, uint8_t slot, uint8_t tgx, uint8_t tgy);
void entity_sprites_run_enemy_lunges_batch(uint8_t px, uint8_t py, const uint8_t *slots, uint8_t count); // concurrent lunge for all attackers

void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py,
                                     const uint8_t *old_ex, const uint8_t *old_ey,
                                     const uint8_t *old_alive);

#endif // ENTITY_SPRITES_H
