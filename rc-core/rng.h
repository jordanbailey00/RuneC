#ifndef RC_RNG_H
#define RC_RNG_H

#include <stdint.h>

// XORshift32 — deterministic, fast, single uint32 state
static inline uint32_t rc_rng_next(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

// Random int in [0, max] inclusive
static inline int rc_rng_range(uint32_t *state, int max) {
    if (max <= 0) return 0;
    return (int)(rc_rng_next(state) % (uint32_t)(max + 1));
}

#endif
