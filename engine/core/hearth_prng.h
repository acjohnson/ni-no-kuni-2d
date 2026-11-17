/* ============================================================================
 * hearth_prng.h — Deterministic 16-Bit Galois LFSR PRNG (ANSI C89 / C99)
 * Identical across x86_64, ARM, Motorola 68000 (Genesis), and WDC 65816 (SNES).
 * ============================================================================ */

#ifndef HEARTH_PRNG_H
#define HEARTH_PRNG_H

#include "hearth_types.h"

typedef struct {
    uint16_t state;
} HearthPrng;

HEARTH_INLINE void hearth_prng_seed(HearthPrng *rng, uint16_t seed) {
    rng->state = seed ? seed : (uint16_t)0xACE1u;
}

HEARTH_INLINE uint16_t hearth_prng_next16(HearthPrng *rng) {
    uint16_t lsb = (uint16_t)(rng->state & 1u);
    rng->state = (uint16_t)(rng->state >> 1);
    if (lsb) {
        rng->state = (uint16_t)(rng->state ^ 0xB400u);
    }
    return rng->state;
}

HEARTH_INLINE int16_t hearth_prng_range(HearthPrng *rng, int16_t min_v, int16_t max_v) {
    uint16_t span;
    if (max_v <= min_v) return min_v;
    span = (uint16_t)(max_v - min_v + 1);
    return (int16_t)(min_v + (int16_t)(hearth_prng_next16(rng) % span));
}

#endif /* HEARTH_PRNG_H */
