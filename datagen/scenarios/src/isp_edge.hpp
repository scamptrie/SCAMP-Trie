#ifndef ISP_EDGE_HPP
#define ISP_EDGE_HPP

#include <string>
#include <random>
#include "fib.hpp"

/*
 * ----------------------------------------------
 * |  No.  |  ISP Name    |  Abbr.   |  Region  |
 * |-------|--------------|----------|----------|
 * |  0    |  Netcologne  |  netclg  |  Germany |
 * |  1    |  Orange      |  orag    |  France  |
 * |  2    |  DTAG        |  dtag    |  Germany |
 * |  3    |  Sky UK      |  sky     |  England |
 * |  4    |  Free SAS    |  free    |  French  |
 * |  5    |  Kabel DE    |  kab     |  Germany |
 * |  6    |  Comcast     |  cmt     |  America |
 * ----------------------------------------------
 */

typedef struct isp_info_t {
    std::string name;
    std::string abbr;
    std::string region;
} isp_info_t;

static isp_info_t isp_infos[7] = {
    {.name = "Netcologne", .abbr = "netclg", .region = "Germany"},
    {.name = "Orange", .abbr = "orag", .region = "France"},
    {.name = "DTAG", .abbr = "dtag", .region = "Germany"},
    {.name = "Sky UK", .abbr = "sky", .region = "England"},
    {.name = "Free SAS", .abbr = "free", .region = "French"},
    {.name = "Kabel DE", .abbr = "kab", .region = "Germany"},
    {.name = "Comcast", .abbr = "cmt", .region = "America"}
};

static std::string isp_asn_str[7] = {
    "8422",                                   // Netcologne
    "3215",                                   // Orange
    "3320",                                   // DTAG
    "5607_207163_39627",                      // Sky UK
    "12322",                                  // Free SAS
    "31334",                                  // Kabel DE
    "7015_7016_7725_7922_11025_13367_13385_"  // Comcast
    "20214_21508_22258_22909_33287_33489_"
    "33490_33491_33650_33651_33652_33653_"
    "33654_33655_33656_33657_33659_33660_"
    "33661_33662_33664_33665_33666_33667_"
    "33668_36732_36733"
};

/* map: asn -> ISP idx */
static std::unordered_map<uint64_t, uint32_t> asn_idx_map;

#define BEGIN_LENGTH 47

/* 47 - 64 */
static uint8_t isp_pfx_prop[7][18] = {
  /* 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 */
    {2, 60, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 32},  // Netcologne
    {0, 0, 0, 0, 0, 0, 0, 3, 16, 80, 0, 0, 0, 0, 0, 0, 0, 1},  // Orange
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 1, 3, 46},  // DTAG
    {0, 0, 0, 0, 0, 0, 0, 0, 5, 78, 0, 0, 0, 2, 0, 0, 1, 14},  // Sky UK
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 18, 70, 0, 1, 0, 7},  // Free SAS
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 55, 8, 35},  // Kabel DE
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 3, 6, 0, 1, 10, 75}   // Comcast
};

struct ISP {
    std::string name;
    std::string abbr;
    std::string region;
    std::vector<uint64_t> asn;
    std::vector<struct Prefix> owned_pfxes;
    struct FIB fib;
};

void
load_isp(const std::string& map_file, struct ISP* isps);

void
print_isp_info(struct ISP* isps);

void
gnrt_isp_edge_fib(struct ISP* isps, uint32_t TOTAL_NUM);

#endif  // ISP_EDGE_HPP