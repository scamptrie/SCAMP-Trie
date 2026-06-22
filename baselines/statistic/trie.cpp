#include "trie.h"
#include <algorithm>

// 计算64位整数的popcount（1的个数）
#ifdef _MSC_VER
#include <intrin.h>
static inline int popcount64(uint64_t x) {
    return __popcnt64(x);
}
#else
static inline int popcount64(uint64_t x) {
    return __builtin_popcountll(x);
}
#endif

TrieNode::TrieNode() 
    : has_prefix(false), collapsed(false), bitmap(0), 
      prefix_count(-1), inverse_distance(0) {  // -1表示未计算
    for (int i = 0; i < MAX_CHILDREN; i++) {
        children[i] = nullptr;
    }
}

uint8_t TrieNode::get_6bit_slice(uint64_t ip_high, uint64_t ip_low, int level) {
    // level从0开始，表示第几层（0-21）
    // 每层提取6位，从128位IP地址的位(level*6)开始
    // ip_high存储位0-63，ip_low存储位64-127
    int bit_offset = level * 6;
    int bits_available = 128 - bit_offset;  // 从bit_offset开始还有多少位可用
    
    // 情况1: 完全在ip_high中（level 0-9）
    if (bit_offset + 6 <= 64) {
        int shift = 64 - bit_offset - 6;
        return (ip_high >> shift) & 0x3F;
    }
    
    // 情况2: 跨越64位边界（level 10）
    if (bit_offset < 64) {
        int bits_in_high = 64 - bit_offset;  // ip_high中的位数
        int bits_in_low = 6 - bits_in_high;  // ip_low中的位数
        
        // 从ip_high提取低bits_in_high位
        uint8_t high_part = ip_high & ((1ULL << bits_in_high) - 1);
        // 从ip_low提取高bits_in_low位
        uint8_t low_part = (ip_low >> (64 - bits_in_low)) & ((1 << bits_in_low) - 1);
        
        // 组合：high_part在高位，low_part在低位
        return (high_part << bits_in_low) | low_part;
    }
    
    // 情况3: 完全在ip_low中，且有足够的6位（level 11-20）
    if (bits_available >= 6) {
        int shift = 64 - (bit_offset - 64) - 6;
        return (ip_low >> shift) & 0x3F;
    }
    
    // 情况4: 完全在ip_low中，但不足6位（level 21，只有2位可用）
    // 需要补0到6位
    int bits_to_extract = bits_available;
    int shift = 64 - (bit_offset - 64) - bits_to_extract;
    uint8_t extracted = (ip_low >> shift) & ((1 << bits_to_extract) - 1);
    // 左移补0，使得结果的高bits_to_extract位是提取的位，低(6-bits_to_extract)位是0
    return extracted << (6 - bits_to_extract);
}

void TrieNode::insert_prefix(const Prefix& prefix, int level) {
    // 计算前缀所在的层数和排数
    int prefix_level = prefix.length / 6;
    
    // 如果前缀就在当前层
    if (prefix_level == level) {
        has_prefix = true;
        update_bitmap(prefix, level);
        return;
    }
    
    // 如果前缀在当前层之后，需要继续向下插入
    if (prefix_level > level) {
        uint8_t slice = get_6bit_slice(prefix.ip_high, prefix.ip_low, level);
        
        if (children[slice] == nullptr) {
            children[slice] = std::make_unique<TrieNode>();
        }
        
        children[slice]->insert_prefix(prefix, level + 1);
    }
}

void TrieNode::update_bitmap(const Prefix& prefix, int level) {
    // 计算前缀所在的层数和排数
    int prefix_level = prefix.length / 6;
    int prefix_row = prefix.length % 6;
    
    // 如果前缀就在当前层
    if (prefix_level == level) {
        // 计算bitmap位置：2^K + 前K位的值
        // K = prefix_row
        uint64_t base = 1ULL << prefix_row;  // 2^K
        
        // 获取该节点的6-bit切片
        uint8_t slice = get_6bit_slice(prefix.ip_high, prefix.ip_low, level);
        
        // 提取前K位
        uint8_t prefix_bits = slice >> (6 - prefix_row);
        
        // 计算位置
        uint64_t position = base + prefix_bits;
        
        // 设置bitmap对应位
        if (position < 64) {
            bitmap |= (1ULL << position);
        }
    }
}

int TrieNode::count_prefixes() {
    // 记忆化：如果已经计算过，直接返回
    if (prefix_count >= 0) {
        return prefix_count;
    }
    
    // 使用树形动态规划：自底向上计算
    // 先递归计算所有子节点的前缀数
    int child_prefix_count = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i] != nullptr) {
            child_prefix_count += children[i]->count_prefixes();
        }
    }
    
    // 当前节点的前缀数 = 当前节点内的前缀数（用bitmap的popcount计算）+ 所有子节点的前缀数
    int node_prefix_count = popcount64(bitmap);
    prefix_count = node_prefix_count + child_prefix_count;
    
    return prefix_count;
}

void TrieNode::compress() {
    // 先计算当前节点内的前缀数（用bitmap的popcount）
    int node_prefix_count = popcount64(bitmap);
    
    // 递归计算所有子节点的前缀数（但不先压缩，只是计算前缀数）
    // 使用count_prefixes()来获取子节点的前缀数，这会利用记忆化避免重复计算
    int child_prefix_count = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i] != nullptr) {
            child_prefix_count += children[i]->count_prefixes();
            // 如果累计前缀数已经超过2，可以提前停止（但为了准确性，还是计算完所有子节点）
        }
    }
    
    // 当前节点的前缀数 = 当前节点内的前缀数 + 所有子节点的前缀数
    int total_prefix_count = node_prefix_count + child_prefix_count;
    
    // 从上至下，碰到第一个子树前缀不超过2的节点就压缩，不继续向下
    // 如果子树内前缀表项不超过2，删除子树但保留节点并标记为collapsed
    if (total_prefix_count <= 2) {
        // 删除所有子节点（unique_ptr会自动释放内存）
        for (int i = 0; i < MAX_CHILDREN; i++) {
            children[i].reset();
        }
        collapsed = true;
    } else {
        // 如果前缀数超过2，继续向下递归压缩子节点
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (children[i] != nullptr) {
                children[i]->compress();
            }
        }
    }
    prefix_count = total_prefix_count;
}

void TrieNode::calculate_inverse_distance() {
    // 计算所有子节点的逆距离，并找到最大值
    int max_child_distance = -1;
    
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i] != nullptr) {
            children[i]->calculate_inverse_distance();
            max_child_distance = std::max(max_child_distance, children[i]->inverse_distance);
        }
    }
    
    // 当前节点的逆距离 = 子节点最大逆距离 + 1
    // 如果没有子节点，max_child_distance = -1，那么 inverse_distance = 0（叶子节点）
    inverse_distance = max_child_distance + 1;
}

int TrieNode::get_child_count() const {
    int count = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i] != nullptr) {
            count++;
        }
    }
    return count;
}

Trie::Trie() {
    root = std::make_unique<TrieNode>();
}

void Trie::insert_prefix(const Prefix& prefix) {
    root->insert_prefix(prefix, 0);
}

void Trie::compress() {
    root->compress();
}

void Trie::calculate_inverse_distance() {
    root->calculate_inverse_distance();
}
