#ifndef TRIE_H
#define TRIE_H

#include <cstdint>
#include <vector>
#include <memory>
#include "fib_parser.h"

class TrieNode {
public:
    static const int MAX_CHILDREN = 64;  // 2^6
    
    std::unique_ptr<TrieNode> children[MAX_CHILDREN];
    bool has_prefix;           // 该节点是否拥有前缀表项
    bool collapsed;            // 是否被标记为collapsed
    uint64_t bitmap;           // 63-bit结构bitmap（用于结构相似度统计）
    int prefix_count;          // 子树内前缀表项总数（-1表示未计算）
    int inverse_distance;      // 逆距离
    
    TrieNode();
    
    // 获取6-bit切片（从128位IP中提取第level层的6位）
    static uint8_t get_6bit_slice(uint64_t ip_high, uint64_t ip_low, int level);
    
    // 插入前缀
    void insert_prefix(const Prefix& prefix, int level);
    
    // 计算子树内前缀表项总数
    int count_prefixes();
    
    // 压缩：删除子树内前缀表项不超过2的子树
    void compress();
    
    // 计算逆距离
    void calculate_inverse_distance();
    
    // 获取子节点数量
    int get_child_count() const;
    
    // 更新bitmap（用于结构相似度统计）
    void update_bitmap(const Prefix& prefix, int level);
};

class Trie {
public:
    static const int MAX_LEVEL = 22;  // 128/6向上取整
    
    std::unique_ptr<TrieNode> root;
    
    Trie();
    
    void insert_prefix(const Prefix& prefix);
    void compress();
    void calculate_inverse_distance();
};

#endif // TRIE_H
