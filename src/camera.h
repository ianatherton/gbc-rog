#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

void camera_init(uint8_t top_tx, uint8_t top_ty); // viewport top-left in map tiles → camera_px/py
void camera_scroll_to(uint8_t target_tx, uint8_t target_ty,
                      uint8_t opx, uint8_t opy, uint8_t px, uint8_t py);
void camera_shake(void); // applies lcd_shake_* wobble during gameplay (ISR consumes)

#endif
