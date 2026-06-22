#include "scenarios.hpp"
#include "cmd_argc.hpp"
#include "isp_edge.hpp"
#include "cloud_gateway.hpp"
#include "host.hpp"

int
gnrt_fib(int mode, std::string mode_str, uint32_t TOTAL_NUM)
{
    if (mode == MODE_NULL) {
        if (mode_str == "backbone") {
            mode = BACKBONE;
        } else if (mode_str == "isp-edge") { // 6 个 256K， K=1e3
            mode = ISP_EDGE;
        } else if (mode_str == "campus") {
            mode = CAMPUS;
        } else if (mode_str == "cloud-gateway") {
            mode = CLOUD_GATEWAY;
        } else if (mode_str == "host") { // 2 个，一个随机，一个递增，256K
            mode = HOST;
        } else if (mode_str == "others") {
            mode = OTHERS;
        } else {
            return -1;
        }
    }

    if (mode == BACKBONE) {
        return -1;
    } else if (mode == ISP_EDGE) {
        struct ISP isps[7];
        std::string map_file = "../data/pfx_asn.map";
        load_isp(map_file, isps);
        print_isp_info(isps);
        gnrt_isp_edge_fib(isps, TOTAL_NUM);
        return 0;
    } else if (mode == CAMPUS) {
        return -1;
    } else if (mode == CLOUD_GATEWAY) {
        if (out_path != "") {
            gnrt_cloud_edge_fib(TOTAL_NUM, out_path);
        } else {
            gnrt_cloud_edge_fib();
        }
        return 0;
    } else if (mode == HOST) {
        if (out_path != "") {
            gnrt_host_fib(host_param, out_path, TOTAL_NUM);
        } else {
            gnrt_host_fib(64, "../data/test-data/host-rd64", 256000);
            gnrt_host_fib(72, "../data/test-data/host-rd72", 256000);
            gnrt_host_fib(80, "../data/test-data/host-rd80", 256000);
        }
        return 0;
    } else if (mode == OTHERS) {
        return -1;
    } else {
        return -1;
    }

    return 0;
}
