#ifndef __SCAMPTRIE_DYNAMIC_H__
#define __SCAMPTRIE_DYNAMIC_H__

#include <cstdio>
#include <string>
#include <vector>

#include "scamptrie_base.h"
#include "moo/moo_weights.h"
#include "utils/scamptrie_hash.h"
#include "utils/nexthop_bitmap_bucket.h"
#include "utils/nexthop_bucket.h"
#include "utils/ram_pool.h"
#include "utils/bmp_manager.h"
#include "utils/mem_init.h"

class ScampTrieDynamic {
    friend class MemInit;
    // 这两个开关现固定为 True：移除运行期可配置逻辑
    static constexpr bool enable_hash = true;
    static constexpr bool enable_free_child = true;

    struct ExtraChildInfo {
        uint8_t ip_sub6;
        ScampTrieNode* node;
        bool operator<(const ExtraChildInfo& other) const { return ip_sub6 < other.ip_sub6; }
    };
public:
    ScampTrieDynamic();
    ~ScampTrieDynamic();
    
    void set_ram_allocation(uint64_t total_ram, const MOOWeights& weights);
    void get_ram_info(uint64_t &ram_used, uint64_t &ram_allocated, uint32_t &tcam_entries);
    // 仅统计流水线每级 RAM 使用（单位：字节），原有接口，保留以兼容其他调用
    void get_stage_usage(uint64_t ram_used_per_stage[RAM_NUM]);

    // 合并接口：同时收集流水线使用和额外存储使用情况
    //  - ram_used_per_stage: 每级流水的实际使用空间（单位：字节）
    //  - fixed_capacity_bits: 其他固定存储总容量（bit）
    //  - fixed_used_bits:     其他固定存储实际使用空间（bit）
    //  - elastic_used_bits:   其他弹性存储实际使用空间（bit）
    void get_storage_usage(uint64_t ram_used_per_stage[RAM_NUM],
                           uint64_t &fixed_capacity_bits,
                           uint64_t &fixed_used_bits,
                           uint64_t &elastic_used_bits) const;

    void get_stage_allocated(uint64_t ram_allocated_per_stage[RAM_NUM]) const;
    void get_stage_type_counts(uint32_t counts[RAM_NUM][7]) const;

    nexthop_t lookup(ip6_t ip, uint32_t &bmp_idx, int start_layer = 129);
    uint32_t reshape();
    uint32_t insert_prefix(ip6_t prefix128, uint8_t len128, nexthop_t nh);

    /** 汇总本次实例上全部 insert_prefix 调用：平均值、P99、最大值（steady_clock，纳秒）。 */
    void print_insert_prefix_timing_stats(FILE* out = stdout, const char* sorted_samples_file = nullptr) const;
    /** 汇总本次实例上全部 insert_prefix 的 stage 抬升循环次数：平均值、P99、最大值。 */
    void print_insert_prefix_stage_raise_stats(FILE* out = stdout, const char* sorted_samples_file = nullptr) const;

    /** 关闭计时以去掉 chrono 与 vector 开销；关闭后不再累加样本。 */
    void set_insert_prefix_timing_enabled(bool enabled) { insert_timing_enabled_ = enabled; }
    bool insert_prefix_timing_enabled() const { return insert_timing_enabled_; }

private:
    // insert a child to Type1, keep Type1 or upgrade to Type2
    uint32_t ins_chd_1to12(ScampTrieNode1* hang_node1, PrefixInfo pInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node);
    // insert a child to Type2, keep Type2 or upgrade to Type3
    uint32_t ins_chd_2to23(ScampTrieNode2* hang_node2, PrefixInfo pInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node);
    // insert a child to Type3, keep Type3
    uint32_t ins_chd_3to3(ScampTrieNode3* hang_node3, PrefixInfo pInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node);

    // collect all children(3+N) of a Type3 node, and rearrange them to 3+N positions
    uint32_t rearrange_free_child(ScampTrieNode3* node3, std::vector<ExtraChildInfo> &extra_child);

    // (hang_node may be nullptr, means new root, bmp_father_idx & ip_128 MUST BE SET)
    // generate a node from 1 prefix under father_node, it must be Type4
    ScampTrieNode4* gen_from_1pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo);
    // generate a node from 2 prefixes under father_node, it must be Type5
    ScampTrieNode5* gen_from_2pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo0, PrefixInfo pInfo1);
    // generate a node from 3 prefixes under father_node, it could be Type1 or 2
    uint32_t gen_from_3pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo0, PrefixInfo pInfo1, PrefixInfo pInfo2, ScampTrieNode* &ret_node);

private:
    RootInfo* root;
    RootInfo* root18[HASH_TABLE_SIZE];
    RootInfo* root64[HASH_TABLE_SIZE];
    ScampTrieHash* hash18;
    ScampTrieHash* hash64;
    NexthopBitmapBucket* nh_bitmap_bucket;
    NexthopBucket* nh_bucket;
    RAMPool* ram_pool;
    BMPManager* bmp_manager;

    uint32_t tcam_entries;

    bool insert_timing_enabled_ = true;
    uint64_t insert_timing_sum_ns_ = 0;
    uint64_t insert_timing_count_ = 0;
    std::vector<uint64_t> insert_timing_ns_samples_;
    uint64_t insert_stage_raise_sum_ = 0;
    uint64_t insert_stage_raise_count_ = 0;
    std::vector<uint32_t> insert_stage_raise_samples_;
};

#endif // __SCAMPTRIE_DYNAMIC_H__