#ifndef SCENARIOS_HPP
#define SCENARIOS_HPP

#include <stdint.h>
#include <string>

enum MODE {
    MODE_NULL = 0,
    BACKBONE,
    ISP_EDGE,
    CAMPUS,
    CLOUD_GATEWAY,
    HOST,
    OTHERS
};

int
gnrt_fib(int mode, std::string mode_str, uint32_t TOTAL_NUM);

#endif  // SCENARIOS_HPP