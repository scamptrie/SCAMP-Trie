#ifndef __BMP_MANAGER_H__
#define __BMP_MANAGER_H__

#include "../scamptrie_base.h"
#include "bmp_entry_block_list.h"
#include "scamptrie_node_block_list.h"

#include <vector>

struct BMPEntry {
    ip6_t ip;
    uint8_t len;
    nexthop_t nh;
    BMPEntry* father;
    BMPEntryBlockList child_list; // Associated Marker List (blocked sorted array)
    ScampTrieNodeBlockList nodes_list;
    BMPEntry(): ip({0, 0}), len(0), nh(0), father(nullptr) {}
};

class BMPManager {
    static const int BMP_ENTRIES_NUM = 1 << 21;
    static const int NODES_NUM = 1 << 20;
public:
    BMPManager();
    ~BMPManager();

    /// @brief Get the nexthop of the bmp entry
    /// @param idx Index of the bmp entry
    /// @return Nexthop
    nexthop_t get_bmp_nexthop(uint32_t idx);

    /// @brief Get the index of the shallowest BMP
    uint32_t shallowest_bmp_idx2(uint32_t i0, uint32_t i1);

    /// @brief Get the index of the shallowest BMP
    uint32_t shallowest_bmp_idx3(uint32_t i0, uint32_t i1, uint32_t i2);

    /// @brief Insert a prefix, update the bmp of the corresponding ENTRIES and NODES
    /// @return Index of the new bmp entry
    /// @note scan all entries & nodes of father_entry, replace some of them with new_entry
    uint32_t insert_prefix(ip6_t ip, uint8_t len128, nexthop_t nh, uint32_t father_idx);

    /// @brief Insert new_node whose father is father_node
    void insert_node(ScampTrieNode* new_node);

    /// @brief Update the ptr of the old_node to the new_node
    /// @note update the corresponding entry.nodes_list & new_node.bmp
    void update_node_ptr(ScampTrieNode* old_node, ScampTrieNode* new_node);

    void debug_infos(uint32_t idx);

private:
    BMPEntry* bmp_entries[BMP_ENTRIES_NUM];
    uint32_t bmp_entries_size;
};

#endif // __BMP_MANAGER_H__