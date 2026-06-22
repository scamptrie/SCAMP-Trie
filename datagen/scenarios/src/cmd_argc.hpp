#ifndef CMD_ARGS_HPP
#define CMD_ARGS_HPP

#include "common.hpp"
#include "scenarios.hpp"
#include <stdint.h>

extern int mode;
extern std::string mode_str;
extern uint32_t total_num;
extern std::string out_path;   /* host output path (without .fib), used when non-empty */
extern int host_param;         /* host param (e.g. 72), used when -P given */

static void
print_help(char** argv)
{
    printf(
        "#############################################\n"
        "%s -[M:O:N:o:P:h]\n"
        "-M: specify mode using string (example: isp-edge).\n"
        "-O: specify mode using number (2).\n"
        "\t 1 -> backbone -> Backbone\n"
        "\t 2 -> isp-edge -> ISP Edge\n"
        "\t 3 -> campus -> Campus\n"
        "\t 4 -> cloud-gateway -> Cloud Gateway\n"
        "\t 5 -> host -> Host\n"
        "\t 6 -> others -> Others\n"
        "-N: total number / scale (e.g. 262144). Default 256000.\n"
        "-o: output path for host (without .fib); if set, generate single host FIB.\n"
        "-P: host param (e.g. 72). Default 72 when -o is used.\n"
        "-h: print help.\n"
        "Sample: %s -O 2 -N 512000\n"
        "        %s -O 5 -N 262144 -o ../data/test-data/host2_1 -P 72\n",
        argv[0], argv[0], argv[0]
    );
}

static int
parse_cmd_args(int argc, char** argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "M:O:N:o:P:h")) != -1) {
        switch (opt)
        {
        case 'M':
            mode_str = optarg;
            break;
        case 'O':
            mode = atoi(optarg);
            break;
        case 'N':
            total_num = (uint32_t)atol(optarg);
            break;
        case 'o':
            out_path = optarg;
            break;
        case 'P':
            host_param = atoi(optarg);
            break;
        case 'h':
        default:
            print_help(argv);
            return -1;
        }
    }

    if (mode == 0 && mode_str == "") {
        printf("Must specify mode!!!\n");
        return -1;
    }

    return 0;
}

#endif  // CMD_ARGS_HPP