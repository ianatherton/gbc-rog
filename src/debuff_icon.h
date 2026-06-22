#ifndef DEBUFF_ICON_H
#define DEBUFF_ICON_H

#include <gbdk/platform.h>
#include <stdint.h>

// Returns 1 and sets out_x/out_y/out_tile to the enemy tile + VRAM tile (root or stun glyph)
// to show on the single shared debuff-icon sprite. Cycles across enemies with either status.
// Returns 0 when no afflicted on-screen enemy should be shown this frame (dark phase or none).
// Called every VBL from entity_sprites_vbl_tick — manages its own cycle/blink state internally.
uint8_t debuff_icon_next(uint8_t *out_x, uint8_t *out_y, uint8_t *out_tile) BANKED;

#endif // DEBUFF_ICON_H
