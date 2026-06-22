#include "../scamptrie_dynamic.h"
#include "mem_init.h"

// Debug flags for different modules
static bool M_INIT_TAG = 1;   // Initialization
static bool M_FILL_TAG = 0;   // RAM filling
static bool M_PACK_TAG = 0;   // Node packing
static bool M_EXPORT_TAG = 1; // File export

MemInit::MemInit(ScampTrieDynamic* trie) {
    if (M_INIT_TAG) printf("[M_INIT] MemInit constructor start\n");
    // Initialize
    this->trie = trie;
    memset(ram, 0, sizeof(ram));
    memset(ram_len, 0, sizeof(ram_len));
    node_queue_head = node_queue_tail = 0;
    nexthop_array_size = 0;
    
    if (M_INIT_TAG) printf("[M_INIT] Calling initialize_trie\n");
    // Initialize and process the trie
    initialize_trie();
    if (M_EXPORT_TAG) printf("[M_INIT] Calling export_mem_files\n");
    export_mem_files();
    printf("MemInit constructor end\n");
}

MemInit::~MemInit() {
}

// ========== Initialization and Node Processing ==========

void MemInit::initialize_trie() {
    if (M_INIT_TAG) printf("[M_INIT] initialize_trie start\n");
    // Step 1: Initialize node queue with all roots
    
    // The only root at layer 0
    if (M_INIT_TAG) printf("[M_INIT] Processing root at layer 0\n");
    RootInfo* root_info = trie->root;
    if (M_INIT_TAG) printf("[M_INIT] root_info=%p\n", (void*)root_info);
    if (root_info == nullptr) {
        if (M_INIT_TAG) printf("[M_INIT] ERROR: root is nullptr\n");
        return;
    }
    ScampTrieNode* node = trie->ram_pool->get(root_info->rootPointer);
    if (M_INIT_TAG) printf("[M_INIT] node=%p, rootPointer=%d, stageID=%d\n", 
                           (void*)node, root_info->rootPointer, root_info->stageID);
    alloc_ram(root_info->stageID, node);
    node_queue[node_queue_tail++] = node;

    // 4096 roots at layer FIRST_LEVEL_BITS
    if (M_INIT_TAG) printf("[M_INIT] Processing roots at layer %d\n", FIRST_LEVEL_BITS);
    for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
        root_info = trie->root18[i];
        if (root_info == nullptr) continue;
        node = trie->ram_pool->get(root_info->rootPointer);
        if (M_INIT_TAG && i < 5) printf("[M_INIT] root18[%d]: node=%p, rootPointer=%d\n", 
                                        i, (void*)node, root_info->rootPointer);
        alloc_ram(root_info->stageID, node);
        node_queue[node_queue_tail++] = node;
    }

    // 4096 roots at layer 64
    if (M_INIT_TAG) printf("[M_INIT] Processing roots at layer 64\n");
    for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
        root_info = trie->root64[i];
        if (root_info == nullptr) continue;
        node = trie->ram_pool->get(root_info->rootPointer);
        if (M_INIT_TAG && i < 5) printf("[M_INIT] root64[%d]: node=%p, rootPointer=%d\n", 
                                        i, (void*)node, root_info->rootPointer);
        alloc_ram(root_info->stageID, node);
        node_queue[node_queue_tail++] = node;
    }

    // Step 2: Scan the whole Trie, Allocate BRAM Node, BRAM NH Bitmap
    // (BRAM Nexthop will be reallocated in Packing Function)
    if (M_INIT_TAG) printf("[M_INIT] Starting node queue processing, queue_size=%d\n", node_queue_tail);
    int processed_count = 0;
    while (node_queue_head != node_queue_tail) {
        ScampTrieNode* node = node_queue[node_queue_head++];
        processed_count++;
        if (M_INIT_TAG && processed_count % 10000 == 0) {
            printf("[M_INIT] Processed %d nodes, queue_head=%d, queue_tail=%d\n", 
                   processed_count, node_queue_head, node_queue_tail);
        }
        
        if (node == nullptr) {
            if (M_INIT_TAG) printf("[M_INIT] ERROR: node is nullptr at queue_head=%d\n", node_queue_head - 1);
            continue;
        }
        if (M_INIT_TAG && processed_count < 10) {
            printf("[M_INIT] Processing node: nodeType=%d, cur_stage=%d\n", node->nodeType, node->cur_stage);
        }
        if (node->nodeType == TYPE1) {
            ScampTrieNode1* node1 = static_cast<ScampTrieNode1*> (node);
            for (int i = 0; i < 2; ++i)
                if (node1->childInfo[i].stageID != STAGE_NONE) {
                    ScampTrieNode* ch_node = trie->ram_pool->get(node1->childInfo[i].childPointer);
                    if (M_INIT_TAG && processed_count < 10) {
                        printf("[M_INIT] TYPE1 child[%d]: ch_node=%p, childPointer=%d\n", 
                               i, (void*)ch_node, node1->childInfo[i].childPointer);
                    }
                    alloc_ram(node1->childInfo[i].stageID, ch_node);
                    node_queue[node_queue_tail++] = ch_node;
                }
            fill_ram(node);
        } else if (node->nodeType == TYPE2) {
            ScampTrieNode2* node2 = static_cast<ScampTrieNode2*>(node);
            for (int i = 0; i < 6; ++i)
                if (node2->childInfo[i].stageID != STAGE_NONE) {
                    ScampTrieNode* ch_node = trie->ram_pool->get(node2->childInfo[i].childPointer);
                    if (M_INIT_TAG && processed_count < 10) {
                        printf("[M_INIT] TYPE2 child[%d]: ch_node=%p, childPointer=%d\n", 
                               i, (void*)ch_node, node2->childInfo[i].childPointer);
                    }
                    alloc_ram(node2->childInfo[i].stageID, ch_node);
                    node_queue[node_queue_tail++] = ch_node;
                }
            fill_ram(node);
        } else if (node->nodeType == TYPE3) {
            ScampTrieNode3* node3 = static_cast<ScampTrieNode3*>(node);
            for (int i = 0; i < 3; ++i)
                if (node3->freeChild[i].stageID != STAGE_NONE) {
                    ScampTrieNode* ch_node = trie->ram_pool->get(node3->freeChild[i].childPointer);
                    if (M_INIT_TAG && processed_count < 10) {
                        printf("[M_INIT] TYPE3 freeChild[%d]: ch_node=%p, childPointer=%d\n", 
                               i, (void*)ch_node, node3->freeChild[i].childPointer);
                    }
                    alloc_ram(node3->freeChild[i].stageID, ch_node);
                    node_queue[node_queue_tail++] = ch_node;
                }
            uint8_t child_cnt = count_ones_before_pos(node3->childBitmap, 64);
            uint32_t mem_addr = ram_len[node3->childStage];
            ram_len[node3->childStage] += child_cnt * 2; // every child takes 256 bits(2 in index)
            for (int j = 0; j < child_cnt; ++j) {
                ScampTrieNode* ch_node = trie->ram_pool->get(node3->childPointer, j);
                ch_node->mem_addr = mem_addr + j * 2;
                node_queue[node_queue_tail++] = ch_node;
            }
            fill_ram(node);
        } else if (node->nodeType == TYPE4) {
            fill_ram(node);
        } else if (node->nodeType == TYPE5) {
            fill_ram(node);
        }
    }
    if (M_INIT_TAG) printf("[M_INIT] initialize_trie end, total_processed=%d\n", processed_count);
}

void MemInit::alloc_ram(uint32_t stage_id, ScampTrieNode* node) {
    if (stage_id < 0 || stage_id >= RAM_NUM)
        msg_error("MemInit::alloc_ram: stage_id out of range");

    // TODO: treat TYPE 1,4 specially
    node->mem_addr = ram_len[stage_id];
    ram_len[stage_id] += 2;
}

// Helper function: Pack a value into bitset starting from bit_pos (LSB first)
// bit_pos starts from 0 and increments, so the value's LSB goes to bit_pos, MSB goes to bit_pos+num_bits-1
void MemInit::pack_bits(std::bitset<256>& ram_entry, int& bit_pos, uint64_t value, int num_bits) {
    // Mask value to num_bits
    uint64_t mask = (num_bits == 64) ? UINT64_MAX : ((1ULL << num_bits) - 1);
    value &= mask;
    
    // Pack from LSB to MSB (bit_pos is the starting position, incrementing)
    for (int i = 0; i < num_bits; ++i) {
        ram_entry.set(bit_pos++, (value >> i) & 1);
    }
}

// Convert ChildInfo to 32-bit value
// Layout (Verilog [31:0]): [22-bit node_addr][4-bit stage_id][6-bit ip_chunk]
// In Verilog [255:0] notation: node_addr at highest bits (left), ip_chunk at lowest bits (right)
// When packed by pack_bits (from bit_pos=0), LSB goes to lower bit positions
// So node_addr should be at [31:10] (MSB side), ip_chunk at [5:0] (LSB side)
uint32_t MemInit::pack_child_info(const ChildInfo& child) {
    if (M_PACK_TAG) printf("[M_PACK] pack_child_info: ip_sub6=%d, stageID=%d, childPointer=%d\n", 
                           child.ip_sub6, child.stageID, child.childPointer);
    uint32_t result = 0;
    
    if (child.stageID != STAGE_NONE) { // the child may be invalid
        // use Hardware Address instead of Software Pointer
        ScampTrieNode* child_node = trie->ram_pool->get(child.childPointer);
        if (M_PACK_TAG) printf("[M_PACK] child_node=%p, mem_addr=%d\n", (void*)child_node, child_node ? child_node->mem_addr : 0);
        result |= (child_node->mem_addr & 0x3FFFFF) << 10;   // 22 bits at [31:10]
    }
    result |= (child.stageID & 0x0F) << 6;      // 4 bits at [9:6]
    result |= (child.ip_sub6 & 0x3F);           // 6 bits at [5:0]
    return result;
}

// Pack ScampTrieNode1 directly into ram (128 bits)
// Layout according to Verilog struct:
// [3-bit node_type][22-bit bmp][16-bit nexthop_bitmap_ptr][22-bit nexthop_ptr]
// [32-bit child_info[0]][32-bit child_info[1]][1-bit reserved]
// The 128 bits are stored at ram_entry[127:0] (lower half)
// node_type occupies [2:0], with MSB at bit 2, LSB at bit 0
void MemInit::pack_node_type1(const ScampTrieNode1* node1, std::bitset<256>& ram_entry) {
    int bit_pos = 0;  // Start from LSB (bit 0 of the 128-bit value, which is ram_entry[0])
    uint16_t real_bmp = trie->bmp_manager->get_bmp_nexthop(node1->bmp);
    uint32_t real_nexthop_ptr = alloc_nexthop_array(node1->nhPointer);
    
    pack_bits(ram_entry, bit_pos, node1->nodeType, 3);              // node_type: 3 bits [2:0]
    pack_bits(ram_entry, bit_pos, real_bmp, 22);                    // bmp: 22 bits [24:3]
    pack_bits(ram_entry, bit_pos, node1->nhBitmapPointer, 16);      // nexthop_bitmap_ptr: 16 bits [40:25]
    pack_bits(ram_entry, bit_pos, real_nexthop_ptr, 22);            // nexthop_ptr: 22 bits [62:41]
    pack_bits(ram_entry, bit_pos, pack_child_info(node1->childInfo[0]), 32);  // child_info[0]: 32 bits [94:63]
    pack_bits(ram_entry, bit_pos, pack_child_info(node1->childInfo[1]), 32);  // child_info[1]: 32 bits [126:95]
    pack_bits(ram_entry, bit_pos, node1->reserved, 1);              // reserved: 1 bit [127:127]
}


// Pack ScampTrieNode2 directly into ram (256 bits)
// Layout according to Verilog struct
// [3-bit node_type][22-bit bmp][16-bit nexthop_bitmap_ptr][22-bit nexthop_ptr]
// [32-bit child_info[0]][32-bit child_info[1]][32-bit child_info[2]]
// [32-bit child_info[3]][32-bit child_info[4]][32-bit child_info[5]][1-bit reserved]
// The 256 bits are stored at ram_entry[255:0]
// node_type occupies [2:0], with MSB at bit 2, LSB at bit 0
void MemInit::pack_node_type2(const ScampTrieNode2* node2, std::bitset<256>& ram_entry) {
    int bit_pos = 0;  // Start from LSB (bit 0 of the 256-bit value, which is ram_entry[0])
    uint16_t real_bmp = trie->bmp_manager->get_bmp_nexthop(node2->bmp);
    uint32_t real_nexthop_ptr = alloc_nexthop_array(node2->nhPointer);
    
    pack_bits(ram_entry, bit_pos, node2->nodeType, 3);              // node_type: 3 bits [2:0]
    pack_bits(ram_entry, bit_pos, real_bmp, 22);                    // bmp: 22 bits [24:3]
    pack_bits(ram_entry, bit_pos, node2->nhBitmapPointer, 16);      // nexthop_bitmap_ptr: 16 bits [40:25]
    pack_bits(ram_entry, bit_pos, real_nexthop_ptr, 22);            // nexthop_ptr: 22 bits [62:41]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[0]), 32);  // child_info[0]: 32 bits [94:63]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[1]), 32);  // child_info[1]: 32 bits [126:95]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[2]), 32);  // child_info[2]: 32 bits [158:127]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[3]), 32);  // child_info[3]: 32 bits [190:159]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[4]), 32);  // child_info[4]: 32 bits [222:191]
    pack_bits(ram_entry, bit_pos, pack_child_info(node2->childInfo[5]), 32);  // child_info[5]: 32 bits [254:223]
    pack_bits(ram_entry, bit_pos, node2->reserved, 1);              // reserved: 1 bit [255:255]
}

// Pack ScampTrieNode3 directly into ram (256 bits)
// Layout according to Verilog struct:
// [3-bit node_type][22-bit bmp][16-bit nexthop_bitmap_ptr][22-bit nexthop_ptr]
// [64-bit child_bitmap][22-bit child_ptr][4-bit child_stage_id]
// [32-bit child_info[0]][32-bit child_info[1]][32-bit child_info[2]][3-bit reserved]
// The 256 bits are stored at ram_entry[255:0]
// node_type occupies [2:0], with MSB at bit 2, LSB at bit 0
void MemInit::pack_node_type3(const ScampTrieNode3* node3, std::bitset<256>& ram_entry) {
    if (M_PACK_TAG) printf("[M_PACK] pack_node_type3: childBitmap=0x%016llX, childPointer=%d\n", 
                           node3->childBitmap, node3->childPointer);
    int bit_pos = 0;  // Start from LSB (bit 0 of the 256-bit value, which is ram_entry[0])
    uint16_t real_bmp = trie->bmp_manager->get_bmp_nexthop(node3->bmp);
    uint32_t real_nexthop_ptr = alloc_nexthop_array(node3->nhPointer);
    // Use hardware address instead of software pointer for child_ptr
    // child_ptr points to the first child in the continuous child array
    uint32_t real_child_ptr = 0;
    if (node3->childBitmap != 0) {
        // Get the hardware address of the first child node
        ScampTrieNode* first_child = trie->ram_pool->get(node3->childPointer, 0);
        if (first_child != nullptr) {
            real_child_ptr = first_child->mem_addr;
        }
        if (M_PACK_TAG) printf("[M_PACK] first_child=%p, real_child_ptr=%d\n", (void*)first_child, real_child_ptr);
    }
    
    pack_bits(ram_entry, bit_pos, node3->nodeType, 3);              // node_type: 3 bits [2:0]
    pack_bits(ram_entry, bit_pos, real_bmp, 22);                    // bmp: 22 bits [24:3]
    pack_bits(ram_entry, bit_pos, node3->nhBitmapPointer, 16);      // nexthop_bitmap_ptr: 16 bits [40:25]
    pack_bits(ram_entry, bit_pos, real_nexthop_ptr, 22);            // nexthop_ptr: 22 bits [62:41]
    pack_bits(ram_entry, bit_pos, node3->childBitmap, 64);          // child_bitmap: 64 bits [126:63]
    pack_bits(ram_entry, bit_pos, real_child_ptr, 22);              // child_ptr: 22 bits [148:127]
    pack_bits(ram_entry, bit_pos, node3->childStage & 0x0F, 4);    // child_stage_id: 4 bits [152:149]
    pack_bits(ram_entry, bit_pos, pack_child_info(node3->freeChild[0]), 32);  // child_info[0]: 32 bits [184:153]
    pack_bits(ram_entry, bit_pos, pack_child_info(node3->freeChild[1]), 32);  // child_info[1]: 32 bits [216:185]
    pack_bits(ram_entry, bit_pos, pack_child_info(node3->freeChild[2]), 32);  // child_info[2]: 32 bits [248:217]
    pack_bits(ram_entry, bit_pos, 0, 7);                            // reserved: 7 bits [255:249] (to align 256 bits)
}

// Pack PrefixInfo fields into bitset
// Layout: [64-bit ip_chunk][8-bit valid_len][22-bit nexthop] = 94 bits
// Helper function to pack prefix_info fields sequentially
void MemInit::pack_prefix_info_fields(std::bitset<256>& ram_entry, int& bit_pos, const PrefixInfo& prefix) {
    pack_bits(ram_entry, bit_pos, prefix.ip_64, 64);                // ip_chunk: 64 bits
    pack_bits(ram_entry, bit_pos, prefix.len_64, 8);                 // valid_len: 8 bits
    pack_bits(ram_entry, bit_pos, prefix.nexthop & 0x3FFFFF, 22);   // nexthop: 22 bits
}

// Pack ScampTrieNode4 directly into ram (128 bits)
// Layout according to Verilog struct:
// [3-bit node_type][22-bit bmp][94-bit prefix_info][9-bit reserved]
// The 128 bits are stored at ram_entry[127:0] (lower half)
// node_type occupies [2:0], with MSB at bit 2, LSB at bit 0
void MemInit::pack_node_type4(const ScampTrieNode4* node4, std::bitset<256>& ram_entry) {
    int bit_pos = 0;  // Start from LSB (bit 0 of the 128-bit value, which is ram_entry[0])
    uint16_t real_bmp = trie->bmp_manager->get_bmp_nexthop(node4->bmp);
    
    pack_bits(ram_entry, bit_pos, node4->nodeType, 3);              // node_type: 3 bits [2:0]
    pack_bits(ram_entry, bit_pos, real_bmp, 22);                    // bmp: 22 bits [24:3]
    pack_prefix_info_fields(ram_entry, bit_pos, node4->prefixInfo[0]);  // prefix_info: 94 bits [118:25]
    pack_bits(ram_entry, bit_pos, node4->reserved & 0x1FF, 9);      // reserved: 9 bits [127:119]
}

// Pack ScampTrieNode5 directly into ram (256 bits)
// Layout according to Verilog struct:
// [3-bit node_type][22-bit bmp][94-bit prefix_info[0]][94-bit prefix_info[1]][43-bit reserved]
// The 256 bits are stored at ram_entry[255:0]
// node_type occupies [2:0], with MSB at bit 2, LSB at bit 0
void MemInit::pack_node_type5(const ScampTrieNode5* node5, std::bitset<256>& ram_entry) {
    int bit_pos = 0;  // Start from LSB (bit 0 of the 256-bit value, which is ram_entry[0])
    uint16_t real_bmp = trie->bmp_manager->get_bmp_nexthop(node5->bmp);
    
    pack_bits(ram_entry, bit_pos, node5->nodeType, 3);              // node_type: 3 bits [2:0]
    pack_bits(ram_entry, bit_pos, real_bmp, 22);                    // bmp: 22 bits [24:3]
    pack_prefix_info_fields(ram_entry, bit_pos, node5->prefixInfo[0]);  // prefix_info[0]: 94 bits [118:25]
    pack_prefix_info_fields(ram_entry, bit_pos, node5->prefixInfo[1]);  // prefix_info[1]: 94 bits [212:119]
    pack_bits(ram_entry, bit_pos, node5->reserved & 0x7FFFFFFFFFF, 43);  // reserved: 43 bits [255:213]
}

// ========== Nexthop Array Management ==========

uint32_t MemInit::alloc_nexthop_array(uint32_t nexthop_ptr) {
    uint32_t real_ptr = nexthop_array_size;

    uint32_t size = trie->nh_bucket->get_size(nexthop_ptr);
    for (int i = 0; i < size; ++i)
        nexthop_array[nexthop_array_size++] = trie->nh_bucket->get_nh(nexthop_ptr, i);

    return real_ptr;
}

void MemInit::fill_ram(ScampTrieNode* node) {
    if (M_FILL_TAG) printf("[M_FILL] fill_ram: nodeType=%d, stage_id=%d, mem_addr=%d\n", 
                           node->nodeType, node->cur_stage, node->mem_addr);
    uint32_t stage_id = node->cur_stage;
    if (stage_id >= RAM_NUM) {
        msg_error("MemInit::fill_ram: stage_id out of range");
        return;
    }
    
    uint32_t addr = node->mem_addr >> 1;
    if (node->nodeType == TYPE1) {
        if (M_PACK_TAG) printf("[M_PACK] pack_node_type1\n");
        pack_node_type1(static_cast<ScampTrieNode1*>(node), ram[stage_id][addr]);
    } else if (node->nodeType == TYPE2) {
        if (M_PACK_TAG) printf("[M_PACK] pack_node_type2\n");
        pack_node_type2(static_cast<ScampTrieNode2*>(node), ram[stage_id][addr]);
    } else if (node->nodeType == TYPE3) {
        if (M_PACK_TAG) printf("[M_PACK] pack_node_type3\n");
        pack_node_type3(static_cast<ScampTrieNode3*>(node), ram[stage_id][addr]);
    } else if (node->nodeType == TYPE4) {
        if (M_PACK_TAG) printf("[M_PACK] pack_node_type4\n");
        pack_node_type4(static_cast<ScampTrieNode4*>(node), ram[stage_id][addr]);
    } else if (node->nodeType == TYPE5) {
        if (M_PACK_TAG) printf("[M_PACK] pack_node_type5\n");
        pack_node_type5(static_cast<ScampTrieNode5*>(node), ram[stage_id][addr]);
    }
}

// ========== Main Export Function ==========

void MemInit::export_mem_files() {
    if (M_EXPORT_TAG) printf("[M_EXPORT] export_mem_files start\n");
    // Export all 5 types of BRAM data
    if (M_EXPORT_TAG) printf("[M_EXPORT] Exporting BRAM node\n");
    export_bram_node();
    if (M_EXPORT_TAG) printf("[M_EXPORT] Exporting BRAM hash18\n");
    export_bram_hash(trie->hash18, trie->root18, file_hash18);
    if (M_EXPORT_TAG) printf("[M_EXPORT] Exporting BRAM hash64\n");
    export_bram_hash(trie->hash64, trie->root64, file_hash64);
    if (M_EXPORT_TAG) printf("[M_EXPORT] Exporting BRAM nexthop\n");
    export_bram_nexthop();
    if (M_EXPORT_TAG) printf("[M_EXPORT] Exporting BRAM nexthop_bitmap\n");
    export_bram_nexthop_bitmap();
    if (M_EXPORT_TAG) printf("[M_EXPORT] export_mem_files end\n");
}

// ========== BRAM Export Functions ==========

void MemInit::export_bram_node() {
    if (M_EXPORT_TAG) printf("[M_EXPORT] export_bram_node start\n");
    // Export each stage's RAM to a separate file
    // Each entry is 256 bits, output as binary string
    for (uint32_t stage_id = 0; stage_id < RAM_NUM; ++stage_id) {
        std::string filename = std::string(file_ram_prefix) + std::to_string(stage_id) + ".mem";
        if (M_EXPORT_TAG) printf("[M_EXPORT] Stage %d: filename=%s, len=%d\n", stage_id, filename.c_str(), ram_len[stage_id]);
        std::ofstream file(filename);
        
        if (!file.is_open()) {
            msg_error(("MemInit::export_bram_node: cannot open file " + filename).c_str());
            continue;
        }
        
        uint32_t len = ram_len[stage_id];
        for (uint32_t i = 0; i < len; i+=2) {
            write_bitset_binary_to_file(file, ram[stage_id][i >> 1]);
        }
        file.close();
    }
    if (M_EXPORT_TAG) printf("[M_EXPORT] export_bram_node end\n");
}

void MemInit::export_bram_hash(ScampTrieHash* hash_table, RootInfo** root_info_array, const char* filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        msg_error(("MemInit::export_bram_hash: cannot open file " + std::string(filename)).c_str());
        return;
    }
    
    // Export hash table: each entry is 27 bits
    // Format: [hit(1), node_addr(22), stageID(4)] = 27 bits per entry
    // hit comes from bucket_occupied, node_addr and stageID come from rootInfo
    // Output as binary string, one entry per line
    for (uint32_t i = 0; i < HASH_TABLE_SIZE; ++i) {
        bool hit = hash_table->bucket_occupied[i];
        RootInfo* root_info = root_info_array[i];
        
        uint32_t node_addr = 0;
        uint8_t stage_id = 0;
        
        if (root_info != nullptr) {
            ScampTrieNode* node = trie->ram_pool->get(root_info->rootPointer);
            node_addr = node->mem_addr & 0x3FFFFF; // 22 bits
            stage_id = root_info->stageID & 0xF; // 4 bits
        }
        
        // Pack: [hit(1 bit)][node_addr(22 bits)][stageID(4 bits)] = 27 bits
        uint64_t entry = ((hit ? 1ULL : 0ULL) << 26) | 
                         ((node_addr & 0x3FFFFF) << 4) | 
                         (stage_id & 0xF);
        write_binary_to_file(file, entry, 27);
    }
    
    file.close();

    // output the only root at layer 0
    uint32_t real_ptr = trie->ram_pool->get(trie->root->rootPointer)->mem_addr;
    printf("root0: ptr=%d stage=%d\n", real_ptr, trie->root->stageID);
}

void MemInit::export_bram_nexthop() {
    std::ofstream file(file_nexthop);
    if (!file.is_open()) {
        msg_error("MemInit::export_bram_nexthop: cannot open file");
        return;
    }
    
    // Export nexthop array
    // Each nexthop is 8 bits, output as binary string, one entry per line
    for (uint32_t i = 0; i < nexthop_array_size; ++i) {
        write_binary_to_file(file, nexthop_array[i], 8);
    }
    
    file.close();
}

void MemInit::export_bram_nexthop_bitmap() {
    std::ofstream file(file_nexthop_bitmap);
    if (!file.is_open()) {
        msg_error("MemInit::export_bram_nexthop_bitmap: cannot open file");
        return;
    }
    
    // Export nexthop bitmap bucket
    // Each bitmap is 64 bits, output as binary string, one entry per line
    // Access nh_bitmap_bucket through trie (now we can access size directly as friend)
    uint16_t bitmap_size = trie->nh_bitmap_bucket->size;
    
    for (uint32_t i = 0; i < bitmap_size; ++i) {
        write_binary_to_file(file, trie->nh_bitmap_bucket->buckets[i], 64);
    }
    
    file.close();
}

// ========== Helper Functions for File Writing ==========

void MemInit::write_binary_to_file(std::ofstream& file, uint64_t value, int bit_width) {
    // Write value as binary string (MSB first, for $readmemb)
    // Example: value=5, bit_width=4 -> "0101"
    for (int i = bit_width - 1; i >= 0; --i) {
        file << ((value >> i) & 1);
    }
    file << std::endl;
}

void MemInit::write_bitset_binary_to_file(std::ofstream& file, const std::bitset<256>& ram_entry) {
    // Write 256-bit bitset as binary string (MSB first, for $readmemb)
    for (int i = 255; i >= 0; --i) {
        file << ram_entry[i];
    }
    file << std::endl;
}