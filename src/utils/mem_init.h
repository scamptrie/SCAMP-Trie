#ifndef __MEM_INIT_H__
#define __MEM_INIT_H__

#include "../scamptrie_base.h"
#include <string>
#include <cstring>
#include <bitset>
#include <fstream>
#include <iomanip>

class ScampTrieDynamic;
class ScampTrieHash;

#ifndef MEM_INIT_DIR
#define MEM_INIT_DIR "./ram_init/"
#endif
#define MEM_INIT_FILE(name) MEM_INIT_DIR name

// Converts ScampTrieDynamic software data to Verilog mem file format
// Can access all private members of ScampTrieDynamic (friend class)
class MemInit {
public:
    MemInit(ScampTrieDynamic* trie);
    ~MemInit();

    // Main export function: export all BRAM data to mem files
    // Exports 4 types of BRAM:
    // 1. BRAM Hash18 & Hash64
    // 2. BRAM Node (11 stages)
    // 3. BRAM Nexthop
    // 4. BRAM Nexthop Bitmap
    void export_mem_files();

private:
    // ========== Initialization and Node Processing ==========
    
    // Initialize and process all nodes in the trie
    void initialize_trie();
    
    // Allocate RAM space for a node and set its mem_addr
    void alloc_ram(uint32_t stage_id, ScampTrieNode* node);

    // Fill RAM with the node's data (after all its children are allocated)
    void fill_ram(ScampTrieNode* node);

    // ========== Nexthop Array Management ==========
    
    // Allocate nexthop array and return hardware address
    uint32_t alloc_nexthop_array(uint32_t nexthop_ptr);

    // ========== BRAM Export Functions ==========
    
    // Export BRAM Node data (multiple stages) to mem files
    // Each entry is 256 bits (one line in mem file)
    void export_bram_node();
    
    // Export BRAM Hash table data to mem file
    // Each entry is 27 bits: [hit(1), node_addr(22), stageID(4)]
    // @param hash_table Pointer to the hash table (hash18 or hash64)
    // @param root_info_array Pointer to the corresponding root info array (root18 or root64)
    // @param filename Output filename
    void export_bram_hash(ScampTrieHash* hash_table, RootInfo** root_info_array, const char* filename);
    
    // Export BRAM Nexthop data to mem file
    // Each entry is 8 bits (packed into 256-bit lines, 32 entries per line)
    void export_bram_nexthop();
    
    // Export BRAM Nexthop Bitmap data to mem file
    // Each entry is 64 bits (packed into 256-bit lines, 4 entries per line)
    void export_bram_nexthop_bitmap();

    // ========== Helper Functions for Packing ==========
    
    // Pack a value into bitset starting from bit_pos (LSB first)
    // bit_pos is passed by reference and will be incremented by num_bits
    // value is masked to num_bits and packed from LSB to MSB
    // Fields are packed sequentially from low bit positions to high bit positions
    // e.g., node_type occupies [2:0] with MSB at bit 2, LSB at bit 0
    void pack_bits(std::bitset<256>& ram_entry, int& bit_pos, uint64_t value, int num_bits);
    
    // Pack ScampTrieNode1 directly into ram (128 bits)
    void pack_node_type1(const ScampTrieNode1* node1, std::bitset<256>& ram_entry);

    // Pack ScampTrieNode2 directly into ram (256 bits)
    void pack_node_type2(const ScampTrieNode2* node2, std::bitset<256>& ram_entry);

    // Pack ScampTrieNode3 directly into ram (256 bits)
    void pack_node_type3(const ScampTrieNode3* node3, std::bitset<256>& ram_entry);

    // Pack ScampTrieNode4 directly into ram (128 bits)
    void pack_node_type4(const ScampTrieNode4* node4, std::bitset<256>& ram_entry);

    // Pack ScampTrieNode5 directly into ram (256 bits)
    void pack_node_type5(const ScampTrieNode5* node5, std::bitset<256>& ram_entry);

    // Convert ChildInfo to 32-bit value
    uint32_t pack_child_info(const ChildInfo& child);
    
    // Pack PrefixInfo fields into bitset (94 bits)
    // Layout: [64-bit ip_chunk][8-bit valid_len][22-bit nexthop]
    void pack_prefix_info_fields(std::bitset<256>& ram_entry, int& bit_pos, const PrefixInfo& prefix);

    // ========== Helper Functions for File Writing ==========
    
    // Write a value as binary string to mem file (for $readmemb)
    // @param file Output file stream
    // @param value Value to write
    // @param bit_width Bit width of the value
    void write_binary_to_file(std::ofstream& file, uint64_t value, int bit_width);
    
    // Write a 256-bit bitset as binary string to mem file
    void write_bitset_binary_to_file(std::ofstream& file, const std::bitset<256>& ram_entry);

private:
    // File paths
    static constexpr const char* file_dir = MEM_INIT_DIR;
    static constexpr const char* file_hash18 = MEM_INIT_FILE("hash18.mem");
    static constexpr const char* file_hash64 = MEM_INIT_FILE("hash64.mem");
    static constexpr const char* file_nexthop = MEM_INIT_FILE("nexthop.mem");
    static constexpr const char* file_nexthop_bitmap = MEM_INIT_FILE("nexthop_bitmap.mem");
    static constexpr const char* file_ram_prefix = MEM_INIT_FILE("node");

    // Node processing queue
    ScampTrieNode* node_queue[1<<22];
    uint32_t node_queue_head, node_queue_tail;

    // BRAM Node data (multiple stages)
    std::bitset<256> ram[RAM_NUM][(10 * MEGA_BYTES) / 256]; // default 10MB / Stage
    uint32_t ram_len[RAM_NUM];

    // BRAM Nexthop Array data
    nexthop_t nexthop_array[200001];
    uint32_t nexthop_array_size;

    // Reference to the trie
    ScampTrieDynamic* trie;
};

#endif // __MEM_INIT_H__