#ifndef SEED_ENTROPY_H
#define SEED_ENTROPY_H

#include <stdint.h>

uint16_t seed_entropy_mix(uint16_t entropy_hint, uint16_t frame_counter); // hardware-tinted mixing

void seed_entropy_random_indices(uint16_t mix, uint8_t *d, uint8_t *n, uint8_t *p); // fills word indices after initrand(mix)

uint16_t seed_entropy_random_seed(uint16_t entropy_hint, uint16_t frame_counter); // packaged seed for title START

#endif
