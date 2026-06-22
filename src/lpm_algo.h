#ifndef LPM_ALGO_H
#define LPM_ALGO_H

#include <cstdint>

typedef uint64_t nexthop_t;

struct ip6_t {
    uint64_t hi;
    uint64_t lo;
};

#endif
