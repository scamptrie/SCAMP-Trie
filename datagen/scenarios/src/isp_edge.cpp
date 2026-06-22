#include "isp_edge.hpp"
#include "common.hpp"

void
split_asn(std::string asn_str, std::vector<uint64_t>& asn)
{
    std::string str;
    int flag = 1;

    asn.clear();

    while (flag) {
        flag = 0;
        int length = asn_str.length();
        for (int i = 0; i < length; ++ i) {
            if (asn_str[i] == '_') {
                str = asn_str.substr(0, i);
                asn.emplace_back(std::stoull(str));
                asn_str = asn_str.substr(i + 1);
                flag = 1;
                break;
            }
        }
    }

    asn.emplace_back(std::stoull(asn_str));
}

void
build_asn_idx_map(std::vector<uint64_t>& asn, uint8_t isp_idx)
{
    for (uint32_t i = 0; i < asn.size(); ++ i) {
        asn_idx_map[asn[i]] = isp_idx;
    }
}

inline ipv6_t
ipv6_format_conversion(std::string ipv6) {
    ipv6_t res;
    res.hi = 0;
    res.lo = 0;

    std::vector<std::string> ipv6_sep;
    int length = ipv6.size();
    int count = 0;
    for (int i = 0; i < length; ++ i) {
        if (ipv6[i] == ':' && (i + 1 < length && ipv6[i+1] != ':')) {
            count ++;
        }
        if (ipv6[i] == ':' && (i + 1 < length && ipv6[i+1] == ':')) {
            i ++;
        }
    }
    int pos_temp = ipv6.find("::");
    int rest;
    if (pos_temp == 0 || pos_temp == ipv6.size()-2) rest = 7 - count;
    else rest = 6 - count;
    if (ipv6.size() == 2) {
        rest = 8;
    }
    int pos = 0;
    while ((pos = ipv6.find(":")) != std::string::npos) {
        int pos2;
        pos2 = ipv6.find("::");
        if (pos == pos2) {
            std::string temp = ipv6.substr(0, pos);
            if (temp.size() > 0) ipv6_sep.emplace_back(temp);
            while (rest--) {
                ipv6_sep.emplace_back("0");
            }
            ipv6 = ipv6.substr(pos + 2);
        } else {
            std::string temp = ipv6.substr(0, pos);
            if (temp.size() > 0) ipv6_sep.emplace_back(temp);
            ipv6 = ipv6.substr(pos + 1);
        }
    }
    if (ipv6.size() > 0)
        ipv6_sep.emplace_back(ipv6);

    //for (auto& str : ipv6_sep) LOG_INFO("%s\n", str.c_str());

    for (int i = 0; i < 4; i ++) {
        uint64_t temp = std::stoull(ipv6_sep[i], 0, 16);
        res.hi += (temp << 16*(3-i));
    }
    for (int i = 4; i < 8; i ++) {
        uint64_t temp = std::stoull(ipv6_sep[i], 0, 16);
        res.lo += (temp << 16*(7-i));
    }

    //std::cout << "hi: "<< res.hi << std::endl;
    //std::cout << "lo: "<< res.lo << std::endl;

    return res;
}

void
load_prefixes(const std::string& map_file, struct ISP* isps)
{
    uint8_t len;
    char ipv6_str[256];
    ipv6_t ipv6;
    char ans_str[512];
    struct Prefix prefix;
    std::vector<uint64_t> asn;
    uint32_t idx;

    OPEN_FILE_OPT(file, map_file, "r");
    while (fscanf(file, "%s %hhu %s", ipv6_str, &len, ans_str) != EOF) {
        ipv6 = ipv6_format_conversion(ipv6_str);
        split_asn(ans_str, asn);
        //printf("hi = %llX, lo = %llX, len = %hhu\n", ipv6.hi, ipv6.lo, len);
        for (uint32_t i = 0; i < asn.size(); ++ i) {
            if (asn_idx_map.find(asn[i]) != asn_idx_map.end()) {
                idx = asn_idx_map[asn[i]];
                prefix.ipv6 = ipv6;
                prefix.len = len;
                isps[idx].owned_pfxes.emplace_back(prefix);
                break;
            }
        }
    }
    CLOSE_FILE(file);
}

void
load_isp(const std::string& map_file, struct ISP* isps)
{
    asn_idx_map.clear();

    /* Load fundamental info for each ISP. */
    for (uint32_t i = 0; i < 7; ++ i) {
        isps[i].owned_pfxes.clear();
        isps[i].name = isp_infos[i].name;
        isps[i].abbr = isp_infos[i].abbr;
        isps[i].region = isp_infos[i].region;
        split_asn(isp_asn_str[i], isps[i].asn);
        build_asn_idx_map(isps[i].asn, i);
    }

    /* Load associated prefixes of each ISP from pfx_asn map file. */
    load_prefixes(map_file, isps);
}

void
print_isp_info(struct ISP* isps)
{
    printf("ISP info:\n");
    for (uint32_t i = 0; i < 7; ++ i) {
        printf("\tISP Name: %s   abbr: %s   region: %s\n",
            isps[i].name.c_str(), isps[i].abbr.c_str(), isps[i].region.c_str());
        printf("\tISP ASN: ");
        for (uint32_t t = 0; t < isps[i].asn.size(); ++ t) {
            printf("%llu ", isps[i].asn[t]);
        }
        printf("\n");
        printf("---------------------------\n");
    }

    printf("\n\nASN idx map:\n");
    int num = 0;
    for (std::unordered_map<uint64_t, uint32_t>::iterator it = asn_idx_map.begin();
        it != asn_idx_map.end(); ++ it) {
        if (num % 5 == 0)
            printf("\n");
        printf("%llu --> %u ", it->first, it->second);
        num ++;
    }

    printf("\n\nOwned prefixes:\n");
    for (uint32_t i = 0; i < 7; ++ i) {
        printf("\tISP Name: %s   abbr: %s   region: %s\n",
            isps[i].name.c_str(), isps[i].abbr.c_str(), isps[i].region.c_str());
        printf("Total number of prefixes = %lu\n", isps[i].owned_pfxes.size());
        for (uint32_t t = 0; t < isps[i].owned_pfxes.size(); ++ t) {
            printf("%llX.%llX %hhu\n",
                isps[i].owned_pfxes[t].ipv6.hi,
                isps[i].owned_pfxes[t].ipv6.lo,
                isps[i].owned_pfxes[t].len);
            if (t > 10) {
                break;
            }
        }
        printf("---------------------------\n");
    }
}

void
gnrt_isp_edge_fib(struct ISP* isps, uint32_t TOTAL_NUM)
{
    std::vector<struct Prefix> candidate_pfxes;
    std::vector<uint8_t> already_lens;
    struct Prefix prefix;
    ipv6_t ipv6;
    ipv6_t tmp_ipv6;
    uint32_t pfx_num;

    for (uint32_t i = 0; i < 7; ++ i) {
        /* Clear FIB. */
        isps[i].fib.clear();
        /* Clear already lengths. */
        already_lens.clear();
        /* Generate prefixes for each length in specific proportion. */
        for (uint8_t len = BEGIN_LENGTH; len <= 64; ++ len) {
            if (isp_pfx_prop[i][len - BEGIN_LENGTH] > 0) {
                /* Add already length. */
                already_lens.emplace_back(len);

                pfx_num = (TOTAL_NUM * isp_pfx_prop[i][len - BEGIN_LENGTH]) / 100;
                //printf("isp = %u, len = %hhu, num = %u\n", i, len, pfx_num);
                /* Clear candidate prefixes. */
                candidate_pfxes.clear();
                /* Select candidate prefixes. */
                for (uint32_t f = 0; f < isps[i].owned_pfxes.size(); ++ f) {
                    if (isps[i].owned_pfxes[f].len < len) {
                        candidate_pfxes.emplace_back(isps[i].owned_pfxes[f]);
                    }
                }
                /* Generate prefixes. */
                for (uint32_t num = 0; num < pfx_num; ++ num) {
                    while (true) {
                        /* Select candidate prefixe randomly. */
                        uint32_t pfx_idx = (__dis32(__gen)) % candidate_pfxes.size();
                        prefix = candidate_pfxes[pfx_idx];
                        ipv6 = mask_ipv6_with_len(prefix.ipv6, prefix.len);
                        ipv6 |= getRandomIPv6(prefix.len);
                        int flag = 0;
                        for (uint32_t l = 0; l < already_lens.size(); ++ l) {
                            tmp_ipv6 = mask_ipv6_with_len(ipv6, already_lens[l]);
                            if (isps[i].fib.pfx_maps[already_lens[l]].find(tmp_ipv6)
                                != isps[i].fib.pfx_maps[already_lens[l]].end()) {
                                flag = 1;
                                break;
                            }
                        }
                        if (flag == 0) {
                            /* Add prefix */
                            ipv6 = mask_ipv6_with_len(ipv6, len);
                            isps[i].fib.pfx_maps[len][ipv6] = true;
                            break;
                        }
                    }
                }
            }
        }

        /* Print prefixes into file. */
        std::string fib_path = "../data/test-data/" + isps[i].abbr + ".fib";
        OPEN_FILE_OPT(file, fib_path, "w");
        for (uint8_t l = 0; l < already_lens.size(); ++ l) {
            for (std::unordered_map<ipv6_t, bool, struct ipv6_hash>::iterator it = isps[i].fib.pfx_maps[already_lens[l]].begin();
                it != isps[i].fib.pfx_maps[already_lens[l]].end(); ++ it) {
                fprintf(file, "%llX.%llX/%hhu\n", it->first.hi, it->first.lo, already_lens[l]);
            }
        }
        CLOSE_FILE(file);

        /* Clear FIB. */
        isps[i].fib.clear();
    }
}