#ifndef __NODE_STATIC_BUCKET_H__
#define __NODE_STATIC_BUCKET_H__

#include "../scamptrie_base.h"

class ScampTrieNode;

#define TRIE_NODE_BUCKET_FAIL UINT32_MAX

class NodeStaticBucket {
    static const uint32_t BUCKET_SIZE = 1 << 22;
public:
    NodeStaticBucket();
    ~NodeStaticBucket();
    
    /**
     * Allocates a continuous segment of length length
     * @param length
     * @return Start position of the allocated segment, UINT32_MAX if allocation fails
     */
    uint32_t alloc(uint32_t length);
    
    /**
     * get the value at the specified position
     * @param ptr
     * @param offset
     * @return The pointer to the ScampTrieNode, nullptr if out of range
     */
    ScampTrieNode* get(uint32_t ptr, uint32_t offset = 0);

    /**
     * @brief get the length of the region at the specified position
     * @param ptr
     * @return The length of the region
     */
    uint32_t get_region_length(uint32_t ptr);

    /**
     * set the value at the specified position (NO reallocation)
     * @param ptr
     * @param offset
     * @param node
     */
    void set(const uint32_t ptr, uint32_t offset, ScampTrieNode* node);
    
    /**
     * Inserts at specified position (May trigger reallocation, ptr will change correspondingly)
     * @param ptr
     * @param offset
     * @param node
     */
    void insert(uint32_t &ptr, uint32_t offset, ScampTrieNode* node);
    
    /**
     * Free the region starting at start_pos (DO NOT free the nodes)
     * @param start_pos
     * @return True if successful
     */
    void free(uint32_t start_pos);
    
private:
    /**
     * find the continuous space of length length
     * @param length
     * @return the start position, if not found return UINT32_MAX
     */
    uint32_t find_continuous_space(uint32_t length);

private:
    bool allocated[BUCKET_SIZE];
    ScampTrieNode* nodes[BUCKET_SIZE];
    uint32_t region_lengths[BUCKET_SIZE];  // Total allocated length for each region
};

#endif // __NODE_STATIC_BUCKET_H__
