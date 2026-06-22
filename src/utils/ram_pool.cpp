#include "ram_pool.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

RAMPool::RAMPool(bool enable_hash) {
    this->enable_hash = enable_hash;
    Bound = enable_hash ? RAM_NUM : STAGE_NUM;
    memset(nodes, 0, sizeof(nodes));
    memset(item_sizes, 0, sizeof(item_sizes));
    memset(allocated, 0, sizeof(allocated));
    memset(region_lengths, 0, sizeof(region_lengths));
    next_fit_start = 1;

    uint64_t ram_all = 10500 * KILO_BYTES;
    MOOWeights weights;
    double _weights[RAM_NUM] = {
        // 120, 350, 30, 50, 30,
        // 25, 20, 10, 5, 5,
        // 5
        // 341.562939, 474.469475, 46.844343, 68.616083, 52.430009, 43.864834, 34.960787, 20.611048, 7.199659, 5.319881, 4.120942
        // 215.910149, 408.177964, 148.696106, 100.190809, 86.256109, 70.303229, 37.576726, 14.108708, 8.452062, 4.966289, 5.361848
        122.158195, 320.450221, 146.088687, 272.435085, 85.914764, 48.938632, 58.034746, 8.246051, 3.543997, 29.666623, 4.522999
    };
    for (int i = 11; i < RAM_NUM; ++i) _weights[i] = 5;
    for (int i = 0; i < RAM_NUM; ++i) weights[i] = _weights[i];
    ALLOC_METHOD = 1;
    
    if (ALLOC_METHOD == 1) ram_all = 10 * KILO_BYTES;
    set_ram_allocation(ram_all, weights);
}

RAMPool::~RAMPool() {
    for (uint32_t i = 0; i < BUCKET_SIZE; ++i)
        if (nodes[i] != nullptr)
            delete nodes[i];
}

void RAMPool::set_ram_allocation(uint64_t total_ram, const MOOWeights& weights) {
    const double* _weights = weights.weights;
    double ratio_all = 0;
    for (int i = 0; i < RAM_NUM; ++i) ratio_all += _weights[i];
    for (int i = 0; i < RAM_NUM; ++i) {
        ram_ratio[i] = weights[i] / ratio_all;
        ram_remain[i] = ram_total[i] = total_ram * ram_ratio[i];
    }
    for (int i = RAM_NUM; i < STAGE_NUM; ++i) {
        ram_total[i] = 10 * MEGA_BYTES;
        ram_remain[i] = ram_total[i];
    }
}

uint32_t RAMPool::alloc(
    uint32_t len, ScampNodeType node_type,
    stage_t lowest_stage, stage_t child_depth,
    uint32_t &ret_ptr, stage_t &ret_stage) {
    if (len == 0 || len > BUCKET_SIZE)
        msg_error("RAMPool::alloc: len out of range");
    
    uint32_t start_pos = find_continuous_space(len, node_type);
    if (start_pos == RAM_POOL_FAIL)
        return RAM_POOL_FAIL;
    ret_ptr = start_pos;
    ret_stage = scan_for_space(
        NODE_SPACE[node_type] * len, lowest_stage,
        Bound - child_depth // depth=0 -> [..., Bound)
    );
    if (ret_stage == STAGE_DEFAULT) {
        printf("RAMPool::alloc: low_stage: %d, ch_dep: %d\n", lowest_stage, child_depth);
        printf("node_type: %d, len: %d, alloc_size: %d\n", node_type, len, NODE_SPACE[node_type] * len);
        print_ram_info();
        return RAM_POOL_FAIL;
    }
    return RAM_POOL_OK;
}

ScampTrieNode* RAMPool::get(uint32_t base_pos, uint32_t offset) {
    uint32_t pos = base_pos + offset;
    if (pos >= BUCKET_SIZE || !allocated[pos])
        return nullptr;

    ScampTrieNode* node = nodes[pos];
    if (node == nullptr) {
        printf("[ERROR] RAMPool::get - Node is nullptr at position %d\n", pos);
        exit(1);
    }
    if (node->nodeType > 5) {
        printf("[ERROR] RAMPool::get - Invalid nodeType: %d at position %d\n", node->nodeType, pos);
        exit(1);
    }
    return node;
}

uint32_t RAMPool::get_region_length(uint32_t ptr) {
    if (ptr >= BUCKET_SIZE || !allocated[ptr])
        return 0;
    return region_lengths[ptr];
}

void RAMPool::set(const uint32_t ptr, uint32_t offset, ScampTrieNode* node) {
    if (node == nullptr)
        msg_error("RAMPool::set: node is nullptr");
    if (ptr + offset >= BUCKET_SIZE || !allocated[ptr]) {
        msg_error("RAMPool::set: ptr+offset out of range or region not allocated");
        return;
    }
    uint32_t region_len = region_lengths[ptr];
    if (offset >= region_len)
        msg_error("RAMPool::set: offset out of allocated range");

    nodes[ptr + offset] = node;
}

uint32_t RAMPool::insert(
    uint32_t &ptr, stage_t &final_stage,
    uint32_t offset, ScampTrieNode* node,
    stage_t lowest_stage, stage_t child_depth) {
    if (ptr >= BUCKET_SIZE || !allocated[ptr])
        msg_error("RAMPool::insert: ptr out of range or region not allocated");
    uint32_t len = region_lengths[ptr];
    if (offset > len) {
        printf("RAMPool::insert: offset out of allocated range, ptr: %d, offset: %d, len: %d\n", ptr, offset, len);
        return RAM_POOL_FAIL;
    }
    stage_t old_stage = final_stage;
    // expand the region
    if (ptr + len < BUCKET_SIZE && !allocated[ptr + len] && ram_remain[final_stage] >= item_sizes[ptr]) {
        allocated[ptr + len] = true;
        ram_remain[final_stage] -= item_sizes[ptr];
        item_sizes[ptr + len] = item_sizes[ptr];
        region_lengths[ptr] = len + 1;
        len++;
    } else {
        uint32_t new_len = len + 1;
        uint32_t new_ptr;
        if (lowest_stage == Bound) {
            printf("RAMPool::insert: start_stage is Bound\n");
            print_ram_info();
            return RAM_POOL_FAIL;
        }

        for (int i = 0; i < len; ++i)
            buffer_nodes[i] = nodes[ptr + i];
        free(ptr, old_stage);
        if (alloc(
            new_len, TYPE3,
            lowest_stage, child_depth,
            new_ptr, final_stage
        )) return RAM_POOL_FAIL;
        for (int i = 0; i < len; ++i)
            nodes[new_ptr + i] = buffer_nodes[i];
        
        ptr = new_ptr;
        len = new_len;
    }
    
    for (int i = len - 1; i > (int)offset; --i)
        nodes[ptr + i] = nodes[ptr + i - 1];
    
    if (ptr + offset >= BUCKET_SIZE)
        msg_error("RAMPool::insert: Insert position out of bounds");
    nodes[ptr + offset] = node;
    if (old_stage != final_stage) {
        for (uint32_t i = 0; i < len; ++i)
            nodes[ptr + i]->cur_stage = final_stage;
    } else node->cur_stage = final_stage;
    return RAM_POOL_OK;
}

uint32_t RAMPool::adjust_stage(
    uint32_t &ptr, stage_t &final_stage,
    stage_t lowest_stage, stage_t child_depth, ScampNodeType node_type) {
    if (ptr >= BUCKET_SIZE || !allocated[ptr])
        msg_error("RAMPool::adjust_stage: ptr out of range or region not allocated");
    uint32_t len = region_lengths[ptr];
    uint32_t new_ptr;
    stage_t old_stage = final_stage;
    if (lowest_stage == Bound) {
        printf("RAMPool::adjust_stage: start_stage is Bound\n");
        printf("lowest_stage: %d, child_depth: %d, now_stage: %d\n", lowest_stage, child_depth, final_stage);
        // print_ram_info();
        return RAM_POOL_FAIL;
    }
    if ((final_stage >= lowest_stage || lowest_stage == STAGE_DEFAULT) && NODE_SPACE[node_type] == item_sizes[ptr])
        return RAM_POOL_OK; // no need to adjust
    if (alloc(len, node_type, lowest_stage, child_depth, new_ptr, final_stage))
        return RAM_POOL_FAIL;
    if (new_ptr != ptr) {
        for (uint32_t i = 0; i < len; ++i)
            nodes[new_ptr + i] = nodes[ptr + i];
        free(ptr, old_stage);
        ptr = new_ptr;
    }
    for (uint32_t i = 0; i < len; ++i)
        nodes[ptr + i]->cur_stage = final_stage;
    return RAM_POOL_OK;
}

void RAMPool::free(uint32_t start_pos, stage_t stage) {
    if (start_pos + region_lengths[start_pos] > BUCKET_SIZE) {
        msg_error("RAMPool::free: start_pos + region_lengths[start_pos] out of range or region not allocated");
        return;
    }
    uint32_t region_len = region_lengths[start_pos];
    ram_remain[stage] += region_len * item_sizes[start_pos];
    for (uint32_t i = start_pos; i < start_pos + region_len; ++i) {
        nodes[i] = nullptr;
        allocated[i] = false;
        item_sizes[i] = 0;
        region_lengths[i] = 0;
    }
}

stage_t RAMPool::scan_for_space(uint32_t alloc_size, stage_t lowest_stage, stage_t highest_stage) {
    if (ALLOC_METHOD == 0) { // static
        if (lowest_stage == STAGE_DEFAULT) lowest_stage = 0;
        for (int i = lowest_stage; i < highest_stage; ++i)
            if (ram_remain[i] >= alloc_size) {
                ram_remain[i] -= alloc_size;
                return i;
            }
        return STAGE_DEFAULT;
    } else { // dynamic allocation for RAM resource
        if (lowest_stage == STAGE_DEFAULT) lowest_stage = 0;
        if (lowest_stage >= RAM_NUM) {
            int i = lowest_stage;
            while (i < Bound && ram_remain[i] < alloc_size) i++;
            if (i == Bound) return STAGE_DEFAULT;
            ram_remain[i] -= alloc_size;
            return i;
        }
        if (highest_stage >= RAM_NUM) highest_stage = RAM_NUM;

        // if (highest_stage - lowest_stage >= 3)
        //     highest_stage = lowest_stage + 3;

        for (int i = lowest_stage; i < highest_stage; ++i)
            if (ram_remain[i] >= alloc_size) {
                ram_remain[i] -= alloc_size;
                return i;
            }
        
        // allocate extra space of minimum required
        double min_ram =
            (ram_total[lowest_stage] + alloc_size - ram_remain[lowest_stage]) / ram_ratio[lowest_stage];
        min_ram += 1*KILO_BYTES;

        // printf("[RAM Expand] Allocating %8.3lf KB in [%d, %d), %8.3lf KB -> %8.3lf KB\n",
        //     min_ram / KILO_BYTES, lowest_stage, highest_stage, ram_total[0] / ram_ratio[0] / KILO_BYTES, min_ram / KILO_BYTES);
        for (int i = 0; i < RAM_NUM; ++i) {
            uint64_t extra_size = min_ram * ram_ratio[i] - ram_total[i];
            ram_remain[i] += extra_size;
            ram_total[i] += extra_size;
        }

        // scan again
        for (int i = lowest_stage; i < highest_stage; ++i)
            if (ram_remain[i] >= alloc_size) {
                ram_remain[i] -= alloc_size;
                return i;
            }
        return STAGE_DEFAULT;
    }
}

uint32_t RAMPool::find_continuous_space(uint32_t len, ScampNodeType node_type) {
    if (len > 1) node_type = TYPE3; // default as a child of TYPE3
    // Next-fit: start from next_fit_start to reduce average scan length (hot path: ~84% in profiler)
    for (int round = 0; round < 2; ++round) {
        uint32_t i = (round == 0) ? next_fit_start : 1;
        uint32_t end = (round == 0) ? BUCKET_SIZE : next_fit_start;
        while (i < end) {
            while (i < end && allocated[i]) i++;
            if (i >= end) break;
            uint32_t start_pos = i;
            while (i < end && !allocated[i]) {
                if (i - start_pos + 1 >= len) {
                    for (uint32_t j = 0; j < len; ++j) {
                        allocated[start_pos + j] = true;
                        item_sizes[start_pos + j] = NODE_SPACE[node_type];
                    }
                    region_lengths[start_pos] = len;
                    next_fit_start = start_pos + len;
                    if (next_fit_start >= BUCKET_SIZE) next_fit_start = 1;
                    return start_pos;
                }
                i++;
            }
        }
    }
    return RAM_POOL_FAIL;
}

void RAMPool::print_ram_info() {
    uint64_t total_allocated = 0, total_remain = 0;
    int info_range = ALLOC_METHOD == 0 ? STAGE_NUM : RAM_NUM;
    for (int i = 0; i < info_range; ++i) {
        total_allocated += ram_total[i];
        total_remain += ram_remain[i];
    }
    uint64_t total_used = total_allocated - total_remain;
    
    printf("\n[RAMPool Memory Usage Statistics] ================================================\n");
    
    // 打印表头
    printf("    Stage   |  Total(KB) |  Remain(KB) |  Used(KB)  |  Usage(%%)\n");
    printf("------------|------------|-------------|------------|----------\n");
    
    // 为每个stage打印信息
    for (int i = 0; i < info_range; ++i) {
        double allocated_percent = (double)ram_total[i] / total_allocated * 100;
        uint64_t used = ram_total[i] - ram_remain[i];
        double usage_percent = 100.0 * used / ram_total[i];
        
        printf("%2d (%5.2f%%) | %10.1f | %11.1f | %10.1f | %7.1f%%\n", 
                i,
                allocated_percent,
                (double)ram_total[i] / KILO_BYTES,
                (double)ram_remain[i] / KILO_BYTES,
                (double)used / KILO_BYTES,
                usage_percent);
    }
    
    // 打印总计行
    printf("------------|------------|-------------|------------|----------\n");
    double total_usage_percent = (total_allocated > 0) ? (100.0 * total_used / total_allocated) : 0.0;
    printf("Total(100%%) | %10.1f | %11.1f | %10.1f | %7.1f%%\n",
            (double)total_allocated / KILO_BYTES,
            (double)total_remain / KILO_BYTES,
            (double)total_used / KILO_BYTES,
            total_usage_percent);
    
    double tcam_ram = 0;
    for (int i = info_range; i < STAGE_NUM; ++i)
        tcam_ram += ram_total[i] - ram_remain[i];
    printf("Replaced by TCAM: %10.1f (KB)\n", tcam_ram / KILO_BYTES);
    
    total_used = 0;
    for (int i = 0; i < BUCKET_SIZE; ++i)
        if (allocated[i])
            total_used += region_lengths[i] * item_sizes[i];
    printf("Total Used: %10.1f (KB)\n", (double)total_used / KILO_BYTES);

    printf("[RAMPool Memory Usage Statistics] ================================================\n\n");
}

void RAMPool::get_ram_info(uint64_t &ram_used, uint64_t &ram_allocated) { 
    ram_used = 0;
    ram_allocated = 0;
    for (int i = 0; i < RAM_NUM; ++i) {
        ram_used += ram_total[i] - ram_remain[i];
        ram_allocated += ram_total[i];
    }
}

void RAMPool::get_stage_usage(uint64_t ram_used_per_stage[RAM_NUM]) {
    // NOTE: Only the first RAM_NUM stages are considered pipeline RAM stages.
    // The values are in BYTES; caller is responsible for converting to bits.
    for (int i = 0; i < RAM_NUM; ++i) {
        ram_used_per_stage[i] = ram_total[i] - ram_remain[i];
    }
}

void RAMPool::get_stage_allocated(uint64_t ram_allocated_per_stage[RAM_NUM]) {
    for (int i = 0; i < RAM_NUM; ++i) {
        ram_allocated_per_stage[i] = ram_total[i];
    }
}

void RAMPool::get_stage_type_counts(uint32_t counts[RAM_NUM][7]) {
    for (int s = 0; s < RAM_NUM; ++s)
        for (int t = 0; t < 7; ++t)
            counts[s][t] = 0;
    for (uint32_t i = 0; i < BUCKET_SIZE; ++i) {
        if (!allocated[i] || nodes[i] == nullptr) continue;
        ScampTrieNode* node = nodes[i];
        if (node->cur_stage >= RAM_NUM) continue;
        ScampNodeType nt = node->nodeType;
        if (nt < TYPE1 || nt > TYPE5) continue;
        // map to output order: Type3->0, Type2->1, Type1->2, Type5->3, Type4->4
        static const int type_to_idx[] = { -1, 2, 1, 0, 4, 3 }; // NONE, TYPE1, TYPE2, TYPE3, TYPE4, TYPE5
        int idx = type_to_idx[nt];
        if (idx >= 0) {
            counts[node->cur_stage][idx]++;
            counts[node->cur_stage][5]++; // Total
        }
    }
    // Per-stage count of nodes whose parent is Hybrid(Type3)
    for (uint32_t i = 0; i < BUCKET_SIZE; ++i) {
        if (!allocated[i] || nodes[i] == nullptr) continue;
        ScampTrieNode* node = nodes[i];
        if (node->nodeType != TYPE3) continue;
        ScampTrieNode3* node3 = static_cast<ScampTrieNode3*>(node);
        for (int j = 0; j < 3; ++j) {
            if (node3->freeChild[j].stageID == STAGE_NONE) continue;
            ScampTrieNode* ch = get(node3->freeChild[j].childPointer);
            if (ch != nullptr && ch->cur_stage < RAM_NUM)
                counts[ch->cur_stage][6]++;
        }
        uint8_t child_cnt = count_ones_before_pos(node3->childBitmap, 64);
        for (uint8_t j = 0; j < child_cnt; ++j) {
            ScampTrieNode* ch = get(node3->childPointer, j);
            if (ch != nullptr && ch->cur_stage < RAM_NUM)
                counts[ch->cur_stage][6]++;
        }
    }
}