/*
 * 实现策略（压缩策略对Trie存储规模统计）：
 * 1. 森林Trie：r0 存 len<=16；hash_16 存 16<len<=64，key 为前 16 位；hash_64 存 len>64，key 为前 64 位。
 * 2. 插入时根据 len 选根，用 extract_sub_prefix 得到子前缀后在对应根上 insert_prefix(sub, 0)。
 * 3. 统计：遍历所有有效根（r0 + hash_16 + hash_64），先不压缩统计节点数得 origin/NI-Comp，
 *    再对每个根 compress()，再统计节点数与 Half-Size（collapsed 且子树前缀<=1 或 非 collapsed 且直接儿子<=2 记 128bit）。
 * 4. 修正：(1) 第2-5列均加上两层 hash 使用量：每有一个根节点（hash_16/hash_64 中的根）额外 65bit。
 *    (2) 第3-5列均加上 bitmap 独立存储：第3列用压缩前所有节点的 bitmap 去重后种类数×64bit；
 *        第4-5列用压缩后所有非 collapsed 节点的 bitmap 去重后种类数×64bit。
 * 5. 输出 res_compress.csv：首行 "dataset,origin,NI-Comp,Collapsed,Half-Size"，每行一个数据集、5 列。
 */

#include "compress_trie.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <set>

// 从 prefix 的 [start_bit, start_bit+effective_len) 提取子前缀，新前缀的高 effective_len 位为这些位
// 使得 get_6bit_slice(new_prefix, level=0) 得到 original[start_bit..start_bit+6)
Prefix extract_sub_prefix(const Prefix& prefix, int start_bit, int effective_len) {
    if (effective_len <= 0) {
        return Prefix(0, 0, 0);
    }
    uint64_t ip_high = prefix.ip_high;
    uint64_t ip_low = prefix.ip_low;
    uint64_t new_high = 0;
    uint64_t new_low = 0;

    if (start_bit + effective_len <= 64) {
        // 全部在 ip_high 内：取 [start_bit, start_bit+effective_len)，放到新前缀高 effective_len 位
        uint64_t extracted = (ip_high << start_bit) >> (64 - effective_len);
        new_high = extracted << (64 - effective_len);
    } else if (start_bit >= 64) {
        // 全部在 ip_low 内
        int off = start_bit - 64;
        uint64_t extracted = (ip_low << off) >> (64 - effective_len);
        new_high = extracted << (64 - effective_len);
    } else {
        // 跨 ip_high 与 ip_low：[start_bit, 64) 来自 ip_high，[64, start_bit+effective_len) 来自 ip_low
        int bits_from_high = 64 - start_bit;
        int bits_from_low = effective_len - bits_from_high;
        uint64_t seg_high = (ip_high << start_bit) >> start_bit;  // ip_high 的低 bits_from_high 位
        uint64_t seg_low = ip_low >> (64 - bits_from_low);
        uint64_t extracted = (seg_high << bits_from_low) | seg_low;
        new_high = extracted << (64 - effective_len);
    }

    return Prefix(new_high, new_low, static_cast<uint8_t>(effective_len));
}

ForestTrie::ForestTrie() {
    r0 = std::make_unique<TrieNode>();
}

void ForestTrie::insert_prefix(const Prefix& prefix) {
    int len = prefix.length;
    if (len <= 0) return;

    if (len <= HASH_BITS_16) {
        Prefix sub = extract_sub_prefix(prefix, 0, len);
        r0->insert_prefix(sub, 0);
        return;
    }
    if (len <= HASH_BITS_64) {
        uint16_t key16 = static_cast<uint16_t>(prefix.ip_high >> (64 - HASH_BITS_16));
        auto it = hash_16.find(key16);
        if (it == hash_16.end()) {
            it = hash_16.emplace(key16, std::make_unique<TrieNode>()).first;
        }
        Prefix sub = extract_sub_prefix(prefix, HASH_BITS_16, len - HASH_BITS_16);
        it->second->insert_prefix(sub, 0);
        return;
    }
    // len > 64
    uint64_t key64 = prefix.ip_high;
    auto it = hash_64.find(key64);
    if (it == hash_64.end()) {
        it = hash_64.emplace(key64, std::make_unique<TrieNode>()).first;
    }
    int eff_len = len - HASH_BITS_64;
    if (eff_len > 64) eff_len = 64;
    Prefix sub = extract_sub_prefix(prefix, HASH_BITS_64, eff_len);
    it->second->insert_prefix(sub, 0);
}

void ForestTrie::compress() {
    if (r0) r0->compress();
    for (auto& p : hash_16) {
        if (p.second) p.second->compress();
    }
    for (auto& p : hash_64) {
        if (p.second) p.second->compress();
    }
}

// 统计单个根下节点数（压缩前：所有节点；压缩后：压缩后的节点）
static int count_nodes(TrieNode* node) {
    if (node == nullptr) return 0;
    int n = 1;
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            n += count_nodes(node->children[i].get());
        }
    }
    return n;
}

// Half-Size：压缩后每个节点 256bit，但满足条件的记 128bit
// collapsed 且子树前缀总数 <= 1 -> 128bit；非 collapsed 且直接儿子数 <= 2 -> 128bit
static uint64_t count_half_size_bits(TrieNode* node) {
    if (node == nullptr) return 0;
    int child_count = node->get_child_count();
    int prefix_count = node->count_prefixes();

    uint64_t self_bits = 256;
    if (node->collapsed) {
        if (prefix_count <= 1) self_bits = 128;
    } else {
        if (child_count <= 2) self_bits = 128;
    }

    uint64_t child_bits = 0;
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            child_bits += count_half_size_bits(node->children[i].get());
        }
    }
    return self_bits + child_bits;
}

static int count_nodes_from_forest(ForestTrie& forest) {
    int total = 0;
    if (forest.r0) total += count_nodes(forest.r0.get());
    for (auto& p : forest.hash_16) {
        if (p.second) total += count_nodes(p.second.get());
    }
    for (auto& p : forest.hash_64) {
        if (p.second) total += count_nodes(p.second.get());
    }
    return total;
}

// 收集所有节点的 bitmap 到集合（去重）
static void collect_distinct_bitmaps_all(TrieNode* node, std::set<uint64_t>& out) {
    if (node == nullptr) return;
    out.insert(node->bitmap);
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            collect_distinct_bitmaps_all(node->children[i].get(), out);
        }
    }
}

// 压缩后只收集非 collapsed 节点的 bitmap（去重）
static void collect_distinct_bitmaps_non_collapsed(TrieNode* node, std::set<uint64_t>& out) {
    if (node == nullptr) return;
    if (!node->collapsed) {
        out.insert(node->bitmap);
    }
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            collect_distinct_bitmaps_non_collapsed(node->children[i].get(), out);
        }
    }
}

static void collect_distinct_bitmaps_from_forest_all(ForestTrie& forest, std::set<uint64_t>& out) {
    if (forest.r0) collect_distinct_bitmaps_all(forest.r0.get(), out);
    for (auto& p : forest.hash_16) {
        if (p.second) collect_distinct_bitmaps_all(p.second.get(), out);
    }
    for (auto& p : forest.hash_64) {
        if (p.second) collect_distinct_bitmaps_all(p.second.get(), out);
    }
}

static void collect_distinct_bitmaps_from_forest_non_collapsed(ForestTrie& forest, std::set<uint64_t>& out) {
    if (forest.r0) collect_distinct_bitmaps_non_collapsed(forest.r0.get(), out);
    for (auto& p : forest.hash_16) {
        if (p.second) collect_distinct_bitmaps_non_collapsed(p.second.get(), out);
    }
    for (auto& p : forest.hash_64) {
        if (p.second) collect_distinct_bitmaps_non_collapsed(p.second.get(), out);
    }
}

void collect_compress_stats(ForestTrie& forest, CompressResult& out) {
    const int EXTRA_BITS = 64 - 16;  // 256 + (64-16) = 304 for origin
    // 两层 hash（16 与 64）每有一个根节点额外 65bit
    const uint64_t hash_bits = static_cast<uint64_t>(forest.hash_16.size() + forest.hash_64.size()) * 65;

    out.distinct_bitmap_count = 0;

    int pre_nodes = count_nodes_from_forest(forest);
    out.origin_bits = static_cast<uint64_t>(pre_nodes) * (256 + EXTRA_BITS) + hash_bits;
    out.ni_comp_bits = static_cast<uint64_t>(pre_nodes) * 256 + hash_bits;

    // 第3列：压缩前所有节点的 bitmap 去重后，每种取值额外 64bit
    std::set<uint64_t> distinct_bitmaps_pre;
    collect_distinct_bitmaps_from_forest_all(forest, distinct_bitmaps_pre);
    out.ni_comp_bits += static_cast<uint64_t>(distinct_bitmaps_pre.size()) * 64;

    forest.compress();

    int post_nodes = count_nodes_from_forest(forest);
    out.collapsed_bits = static_cast<uint64_t>(post_nodes) * 256 + hash_bits;
    // 第4列：压缩后所有非 collapsed 节点的 bitmap 去重，每种 64bit
    std::set<uint64_t> distinct_bitmaps_post;
    collect_distinct_bitmaps_from_forest_non_collapsed(forest, distinct_bitmaps_post);
    out.collapsed_bits += static_cast<uint64_t>(distinct_bitmaps_post.size()) * 64;
    out.distinct_bitmap_count = distinct_bitmaps_post.size();

    uint64_t half_bits = 0;
    if (forest.r0) half_bits += count_half_size_bits(forest.r0.get());
    for (auto& p : forest.hash_16) {
        if (p.second) half_bits += count_half_size_bits(p.second.get());
    }
    for (auto& p : forest.hash_64) {
        if (p.second) half_bits += count_half_size_bits(p.second.get());
    }
    out.half_size_bits = half_bits + hash_bits + static_cast<uint64_t>(distinct_bitmaps_post.size()) * 64;
}

void write_compress_csv(const std::vector<std::string>& dataset_names,
                        const std::vector<CompressResult>& results,
                        const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << output_path << " for writing" << std::endl;
        return;
    }
    file << "dataset,origin,NI-Comp,Collapsed,Half-Size\n";
    for (size_t i = 0; i < dataset_names.size() && i < results.size(); i++) {
        const auto& r = results[i];
        file << dataset_names[i] << ","
             << r.origin_bits << ","
             << r.ni_comp_bits << ","
             << r.collapsed_bits << ","
             << r.half_size_bits << "\n";
    }
    file.close();
}

void write_bitmap_address_bits_csv(const std::vector<std::string>& dataset_names,
                                   const std::vector<CompressResult>& results,
                                   const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << output_path << " for writing" << std::endl;
        return;
    }
    file << "dataset,distinct_bitmap_count\n";
    for (size_t i = 0; i < dataset_names.size() && i < results.size(); i++) {
        file << dataset_names[i] << "," << results[i].distinct_bitmap_count << "\n";
    }
    file.close();
}
