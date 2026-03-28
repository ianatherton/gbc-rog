#ifndef ENTITY_SPRITES_H
#define ENTITY_SPRITES_H

#include "defs.h"

void entity_sprites_init(void); // palettes + SHOW_SPRITES; hide OAM slots

/* Optional world pixel override for player (else tile px,py × 8). Used during camera scroll. */
void entity_sprites_set_player_world(int16_t wx, int16_t wy);
void entity_sprites_clear_player_world(void); // use px*8, py*8 after scroll ends

void entity_sprites_refresh(uint8_t px, uint8_t py); // OAM from map + camera + lunges

/* ~333ms at 60Hz; still readable bump. Lower = snappier turns. */
#define ENTITY_LUNGE_FRAMES 20u
void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dir_x, int8_t dir_y); // blocks on VBlank
void entity_sprites_run_enemy_lunge(uint8_t px, uint8_t py, uint8_t slot, uint8_t tgx, uint8_t tgy);

void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py, // smooth slide from old to current positions over SCROLL_SPEED frames
                                     const uint8_t *old_ex, const uint8_t *old_ey);

#endif // ENTITY_SPRITES_H
