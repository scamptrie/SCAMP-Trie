#include "host.hpp"
#include "common.hpp"
#include "fib.hpp"

void
gnrt_host_fib(int param, std::string fib_path, uint32_t pfx_num)
{
    struct FIB fib;
    ipv6_t ipv6;
    ipv6_t tmp_ipv6;
    uint32_t num;

    std::string fib_file = fib_path + ".fib";
    OPEN_FILE_OPT(file, fib_file, "w");

    // 随机分配
    fib.clear();
    for (num = 0; num < pfx_num; ++ num) {
        while (true) {
            ipv6 = {.hi = HOST_PREFIX, .lo = 0};
            ipv6 |= getRandomIPv6(param);
            if (fib.pfx_maps[64].find(ipv6) == fib.pfx_maps[64].end()) {
                fib.pfx_maps[64][ipv6] = true;
                break;
            }
        }
        fprintf(file, "%llX.%llX/128\n", ipv6.hi, ipv6.lo);
    }

    // 顺序分配
    /*
    for (num = 0; num < pfx_num; ++ num) {
        ++ ipv6.lo;
        fprintf(file, "%llX.%llX/128\n", ipv6.hi, ipv6.lo);
    }
    */

    CLOSE_FILE(file);
}