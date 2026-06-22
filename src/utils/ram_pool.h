#ifndef __RAM_POOL_H__
#define __RAM_POOL_H__

#include "../scamptrie_base.h"
#include "../moo/moo_weights.h"

class ScampTrieNode;

#define RAM_POOL_OK 0
#define RAM_POOL_FAIL UINT32_MAX

class RAMPool {
public:
    RAMPool(bool enable_hash);
    ~RAMPool();

    /**
     * Set total memory size for all stages
     * @param total_ram total memory size in bytes
     * @param weights array of weights for each stage
     */
    void set_ram_allocation(uint64_t total_ram, const MOOWeights& weights);
    
    /**
     * Allocates a continuous segment of length length
     * @param len the length of the segment
     * @param node_type the type of node in the segment
     * @param lowest_stage the lowest stage of the segment [lowest_stage, STAGE_NUM - depth)
     * @param depth the highest stage of the node (highest_stage - depth)
     * @param ret_ptr the pointer to the segment
     * @param ret_stage the stage of the segment
     * @return SCAMPTRIE_OK if successful, SCAMPTRIE_FAIL if failed
     */
    uint32_t alloc(
        uint32_t len, ScampNodeType node_type,
        stage_t lowest_stage, stage_t depth,
        uint32_t &ret_ptr, stage_t &ret_stage
    );
    
    /**
     * get the value at the specified position
     * @param ptr the pointer to the segment
     * @param offset the offset of the node in the segment
     * @return The pointer to the ScampTrieNode, nullptr if out of range
     */
    ScampTrieNode* get(uint32_t ptr, uint32_t offset = 0);

    /**
     * @brief get the length of the region at the specified position
     * @param ptr the pointer to the segment
     * @return The length of the region
     */
    uint32_t get_region_length(uint32_t ptr);

    /**
     * set the value at the specified position (NO reallocation)
     * @param ptr the pointer to the segment
     * @param offset the offset of the node in the segment
     * @param node the node to set
     */
    void set(const uint32_t ptr, uint32_t offset, ScampTrieNode* node);
    
    /**
     * Inserts at specified position (May trigger reallocation, ptr will change correspondingly)
     * @param ptr the pointer to the segment
     * @param final_stage the final stage of the segment (after stage adjustment)
     * @param offset the offset of the node in the segment
     * @param node the node to insert
     * @param lowest_stage the lowest stage of the segment [lowest_stage, STAGE_NUM - child_depth)
     * @param child_depth the depth of the child node
     * @return SCAMPTRIE_OK if successful, SCAMPTRIE_FAIL if failed
     */
    uint32_t insert(
        uint32_t &ptr, stage_t &final_stage,
        uint32_t offset, ScampTrieNode* node,
        stage_t lowest_stage, stage_t child_depth
    );
    
    /**
     * @brief adjust the stage of the segment, ptr & final_stage will be changed correspondingly
     * @param ptr the pointer to the segment
     * @param final_stage the final stage of the segment (after stage adjustment)
     * @param lowest_stage the lowest stage of the segment [lowest_stage, STAGE_NUM - child_depth)
     * @param child_depth the depth of the child node
     * @param node_type the type of node in the segment
     * @return SCAMPTRIE_OK if successful, SCAMPTRIE_FAIL if failed
     */
    uint32_t adjust_stage(
        uint32_t &ptr, stage_t &final_stage,
        stage_t lowest_stage, stage_t child_depth, ScampNodeType node_type
    );

    /**
     * Free the region starting at start_pos (DO NOT free the nodes)
     * @param start_pos the pointer to the segment
     * @param stage the stage of the segment
     * @return True if successful
     */
    void free(uint32_t start_pos, stage_t stage);

    /**
     * @brief print the ram info (total and remain)
     * @return the ram used (Bytes)
     */
    void print_ram_info();

    /**
     * @brief get the ram info(used and allocated)
     * @param ram_used the ram used (Bytes)
     * @param ram_allocated the ram allocated (Bytes)
     */
    void get_ram_info(uint64_t &ram_used, uint64_t &ram_allocated);

    /**
     * @brief get per-stage RAM usage for pipeline stages
     * @param ram_used_per_stage array of size RAM_NUM, each entry is used bytes
     */
    void get_stage_usage(uint64_t ram_used_per_stage[RAM_NUM]);

    /**
     * @brief get per-stage RAM allocation for pipeline stages (bytes)
     * @param ram_allocated_per_stage array of size RAM_NUM
     */
    void get_stage_allocated(uint64_t ram_allocated_per_stage[RAM_NUM]);

    /**
     * @brief get per-stage node type counts for pipeline stages
     * @param counts [RAM_NUM][7]: [stage][0..4]=Type3,Type2,Type1,Type5,Type4; [stage][5]=Total; [stage][6]=nodes in stage whose parent is Hybrid(Type3)
     */
    void get_stage_type_counts(uint32_t counts[RAM_NUM][7]);

private:
    /**
    * @brief scan for space of the specified size
    * @param alloc_size the size of the space to scan
    * @param lowest_stage the lowest stage of the scan [lowest_stage, highest_stage)
    * @param highest_stage the highest stage of the scan [lowest_stage, highest_stage)
    * @return the stage of the space, STAGE_DEFAULT if not found
    */
    stage_t scan_for_space(uint32_t alloc_size, stage_t lowest_stage, stage_t highest_stage);

    /**
     * find the continuous space of length length
     * @param len the length of the space to find
     * @param node_type the type of node in the space
     * @return the start position, if not found return UINT32_MAX
     */
    uint32_t find_continuous_space(uint32_t len, ScampNodeType node_type);

private:
    static const uint32_t BUCKET_SIZE = 1 << 22;
    bool enable_hash;
    int Bound;

    uint32_t bucket_size;
    bool allocated[BUCKET_SIZE];
    uint32_t item_sizes[BUCKET_SIZE];
    ScampTrieNode* nodes[BUCKET_SIZE];
    ScampTrieNode* buffer_nodes[BUCKET_SIZE];
    uint32_t region_lengths[BUCKET_SIZE];  // Total allocated length for each region

    bool ALLOC_METHOD; // 0: static, 1: dynamic
    uint64_t ram_total[STAGE_NUM];
    uint64_t ram_remain[STAGE_NUM];
    double ram_ratio[STAGE_NUM];

    /** Next-fit hint: start next find_continuous_space from here to reduce linear scan. */
    uint32_t next_fit_start;
};

#endif // __RAM_POOL_H__
