#ifndef __MOO_WEIGHTS_H__
#define __MOO_WEIGHTS_H__

#include "../scamptrie_base.h"

struct MOOWeights {
    double weights[RAM_NUM];

    double& operator[](int idx) { return weights[idx]; }
    const double& operator[](int idx) const { return weights[idx]; }

    void normalize() {
        double sum = 0.0;
        for (int i = 0; i < RAM_NUM; ++i) sum += weights[i];
        if (sum > 0) {
            double scale = 1100.0 / sum;
            for (int i = 0; i < RAM_NUM; ++i) weights[i] *= scale;
        }
    }
};

// FIB entry structure
struct FIBEntry {
    ip6_t prefix;
    uint8_t prefix_len;
    nexthop_t nexthop;
    
    FIBEntry(ip6_t p, uint8_t len, nexthop_t nh) 
        : prefix(p), prefix_len(len), nexthop(nh) {}
};

#endif // __MOO_WEIGHTS_H__