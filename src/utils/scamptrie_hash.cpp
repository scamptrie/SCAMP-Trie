#include "scamptrie_hash.h"

#include <cstring>
#include <cstdio>

ScampTrieHash::ScampTrieHash() {
    memset(bucket_occupied, 0, sizeof(bucket_occupied));
}

// FPGA友好的hash函数实现
// 等同于Verilog逻辑：
// wire [63:0] mix1 = ip ^ {ip[15:0], ip[63:16]}; // 左旋16位再异或
// wire [63:0] mix2 = mix1 ^ {mix1[31:0], mix1[63:32]}; // 左旋32位再异或
// assign hash1_index = mix2[11:0] ^ mix2[23:12] ^ mix2[35:24] ^ mix2[47:36] ^ mix2[59:48];
uint16_t ScampTrieHash::compute_hash_index(uint64_t ip) {
    // mix1 = ip ^ {ip[15:0], ip[63:16]} - 左旋16位再异或
    uint64_t rotated_16 = (ip << 16) | (ip >> 48);
    uint64_t mix1 = ip ^ rotated_16;
    
    // mix2 = mix1 ^ {ip[31:0], ip[63:32]} - 左旋32位再异或
    uint64_t rotated_32 = (mix1 << 32) | (mix1 >> 32);
    uint64_t mix2 = mix1 ^ rotated_32;
    
    // hash1_index = mix2[11:0] ^ mix2[23:12] ^ mix2[35:24] ^ mix2[47:36] ^ mix2[59:48]
    uint64_t hash_index = (mix2 & 0xFFF) ^                    // mix2[11:0]
                          ((mix2 >> 12) & 0xFFF) ^            // mix2[23:12]
                          ((mix2 >> 24) & 0xFFF) ^            // mix2[35:24]
                          ((mix2 >> 36) & 0xFFF) ^            // mix2[47:36]
                          ((mix2 >> 48) & 0xFFF);             // mix2[59:48]
    
    // printf("Hash: %016llX -> %d\n", ip, (uint16_t)(hash_index % HASH_TABLE_SIZE));
    return hash_index % HASH_TABLE_SIZE;
}

bool ScampTrieHash::get64_12(uint64_t key, uint16_t& hash_val) {
    // size_t idx = compute_hash_index(key);
    size_t idx = hash_fn(key) % HASH_TABLE_SIZE;
    size_t start_idx = idx;
    while (bucket_occupied[idx]) {
        if (bucket_keys[idx] == key) {
            hash_val = idx;
            return true;
        }
        idx = (idx + 1) % HASH_TABLE_SIZE;
        if (idx == start_idx)
            return false;
    }
    return false;
}

bool ScampTrieHash::alloc64_12(uint64_t key) {
    // size_t idx = compute_hash_index(key);
    size_t idx = hash_fn(key) % HASH_TABLE_SIZE;
    size_t start_idx = idx;
    while (bucket_occupied[idx]) {
        if (bucket_keys[idx] == key) {
            return true;
        }
        // printf("############# HASH INTERRUPTED, key: %016llX\n", key);
        idx = (idx + 1) % HASH_TABLE_SIZE;
        if (idx == start_idx) {
            printf("Hash::alloc64_12 failed\n");
            return false;
        }
    }
    bucket_keys[idx] = key;
    bucket_occupied[idx] = true;
    return true;
}

bool ScampTrieHash::get_key_by_idx(uint32_t idx, uint64_t& key) {
    if (idx >= HASH_TABLE_SIZE)
        return false;
    if (!bucket_occupied[idx])
        return false;
    key = bucket_keys[idx];
    return true;
}

uint32_t ScampTrieHash::get_used_bucket_count() const {
    uint32_t cnt = 0;
    for (size_t i = 0; i < HASH_TABLE_SIZE; ++i) {
        if (bucket_occupied[i])
            ++cnt;
    }
    return cnt;
}

bool ScampTrieHash::has_any_bucket() const {
    for (size_t i = 0; i < HASH_TABLE_SIZE; ++i) {
        if (bucket_occupied[i])
            return true;
    }
    return false;
}