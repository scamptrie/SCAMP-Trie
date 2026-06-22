#ifndef CLOUD_GATEWAY_HPP
#define CLOUD_GATEWAY_HPP

#include <stdint.h>
#include <string>

#define CLOUD_PFX_LEN 32
#define CLOUD_TENANT_VPC_PFX_LEN 56
#define CLOUD_TENANT_SUBNET_PFX_LEN 64
#define CLOUD_PREFIX 0x2400650000000000

/* 无参：生成 4 个固定规模到 data/test-data/cg_*.fib */
void
gnrt_cloud_edge_fib();

/* 有参：按 Azure 策略生成恰好 target_pfx_num 条 /64 前缀，写入 out_path.fib */
void
gnrt_cloud_edge_fib(uint32_t target_pfx_num, const std::string& out_path);

#endif