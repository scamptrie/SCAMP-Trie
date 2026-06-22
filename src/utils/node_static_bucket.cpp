#include "node_static_bucket.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

NodeStaticBucket::NodeStaticBucket() {
    memset(nodes, 0, sizeof(nodes));
    memset(allocated, 0, sizeof(allocated));
    memset(region_lengths, 0, sizeof(region_lengths));
}

NodeStaticBucket::~NodeStaticBucket() {
    for (uint32_t i = 0; i < BUCKET_SIZE; ++i)
        if (nodes[i] != nullptr)
            delete nodes[i];
}

uint32_t NodeStaticBucket::alloc(uint32_t length) {
    if (length == 0 || length > BUCKET_SIZE)
        return TRIE_NODE_BUCKET_FAIL;
    
    uint32_t start_pos = find_continuous_space(length);
    if (start_pos == TRIE_NODE_BUCKET_FAIL)
        return TRIE_NODE_BUCKET_FAIL;
    return start_pos;
}

ScampTrieNode* NodeStaticBucket::get(uint32_t base_pos, uint32_t offset) {
    uint32_t pos = base_pos + offset;
    if (pos >= BUCKET_SIZE || !allocated[pos])
        return nullptr;
    
    ScampTrieNode* node = nodes[pos];
    if (node != nullptr && node->nodeType > 5) {
        printf("[ERROR] NodeStaticBucket::get - Invalid nodeType: %d at position %d\n", node->nodeType, pos);
        printf("[ERROR] Node address: %p\n", (void*)node);
        printf("[ERROR] Node memory dump (first 16 bytes): ");
        uint8_t* node_bytes = (uint8_t*)node;
        for (int k = 0; k < 16; ++k) {
            printf("%02X ", node_bytes[k]);
        }
        printf("\n");
        exit(1);
    }
    return node;
}

uint32_t NodeStaticBucket::get_region_length(uint32_t ptr) {
    if (ptr >= BUCKET_SIZE || !allocated[ptr])
        return 0;
    return region_lengths[ptr];
}

void NodeStaticBucket::set(const uint32_t ptr, uint32_t offset, ScampTrieNode* node) {
    if (node == nullptr)
        msg_error("NodeStaticBucket::set: node is nullptr");
    if (ptr + offset >= BUCKET_SIZE || !allocated[ptr]) {
        msg_error("NodeStaticBucket::set: ptr+offset out of range or region not allocated");
        return;
    }
    uint32_t region_len = region_lengths[ptr];
    if (offset >= region_len) {
        msg_error("NodeStaticBucket::set: offset out of allocated range");
        return;
    }
    if (!allocated[ptr + offset]) {
        msg_error("NodeStaticBucket::set: ptr + offset not allocated");
        return;
    }
    
    if (node != nullptr && node->nodeType > 5) {
        printf("[ERROR] TrieNodeBucket::set - Invalid nodeType: %d at position %d\n", node->nodeType, ptr + offset);
        printf("[ERROR] Node address: %p\n", (void*)node);
        printf("[ERROR] Node memory dump (first 16 bytes): ");
        uint8_t* node_bytes = (uint8_t*)node;
        for (int k = 0; k < 16; ++k) {
            printf("%02X ", node_bytes[k]);
        }
        printf("\n");
        exit(1);
    }
    
    nodes[ptr + offset] = node;
}

void NodeStaticBucket::insert(uint32_t &ptr, uint32_t offset, ScampTrieNode* node) {
    if (ptr >= BUCKET_SIZE || !allocated[ptr])
        msg_error("TrieNodeBucket::insert: ptr out of range or region not allocated");
    uint32_t len = region_lengths[ptr];
    if (offset > len) {
        printf("TrieNodeBucket::insert: offset out of allocated range, ptr: %d, offset: %d, len: %d\n", ptr, offset, len);
        return;
    }
    // expand the region
    if (ptr + len < BUCKET_SIZE && !allocated[ptr + len]) {
        allocated[ptr + len] = true;
        region_lengths[ptr] = len + 1;
        len++;
    } else {
        uint32_t new_len = len + 1;
        uint32_t new_ptr = alloc(new_len);
        if (new_ptr == TRIE_NODE_BUCKET_FAIL) { 
            msg_error("TrieNodeBucket::insert: failed to allocate new space");
            return;
        }

        // Copy existing elements
        for (uint32_t i = 0; i < len; ++i)
            nodes[new_ptr + i] = nodes[ptr + i];

        free(ptr);
        ptr = new_ptr;
        len = new_len;
    }
    
    for (int i = len - 1; i > (int)offset; --i)
        nodes[ptr + i] = nodes[ptr + i - 1];
    
    if (ptr + offset >= BUCKET_SIZE)
        msg_error("TrieNodeBucket::insert: Insert position out of bounds");
    nodes[ptr + offset] = node;
}

void NodeStaticBucket::free(uint32_t start_pos) {
    if (start_pos + region_lengths[start_pos] > BUCKET_SIZE) {
        msg_error("TrieNodeBucket::free: start_pos + region_lengths[start_pos] out of range or region not allocated");
        return;
    }
    uint32_t region_len = region_lengths[start_pos];
    for (uint32_t i = start_pos; i < start_pos + region_len; ++i) {
        nodes[i] = nullptr;
        allocated[i] = false;
        region_lengths[i] = 0;
    }
}

uint32_t NodeStaticBucket::find_continuous_space(uint32_t length) {
    for (uint32_t i = 0; i < BUCKET_SIZE; ) {
        while (i < BUCKET_SIZE && allocated[i]) i++;
        
        uint32_t start_pos = i;
        while (i < BUCKET_SIZE && !allocated[i]) {
            if (i - start_pos + 1 >= length) {
                // Mark all positions in the region as allocated
                for (uint32_t j = 0; j < length; ++j) {
                    allocated[start_pos + j] = true;
                }
                // Only the first position stores the total region length
                region_lengths[start_pos] = length;
                return start_pos;
            }
            i++;
        }
    }
    return TRIE_NODE_BUCKET_FAIL;
}
