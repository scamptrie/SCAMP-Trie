#ifndef __RAND_GEN_H__
#define __RAND_GEN_H__

#include <cstdint>
#include <random>

#define RANDMAX ((uint32_t)1e9 + 7)

// [0, rand_max)
inline uint32_t rand_gen32(uint32_t rand_max = RANDMAX) {
    return ((uint64_t)rand() * rand()) % rand_max;
}

inline double rand_double(double rand_min, double rand_max) {
    return rand_min + (rand_max - rand_min) * rand_gen32(RANDMAX) / RANDMAX;
}

// true with probability p
inline bool rand_check_p(double p) {
    if (p > 1.0) return true;
    uint32_t rand_val = rand_gen32(RANDMAX);
    return rand_val < p * RANDMAX;
}

#endif // __RAND_GEN_H__