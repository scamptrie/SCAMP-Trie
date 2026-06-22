#include "cloud_gateway.hpp"
#include "common.hpp"
#include "fib.hpp"
#include <string>
#include <vector>

/*
 * Azure 风格：先分配 /56，再在其下派生 /64；80% 租户 1 个 /64，20% 租户 2～256 个 /64。
 * 无参：生成 4 个固定规模 cg_256k, cg_512k, cg_1m, cg_6500。
 * 有参：生成恰好 target_pfx_num 条 /64 前缀写入 out_path.fib。
 */
static uint32_t tanants_scale[4] = {256000, 512000, 6329, 6500};
static const char* names[4] = {"256k", "512k", "1m", "6500"};

/* 单次生成逻辑：返回本 tenant 产生的 /64 数量，并把新前缀追加到 out_vec（可为 NULL） */
static uint32_t
add_tenant_azure(struct FIB& fib, std::vector<ipv6_t>* out_vec, uint32_t* pfx_num, uint32_t tenant_scale, uint32_t num)
{
    ipv6_t ipv6, tmp_ipv6;
    uint32_t subnet_scale;

    while (true) {
        ipv6 = {.hi = CLOUD_PREFIX, .lo = 0};
        ipv6 |= getRandomIPv6(CLOUD_PFX_LEN);
        ipv6 = mask_ipv6_with_len(ipv6, CLOUD_TENANT_VPC_PFX_LEN);
        if (fib.pfx_maps[CLOUD_TENANT_VPC_PFX_LEN].find(ipv6) == fib.pfx_maps[CLOUD_TENANT_VPC_PFX_LEN].end()) {
            fib.pfx_maps[CLOUD_TENANT_VPC_PFX_LEN][ipv6] = true;
            break;
        }
    }
    if (num < (tenant_scale * 0.8)) {
        subnet_scale = 1;
    } else {
        if ((*pfx_num + (2 * (tenant_scale - num)) + 256) < (tenant_scale * 1.25)) {
            subnet_scale = ((__dis32(__gen)) % 255) + 2;
        } else {
            subnet_scale = 2;
        }
    }
    uint32_t added = 0;
    for (uint64_t t = 0; t < subnet_scale; ++t) {
        tmp_ipv6 = ipv6;
        tmp_ipv6.hi |= t;
        if (fib.pfx_maps[CLOUD_TENANT_SUBNET_PFX_LEN].find(tmp_ipv6) == fib.pfx_maps[CLOUD_TENANT_SUBNET_PFX_LEN].end()) {
            fib.pfx_maps[CLOUD_TENANT_SUBNET_PFX_LEN][tmp_ipv6] = true;
            if (out_vec) out_vec->push_back(tmp_ipv6);
            (*pfx_num)++;
            added++;
        }
    }
    return added;
}

void
gnrt_cloud_edge_fib()
{
    std::string fib_path = "../data/test-data/cg_";
    struct FIB fib;
    uint32_t num, pfx_num, tenant_scale;

    for (int i = 0; i < 4; i++) {
        tenant_scale = tanants_scale[i];
        num = 0;
        pfx_num = 0;
        fib.clear();
        while (num < tenant_scale) {
            add_tenant_azure(fib, nullptr, &pfx_num, tenant_scale, num);
            num++;
        }
        std::string fib_file = fib_path + names[i] + ".fib";
        OPEN_FILE_OPT(file, fib_file, "w");
        for (auto it = fib.pfx_maps[CLOUD_TENANT_SUBNET_PFX_LEN].begin();
             it != fib.pfx_maps[CLOUD_TENANT_SUBNET_PFX_LEN].end(); ++it) {
            fprintf(file, "%llX.%llX/%hhu\n", (unsigned long long)it->first.hi, (unsigned long long)it->first.lo, CLOUD_TENANT_SUBNET_PFX_LEN);
        }
        CLOSE_FILE(file);
    }
}

void
gnrt_cloud_edge_fib(uint32_t target_pfx_num, const std::string& out_path)
{
    struct FIB fib;
    std::vector<ipv6_t> prefix_vec;
    prefix_vec.reserve(target_pfx_num + 256);
    uint32_t pfx_num = 0;
    uint32_t num = 0;
    /* 用近似 tenant 规模驱动，保证至少 target_pfx_num 条（80/20 下约 1.0～1.25 倍） */
    uint32_t tenant_scale = target_pfx_num + 100000;

    fib.clear();
    while (pfx_num < target_pfx_num) {
        add_tenant_azure(fib, &prefix_vec, &pfx_num, tenant_scale, num);
        num++;
    }

    std::string fib_file = out_path + ".fib";
    OPEN_FILE_OPT(file, fib_file, "w");
    for (uint32_t i = 0; i < target_pfx_num; i++) {
        const ipv6_t& p = prefix_vec[i];
        fprintf(file, "%llX.%llX/%hhu\n", (unsigned long long)p.hi, (unsigned long long)p.lo, CLOUD_TENANT_SUBNET_PFX_LEN);
    }
    CLOSE_FILE(file);
}