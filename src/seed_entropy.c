/* Mix DIV, power-on and frame count for seed entropy; then rand 0..39 three times. */
#include <gb/hardware.h>
#include <rand.h>
#include <stdint.h>
#include "seed_entropy.h"

uint16_t seed_entropy_mix(uint16_t power_on_ticks, uint16_t frame_counter) {
	uint8_t div0 = DIV_REG;
	uint8_t div1 = DIV_REG;  // a few cycles later
	uint16_t lo = (uint16_t)power_on_ticks + (uint16_t)frame_counter * 31u;
	uint16_t hi = (uint16_t)(div0 * 257u) + (uint16_t)div1;
	return (uint16_t)(lo ^ (hi << 5) ^ (hi >> 3));
}

void seed_entropy_random_indices(uint16_t mix, uint8_t *d, uint8_t *n, uint8_t *p) {
	initrand(mix);
	*d = (uint8_t)(rand() % 40u);
	*n = (uint8_t)(rand() % 40u);
	*p = (uint8_t)(rand() % 40u);
}

uint16_t seed_entropy_random_seed(uint16_t power_on_ticks, uint16_t frame_counter) {
	uint8_t d, n, p;
	uint16_t mix = seed_entropy_mix(power_on_ticks, frame_counter);
	seed_entropy_random_indices(mix, &d, &n, &p);
	uint16_t s = 1u + (uint16_t)d + 40u * (uint16_t)n + 1600u * (uint16_t)p;
	if (!s) s = 1u;
	return s;
}
