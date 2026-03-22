#include <gb/hardware.h> // DIV_REG, LY_REG
#include <rand.h>        // initrand, rand
#include <stdint.h>
#include "seed_entropy.h"

uint16_t seed_entropy_mix(uint16_t entropy_hint, uint16_t frame_counter) { // fold timing + counters into 16 bits
	uint8_t div0 = DIV_REG;
	uint8_t ly   = LY_REG; // current LCD scanline; changes between reads
	uint8_t div1 = DIV_REG;
	uint16_t lo  = (uint16_t)entropy_hint + (uint16_t)frame_counter * 31u;
	uint16_t hi  = (uint16_t)(div0 * 257u) + (uint16_t)div1 + (uint16_t)ly * 7u;
	return (uint16_t)(lo ^ (hi << 5) ^ (hi >> 3)); // avalanche bits across both halves
}

void seed_entropy_random_indices(uint16_t mix, uint8_t *d, uint8_t *n, uint8_t *p) { // three draws after reseeding PRNG
	initrand(mix);
	*d = (uint8_t)(rand() % 40u);
	*n = (uint8_t)(rand() % 40u);
	*p = (uint8_t)(rand() % 40u);
}

uint16_t seed_entropy_random_seed(uint16_t power_on_ticks, uint16_t frame_counter) { // 1..64000 inclusive
	uint8_t d, n, p;
	uint16_t mix = seed_entropy_mix(power_on_ticks, frame_counter);
	seed_entropy_random_indices(mix, &d, &n, &p);
	uint16_t s = 1u + (uint16_t)d + 40u * (uint16_t)n + 1600u * (uint16_t)p; // bijective mapping for valid triples
	if (!s) s = 1u;
	return s;
}
