/** Seed entropy: mix hardware/time sources and produce three independent 0..39 indices.
 * Used so descriptor/noun/place words are well distributed. */
#ifndef SEED_ENTROPY_H
#define SEED_ENTROPY_H

#include <stdint.h>

/** Build a 16-bit mix from power_on_ticks, frame_counter, and fresh DIV reads. */
uint16_t seed_entropy_mix(uint16_t power_on_ticks, uint16_t frame_counter);

/** Seed RNG from mix, then set *d,*n,*p to three independent values in 0..39.
 * Run seed = 1 + d + 40*n + 1600*p. */
void seed_entropy_random_indices(uint16_t mix, uint8_t *d, uint8_t *n, uint8_t *p);

/** One-shot: mix entropy and return a run seed in 1..64000 from three random indices. */
uint16_t seed_entropy_random_seed(uint16_t power_on_ticks, uint16_t frame_counter);

#endif
