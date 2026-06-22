#ifndef __SCAMPTRIE_HASH_H__
#define __SCAMPTRIE_HASH_H__

#include <functional>
#include <cstdint>

#define HASH_TABLE_SIZE (1024 * 4)

class MemInit;  // Forward declaration

class ScampTrieHash {
    friend class MemInit;
public:
    ScampTrieHash();
    ~ScampTrieHash() = default;

    // get the hash value for key
    bool get64_12(uint64_t key, uint16_t& hash_val);
    // alloc a new entry for key
    bool alloc64_12(uint64_t key);
    bool get_key_by_idx(uint32_t idx, uint64_t& key);

    // 返回当前表中已占用 bucket 的数量
    uint32_t get_used_bucket_count() const;
    // 是否存在至少一个已占用 bucket（用于判断该 hash 表是否“被使用”）
    bool has_any_bucket() const;
private:
    static uint16_t compute_hash_index(uint64_t ip);
    uint64_t bucket_keys[HASH_TABLE_SIZE];
    bool bucket_occupied[HASH_TABLE_SIZE];
    std::hash<uint64_t> hash_fn;
};

#endif // __SCAMPTRIE_HASH_H__