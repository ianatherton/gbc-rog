#ifndef CAMERA_H
#define CAMERA_H

#include <gb/gb.h>
#include <stdint.h>

void camera_init(uint8_t top_tx, uint8_t top_ty) BANKED;
void camera_scroll_to(uint8_t target_tx, uint8_t target_ty,
                      uint8_t opx, uint8_t opy, uint8_t px, uint8_t py) BANKED;
void camera_shake(void) BANKED;

#endif
