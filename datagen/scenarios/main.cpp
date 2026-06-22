#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdint.h>
#include "src/common.hpp"
#include "src/cmd_argc.hpp"
#include "src/isp_edge.hpp"
#include "src/fib.hpp"
#include "src/scenarios.hpp"

uint32_t total_num = 256000;
std::string out_path = "";
int host_param = 72;

int mode = 0;
std::string mode_str = "";

int
main(int argc, char** argv)
{
    int ret;

    ret = parse_cmd_args(argc, argv);
    if (ret < 0)
        return -1;

    ret = gnrt_fib(mode, mode_str, total_num);
    if (ret < 0)
        return -1;

    return 0;
}