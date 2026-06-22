#ifndef HOST_HPP
#define HOST_HPP

#include <stdint.h>
#include <string>

#define HOST_PFX_LEN 128
#define HOST_PREFIX 0x6400000000000000

void
gnrt_host_fib(int param, std::string fib_path, uint32_t pfx_num);

#endif // HOST_HPP