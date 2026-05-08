#ifndef ROOT_ICON_H
#define ROOT_ICON_H

#include <gbdk/platform.h>
#include <stdint.h>

// Returns 1 and sets out_x/out_y to the enemy tile to show the root icon over.
// Returns 0 when no rooted on-screen enemy should be shown this frame (dark phase or none rooted).
// Called every VBL from entity_sprites_vbl_tick — manages its own cycle/blink state internally.
uint8_t root_icon_next(uint8_t *out_x, uint8_t *out_y) BANKED;

#endif // ROOT_ICON_H
