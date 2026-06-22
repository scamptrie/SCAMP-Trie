#ifndef COMPRESS_TRIE_H
#define COMPRESS_TRIE_H

#include "fib_parser.h"
#include "trie.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

/*
 * 实现策略（压缩策略对Trie存储规模统计）：
 * 1. 森林Trie结构：r0根 + 16位hash层 + 64位hash层。前缀按有效长度len分流：
 *    len<=16 -> r0；16<len<=64 -> hash16(前16位)根；len>64 -> hash64(前64位)根。
 * 2. 子前缀插入：确定根后，用 extract_sub_prefix 得到“剩余位”对应的子前缀，在对应根上
 *    用原有 TrieNode::insert_prefix(sub, 0) 插入。子树有效长度最多64位，层数最多11。
 * 3. 统计口径：
 *    - origin：压缩前节点数 * (256 + (64-16)) = 节点数 * 304 (bit)
 *    - NI-Comp：压缩前节点数 * 256 (bit)
 *    - Collapsed：压缩后节点数 * 256 (bit)
 *    - Half-Size：压缩后逐节点计：collapsed且子树前缀数<=1 或 非collapsed且直接儿子数<=2 记128bit，否则256bit。
 * 4. 输出：res_compress.csv，与其他结果同目录，首行策略名，后续每行一个数据集、5列（数据集名+4个策略bit数）。
 */

// 从 prefix 的 [start_bit, start_bit+effective_len) 提取子前缀，作为新的 128 位前缀（高 effective_len 位有效）
Prefix extract_sub_prefix(const Prefix& prefix, int start_bit, int effective_len);

// 森林 Trie：r0 + hash@16 + hash@64，节点复用 TrieNode
class ForestTrie {
public:
    static const int HASH_BITS_16 = 16;
    static const int HASH_BITS_64 = 64;
    static const int MAX_SUBTREE_LEVEL = 11;  // 64/6 向上

    std::unique_ptr<TrieNode> r0;
    std::map<uint16_t, std::unique_ptr<TrieNode>> hash_16;
    std::map<uint64_t, std::unique_ptr<TrieNode>> hash_64;

    ForestTrie();

    void insert_prefix(const Prefix& prefix);
    void compress();
};

// 单数据集的压缩统计结果（4 列 bit + 压缩后不同 bitmap 种类数）
struct CompressResult {
    uint64_t origin_bits;           // 原始规格
    uint64_t ni_comp_bits;         // NI 压缩
    uint64_t collapsed_bits;       // 稀疏子树压缩
    uint64_t half_size_bits;       // Half-Size 压缩
    size_t distinct_bitmap_count;  // 压缩后森林内部不同 bitmap 种类数
};

// 对森林 Trie 做一次完整统计（先不压缩数节点，再压缩，再数节点与 Half-Size）
void collect_compress_stats(ForestTrie& forest, CompressResult& out);

// 将多数据集结果写入 res_compress.csv（与其它结果同目录）
void write_compress_csv(const std::vector<std::string>& dataset_names,
                        const std::vector<CompressResult>& results,
                        const std::string& output_path);

// 将压缩后各数据集的 bitmap 种类数写入 res_bitmap_address_bits.csv（顺序与其他结果相同）
void write_bitmap_address_bits_csv(const std::vector<std::string>& dataset_names,
                                   const std::vector<CompressResult>& results,
                                   const std::string& output_path);

#endif // COMPRESS_TRIE_H
