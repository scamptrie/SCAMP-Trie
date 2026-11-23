#include "scamptrie_dynamic.h"
#include "scamptrie_base.h"
#include "utils/mem_init.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

static FILE* open_file_write_portable(const char* path) {
#ifdef _WIN32
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "w") == 0) return fp;
    return nullptr;
#else
    return fopen(path, "w");
#endif
}

ScampTrieDynamic::ScampTrieDynamic() {
    root = nullptr;
    if (enable_hash) {
        for (int i = 0; i < HASH_TABLE_SIZE; ++i)
            root18[i] = nullptr;
        for (int i = 0; i < HASH_TABLE_SIZE; ++i)
            root64[i] = nullptr;
        hash18 = new ScampTrieHash();
        hash64 = new ScampTrieHash();
    }

    nh_bitmap_bucket = new NexthopBitmapBucket();
    nh_bucket = new NexthopBucket();
    ram_pool = new RAMPool(enable_hash);
    bmp_manager = new BMPManager();
}

void ScampTrieDynamic::print_insert_prefix_timing_stats(FILE* out, const char* sorted_samples_file) const {
    if (insert_timing_count_ == 0) {
        fprintf(out, "[ScampTrieDynamic::insert_prefix timing] (no inserts)\n");
        return;
    }
    double mean_ns = static_cast<double>(insert_timing_sum_ns_) / static_cast<double>(insert_timing_count_);
    uint64_t max_ns = 0;
    uint64_t p99_ns = 0;
    if (!insert_timing_ns_samples_.empty()) {
        std::vector<uint64_t> v = insert_timing_ns_samples_;
        std::sort(v.begin(), v.end());
        if (sorted_samples_file != nullptr) {
            FILE* fp = open_file_write_portable(sorted_samples_file);
            if (fp != nullptr) {
                for (uint64_t x : v) fprintf(fp, "%llu\n", static_cast<unsigned long long>(x));
                fclose(fp);
            } else {
                fprintf(out, "[ScampTrieDynamic::insert_prefix timing] failed to open %s\n", sorted_samples_file);
            }
        }
        max_ns = *std::max_element(v.begin(), v.end());
        const size_t n = v.size();
        size_t idx = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(n))) - 1;
        if (idx >= n)
            idx = n - 1;
        p99_ns = v[idx];
    }
    fprintf(out,
            "[ScampTrieDynamic::insert_prefix timing] n=%llu  mean=%.2f ns  p99=%llu ns  max=%llu ns\n",
            static_cast<unsigned long long>(insert_timing_count_),
            mean_ns,
            static_cast<unsigned long long>(p99_ns),
            static_cast<unsigned long long>(max_ns));
}

void ScampTrieDynamic::print_insert_prefix_stage_raise_stats(FILE* out, const char* sorted_samples_file) const {
    if (insert_stage_raise_count_ == 0) {
        fprintf(out, "[ScampTrieDynamic::insert_prefix stage-raise] (no inserts)\n");
        return;
    }
    double mean = static_cast<double>(insert_stage_raise_sum_) / static_cast<double>(insert_stage_raise_count_);
    uint32_t max_v = 0;
    uint32_t p99_v = 0;
    if (!insert_stage_raise_samples_.empty()) {
        std::vector<uint32_t> v = insert_stage_raise_samples_;
        std::sort(v.begin(), v.end());
        if (sorted_samples_file != nullptr) {
            FILE* fp = open_file_write_portable(sorted_samples_file);
            if (fp != nullptr) {
                for (uint32_t x : v) fprintf(fp, "%u\n", x);
                fclose(fp);
            } else {
                fprintf(out, "[ScampTrieDynamic::insert_prefix stage-raise] failed to open %s\n", sorted_samples_file);
            }
        }
        max_v = *std::max_element(v.begin(), v.end());
        const size_t n = v.size();
        size_t idx = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(n))) - 1;
        if (idx >= n)
            idx = n - 1;
        p99_v = v[idx];
    }
    fprintf(out,
            "[ScampTrieDynamic::insert_prefix stage-raise] n=%llu  mean=%.2f  p99=%u  max=%u\n",
            static_cast<unsigned long long>(insert_stage_raise_count_),
            mean,
            p99_v,
            max_v);
}

ScampTrieDynamic::~ScampTrieDynamic() {
    if (root != nullptr) delete root;
    if (enable_hash) {
        for (int i = 0; i < HASH_TABLE_SIZE; ++i)
            if (root18[i] != nullptr) delete root18[i];
        for (int i = 0; i < HASH_TABLE_SIZE; ++i)
            if (root64[i] != nullptr) delete root64[i];
        delete hash18;
        delete hash64;
    }
    delete nh_bitmap_bucket;
    delete nh_bucket;
    delete ram_pool;
    delete bmp_manager;
}

void ScampTrieDynamic::use_basic_mapping() { ram_pool->use_basic_mapping(); }

void ScampTrieDynamic::set_ram_allocation(uint64_t total_ram, const MOOWeights& weights) { ram_pool->set_ram_allocation(total_ram, weights); }

void ScampTrieDynamic::get_ram_info(uint64_t &ram_used, uint64_t &ram_allocated, uint32_t &tcam_entries) {
    ram_pool->get_ram_info(ram_used, ram_allocated);
    // enable_hash 固定为 True，TCAM 统计逻辑移除掉（原实现里 enable_hash==True 为占位值 1001）
    tcam_entries = 1001;
}

void ScampTrieDynamic::get_stage_usage(uint64_t ram_used_per_stage[RAM_NUM]) {
    ram_pool->get_stage_usage(ram_used_per_stage);
}

void ScampTrieDynamic::get_storage_usage(uint64_t ram_used_per_stage[RAM_NUM],
                                        uint64_t &fixed_capacity_bits,
                                        uint64_t &fixed_used_bits,
                                        uint64_t &elastic_used_bits) const {
    // 先收集每级流水的 RAM 使用情况（字节）
    ram_pool->get_stage_usage(ram_used_per_stage);

    fixed_capacity_bits = 0;
    fixed_used_bits = 0;
    elastic_used_bits = 0;

    // 1. Hash: 两块，每块 4096 * 65 bits，属于“其他固定存储”
    //    是否计入容量：只要该 hash 表有至少一个 bucket 被占用
    //    实际使用：占用 bucket 的数量 * 65 bits
    if (enable_hash) {
        const uint64_t HASH_ENTRY_BITS = 65ULL;
        const uint64_t HASH_TABLE_CAPACITY_BITS = (uint64_t)HASH_TABLE_SIZE * HASH_ENTRY_BITS; // 4096 * 65

        if (hash18 && hash18->has_any_bucket()) {
            uint32_t used_cnt = hash18->get_used_bucket_count();
            fixed_capacity_bits += HASH_TABLE_CAPACITY_BITS;
            fixed_used_bits += (uint64_t)used_cnt * HASH_ENTRY_BITS;
        }
        if (hash64 && hash64->has_any_bucket()) {
            uint32_t used_cnt = hash64->get_used_bucket_count();
            fixed_capacity_bits += HASH_TABLE_CAPACITY_BITS;
            fixed_used_bits += (uint64_t)used_cnt * HASH_ENTRY_BITS;
        }
    }

    // 2. NexthopBitmapBucket: 一块，size 为使用量，每个 64 bits，属于“其他弹性存储”
    if (nh_bitmap_bucket) {
        const uint64_t BITMAP_ENTRY_BITS = 64ULL;
        elastic_used_bits = (uint64_t)nh_bitmap_bucket->get_size() * BITMAP_ENTRY_BITS;
    }
}

void ScampTrieDynamic::get_stage_allocated(uint64_t ram_allocated_per_stage[RAM_NUM]) const {
    ram_pool->get_stage_allocated(ram_allocated_per_stage);
}

void ScampTrieDynamic::get_stage_type_counts(uint32_t counts[RAM_NUM][7]) const {
    ram_pool->get_stage_type_counts(counts);
}

nexthop_t ScampTrieDynamic::lookup(ip6_t ip, uint32_t &bmp_idx, int start_layer) {
    bmp_idx = 0; // default as virtual root
    
    uint16_t hash_val = 0;
    ScampTrieNode* node = nullptr;
    uint64_t ip64 = 0;
    uint32_t len = 0;
    if (enable_hash && start_layer >= 64 && this->hash64->get64_12(ip.hi, hash_val)) {
        node = ram_pool->get(root64[hash_val]->rootPointer);
        ip64 = ip.lo;
        len = 64;
    } else if (enable_hash && start_layer >= FIRST_LEVEL_BITS && this->hash18->get64_12(ip.hi & MASK_FIRST_LEVEL, hash_val)) {
        node = ram_pool->get(root18[hash_val]->rootPointer);
        ip64 = ip.hi << FIRST_LEVEL_BITS;
        len = 64 - FIRST_LEVEL_BITS;
    } else {
        if (root == nullptr) return LPM_INVALID_NH; // root has not been created yet
        node = ram_pool->get(root->rootPointer);
        ip64 = ip.hi & MASK_FIRST_LEVEL;
        len = FIRST_LEVEL_BITS;
    }
    if (!enable_hash) len = 128;
    if (node == nullptr) return LPM_INVALID_NH;

    uint32_t node_addr = 0;
    for (int i = 0; i <= len; i += 6) {
        uint16_t idx6 = enable_hash ? get_substring_in_subtrie(ip64, i) : get_substring_in_subtrie(ip, i);

        if (node->nodeType == TYPE4) {
            ScampTrieNode4* node4 = static_cast<ScampTrieNode4*>(node);
            bool match = enable_hash ? node4->prefixInfo[0].match64(ip64) : node4->prefixInfo[0].match128(ip);
            if (match) {
                bmp_idx = node4->prefixInfo[0].bmp_entry_idx;
                return node4->prefixInfo[0].nexthop;
            }
            bmp_idx = node4->bmp;
            nexthop_t real_bmp = bmp_manager->get_bmp_nexthop(bmp_idx);
            return real_bmp;
        } else if (node->nodeType == TYPE5) {
            ScampTrieNode5* node5 = static_cast<ScampTrieNode5*>(node);
            bool match0 = enable_hash ? node5->prefixInfo[0].match64(ip64) : node5->prefixInfo[0].match128(ip);
            bool match1 = enable_hash ? node5->prefixInfo[1].match64(ip64) : node5->prefixInfo[1].match128(ip);

            if (match0 && match1) {
                nexthop_t result;
                uint32_t better_idx = node5->prefixInfo[0].len_128 > node5->prefixInfo[1].len_128 ? 0 : 1;
                result = node5->prefixInfo[better_idx].nexthop;
                bmp_idx = node5->prefixInfo[better_idx].bmp_entry_idx;
                return result;
            }
            if (match0 || match1) {
                nexthop_t result;
                result = node5->prefixInfo[match1].nexthop;
                bmp_idx = node5->prefixInfo[match1].bmp_entry_idx;
                return result;
            }
            bmp_idx = node5->bmp;
            nexthop_t real_bmp = bmp_manager->get_bmp_nexthop(bmp_idx);
            return real_bmp;
        } else if (node->nodeType == TYPE1) {
            ScampTrieNode1* node1 = static_cast<ScampTrieNode1*>(node);
            bool match_child = false;
            for (int j = 0; j < 2; ++j)
                if (node1->childInfo[j].stageID != STAGE_NONE && node1->childInfo[j].ip_sub6 == idx6) {
                    node = this->ram_pool->get(node1->childInfo[j].childPointer);
                    node_addr = node1->childInfo[j].childPointer;
                    match_child = true;
                    break;
                }
            if (match_child) continue;
        } else if (node->nodeType == TYPE2) {
            ScampTrieNode2* node2 = static_cast<ScampTrieNode2*>(node);
            bool match_child = false;
            for (int j = 0; j < 6; ++j)
                if (node2->childInfo[j].stageID != STAGE_NONE && node2->childInfo[j].ip_sub6 == idx6) {
                    node = this->ram_pool->get(node2->childInfo[j].childPointer);
                    node_addr = node2->childInfo[j].childPointer;
                    match_child = true;
                    break;
                }
            if (match_child) continue;
        } else if (node->nodeType == TYPE3) {
            ScampTrieNode3* node3 = static_cast<ScampTrieNode3*>(node);
            bool match_child = false;
            if (enable_free_child) {
                for (int j = 0; j < 3; ++j)
                    if (node3->freeChild[j].stageID != STAGE_NONE && node3->freeChild[j].ip_sub6 == idx6) {
                        node = this->ram_pool->get(node3->freeChild[j].childPointer);
                        node_addr = node3->freeChild[j].childPointer;
                        match_child = true;
                        break;
                    }
            }
            if (match_child) continue;
            if (node3->childBitmap & (1ULL << idx6)) {
                uint16_t child_offset = count_ones_before_pos(node3->childBitmap, idx6);
                node = this->ram_pool->get(node3->childPointer + child_offset);
                node_addr = node3->childPointer + child_offset;
                continue;
            }
        }

        // no child match, check inner bitmap
        uint16_t nhBitmapPointer = node->get_nh_bitmap_pointer();
        uint32_t nhPointer = node->get_nh_pointer();
        uint64_t bitmap = this->nh_bitmap_bucket->get_bitmap(nhBitmapPointer);

        int remain_len = len - i;
        if (remain_len > 5) remain_len = 5;
        for (int len = remain_len; len >= 0; --len) {
            uint16_t pos = get_nh_bitmap_pos_6(idx6, (uint8_t)len);
            if (bitmap & (1ULL << pos)) {
                uint16_t offset = count_ones_before_pos(bitmap, pos);
                nexthop_t result = nh_bucket->get_nh(nhPointer, offset);
                bmp_idx = nh_bucket->get_bmp_idx(nhPointer, offset);
                return result;
            }
        }
        bmp_idx = node->bmp;
        nexthop_t real_bmp = bmp_manager->get_bmp_nexthop(bmp_idx);
        return real_bmp;
    }

    return LPM_INVALID_NH;
}

uint32_t ScampTrieDynamic::reshape() {
    return SCAMPTRIE_OK;
}

uint32_t ScampTrieDynamic::insert_prefix(ip6_t prefix128, uint8_t len128, nexthop_t nh) {
    uint32_t stage_raise_loop_cnt = 0;
    struct InsertTimingScopeGuard {
        ScampTrieDynamic* self;
        uint32_t* stage_raise_loop_cnt_ptr;
        std::chrono::steady_clock::time_point t0;
        ~InsertTimingScopeGuard() {
            if (self == nullptr)
                return;
            self->insert_stage_raise_sum_ += static_cast<uint64_t>(*stage_raise_loop_cnt_ptr);
            self->insert_stage_raise_count_ += 1;
            self->insert_stage_raise_samples_.push_back(*stage_raise_loop_cnt_ptr);
            if (!self->insert_timing_enabled_)
                return;
            const uint64_t ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            self->insert_timing_sum_ns_ += ns;
            self->insert_timing_count_ += 1;
            self->insert_timing_ns_samples_.push_back(ns);
        }
    } _insert_timing_guard{this, &stage_raise_loop_cnt, std::chrono::steady_clock::now()};

    thread_local static int insert_prefix_count = 0;
    insert_prefix_count++;
    ScampTrieNode* current_node = nullptr;
    RootInfo* root_ptr = nullptr;

    // these 3 variables are used when enable_hash=1
    uint8_t len64 = 0;
    uint64_t prefix64 = 0;
    uint8_t len_0_18_64 = 0;

    // Step1. Choose the root out of three layers
    if (!enable_hash || len128 <= FIRST_LEVEL_BITS) { // even if there's no hash, root initialization is also necessary
        len64 = len128;
        len_0_18_64 = 0;
        prefix64 = prefix128.hi & MASK_FIRST_LEVEL;
        if (root == nullptr) {
            // root(at layer 0) has no BMP -> virtual root
            uint32_t bmp_father_idx = 0;
            // bmp_current_idx is not used, but insert_prefix will register an entry in bmp_manager
            uint32_t bmp_current_idx = bmp_manager->insert_prefix(prefix128, len128, nh, bmp_father_idx);
            PrefixInfo pInfo = (PrefixInfo){prefix64, len64, static_cast<uint32_t>(nh), bmp_current_idx, prefix128, len128};
            root = new RootInfo();
            ScampTrieNode4* node4 = gen_from_1pf(-6, -1, bmp_father_idx, pInfo);
            if (ram_pool->alloc(1, TYPE4, STAGE_DEFAULT, 0, root->rootPointer, root->stageID))
                return SCAMPTRIE_FAIL;
            node4->cur_stage = root->stageID;
            bmp_manager->insert_node(node4);
            ram_pool->set(root->rootPointer, 0, node4);
            return SCAMPTRIE_OK;
        }
        current_node = ram_pool->get(root->rootPointer);
        root_ptr = root;
    } else if (len128 <= 64) {
        len64 = len128 - FIRST_LEVEL_BITS;
        len_0_18_64 = FIRST_LEVEL_BITS;
        prefix64 = prefix128.hi << FIRST_LEVEL_BITS;
        uint16_t hash_val = 0;
        uint64_t prefix18 = prefix128.hi & MASK_FIRST_LEVEL;
        if (!hash18->get64_12(prefix18, hash_val)) {
            if (!hash18->alloc64_12(prefix18)) {
                return SCAMPTRIE_FAIL;
            }
            hash18->get64_12(prefix18, hash_val);
            uint32_t bmp_father_idx = 0;
            nexthop_t nh_bmp = lookup(prefix128, bmp_father_idx, 0);
            uint32_t bmp_current_idx = bmp_manager->insert_prefix(prefix128, len128, nh, bmp_father_idx);
            PrefixInfo pInfo = (PrefixInfo){prefix64, len64, static_cast<uint32_t>(nh), bmp_current_idx, prefix128, len128};
            root18[hash_val] = new RootInfo();
            ScampTrieNode4* node4 = gen_from_1pf(FIRST_LEVEL_BITS - 6, -1, bmp_father_idx, pInfo);
            if (ram_pool->alloc(1, TYPE4, STAGE_DEFAULT, 0, root18[hash_val]->rootPointer, root18[hash_val]->stageID))
                return SCAMPTRIE_FAIL;
            node4->cur_stage = root18[hash_val]->stageID;
            bmp_manager->insert_node(node4);
            ram_pool->set(root18[hash_val]->rootPointer, 0, node4);
            return SCAMPTRIE_OK;
        }
        current_node = ram_pool->get(root18[hash_val]->rootPointer);
        root_ptr = root18[hash_val];
    } else { // 64 < len128 <= 128
        len64 = len128 - 64;
        len_0_18_64 = 64;
        prefix64 = prefix128.lo;
        uint16_t hash_val = 0;
        if (!hash64->get64_12(prefix128.hi, hash_val)) {
            if (!hash64->alloc64_12(prefix128.hi)) {
                return SCAMPTRIE_FAIL;
            }
            hash64->get64_12(prefix128.hi, hash_val);
            uint32_t bmp_father_idx = 0;
            nexthop_t nh_bmp = lookup(prefix128, bmp_father_idx, FIRST_LEVEL_BITS);
            uint32_t bmp_current_idx = bmp_manager->insert_prefix(prefix128, len128, nh, bmp_father_idx);
            PrefixInfo pInfo = (PrefixInfo){prefix64, len64, static_cast<uint32_t>(nh), bmp_current_idx, prefix128, len128};
            root64[hash_val] = new RootInfo();
            ScampTrieNode4* node4 = gen_from_1pf(64 - 6, -1, bmp_father_idx, pInfo);
            if (ram_pool->alloc(1, TYPE4, STAGE_DEFAULT, 0, root64[hash_val]->rootPointer, root64[hash_val]->stageID))
                return SCAMPTRIE_FAIL;
            node4->cur_stage = root64[hash_val]->stageID;
            bmp_manager->insert_node(node4);
            ram_pool->set(root64[hash_val]->rootPointer, 0, node4);
            return SCAMPTRIE_OK;
        }
        current_node = ram_pool->get(root64[hash_val]->rootPointer);
        root_ptr = root64[hash_val];
    }
    // Step2. Record the lookup path
    thread_local static ScampTrieNode* path[64];
    thread_local static int path_len;
    path_len = 0;

    uint8_t ins_len = enable_hash ? len64 : len128;
    for (int i = 0; i <= ins_len; i += 6) {
        path[path_len++] = current_node;
        uint16_t idx = enable_hash ? get_substring_in_subtrie(prefix64, i) : get_substring_in_subtrie(prefix128, i);

        if (current_node->nodeType == TYPE4 || current_node->nodeType == TYPE5) {
            break;
        }
        if (ins_len < i + 6) { // reach the end of the prefix
            break;
        }
        bool found_child = false;
        if (current_node->nodeType == TYPE1) {
            ScampTrieNode1* node1 = static_cast<ScampTrieNode1*>(current_node);
            for (int j = 0; j < 2; ++ j) {
                if (node1->childInfo[j].stageID != STAGE_NONE && node1->childInfo[j].ip_sub6 == idx) {
                    ScampTrieNode* next_node = this->ram_pool->get(node1->childInfo[j].childPointer);
                    current_node = next_node;
                    found_child = true;
                    break;
                }
            }
        } else if (current_node->nodeType == TYPE2) {
            ScampTrieNode2* node2 = static_cast<ScampTrieNode2*>(current_node);
            for (int j = 0; j < 6; ++ j) {
                if (node2->childInfo[j].stageID != STAGE_NONE && node2->childInfo[j].ip_sub6 == idx) {
                    ScampTrieNode* next_node = this->ram_pool->get(node2->childInfo[j].childPointer);
                    current_node = next_node;
                    found_child = true;
                    break;
                }
            }
        } else if (current_node->nodeType == TYPE3) {
            ScampTrieNode3* node3 = static_cast<ScampTrieNode3*>(current_node);
            if (enable_free_child) {
                for (int j = 0; j < 3; ++j)
                    if (node3->freeChild[j].stageID != STAGE_NONE && node3->freeChild[j].ip_sub6 == idx) {
                        ScampTrieNode* next_node = this->ram_pool->get(node3->freeChild[j].childPointer);
                        current_node = next_node;
                        found_child = true;
                        break;
                    }
            }
            if (found_child) continue;
            if (node3->childBitmap & (1ULL << idx)) {
                uint32_t child_offset = count_ones_before_pos(node3->childBitmap, idx);
                ScampTrieNode* next_node = this->ram_pool->get(node3->childPointer, child_offset);
                current_node = next_node;
                found_child = true;
            }
        }
        if (!found_child) {
            break;
        }
    }
    // Step3. Calculate bmp_father_idx in the undermost node
    uint32_t bmp_father_idx = current_node->bmp; // it must be bmp of current node or one of its inner prefixes
    if (current_node->nodeType == TYPE4) {
        ScampTrieNode4* current_node4 = static_cast<ScampTrieNode4*>(current_node);
        const PrefixInfo &pInfo = current_node4->prefixInfo[0];
        if (is_A_prefix_of_B(pInfo.ip_128, pInfo.len_128, prefix128, len128))
            bmp_father_idx = pInfo.bmp_entry_idx;
    } else if (current_node->nodeType == TYPE5) {
        ScampTrieNode5* current_node5 = static_cast<ScampTrieNode5*>(current_node);
        const PrefixInfo &pInfo0 = current_node5->prefixInfo[0];
        const PrefixInfo &pInfo1 = current_node5->prefixInfo[1];
        bool match0 = is_A_prefix_of_B(pInfo0.ip_128, pInfo0.len_128, prefix128, len128);
        bool match1 = is_A_prefix_of_B(pInfo1.ip_128, pInfo1.len_128, prefix128, len128);
        if (match0 && match1) bmp_father_idx = bmp_manager->shallowest_bmp_idx2(pInfo0.bmp_entry_idx, pInfo1.bmp_entry_idx);
        else if (match0) bmp_father_idx = pInfo0.bmp_entry_idx;
        else if (match1) bmp_father_idx = pInfo1.bmp_entry_idx;
    } else { // TYPE 1,2,3,  check inner nh bitmap
        uint16_t nhBitmapPointer = current_node->get_nh_bitmap_pointer();
        uint32_t nhPointer = current_node->get_nh_pointer();
        uint64_t bitmap = nh_bitmap_bucket->get_bitmap(nhBitmapPointer);
        int base_len = (path_len-1) * 6;
        uint64_t prefix6 = enable_hash ?
            get_substring_in_subtrie(prefix64, base_len) : get_substring_in_subtrie(prefix128, base_len);
        int remain_len = ins_len - base_len;
        if (remain_len > 5) remain_len = 5;
        for (int len = remain_len; len >= 1; -- len) {
            uint64_t pos = get_nh_bitmap_pos_6(prefix6, len);
            if (bitmap & (1ULL << pos)) {
                uint32_t offset = count_ones_before_pos(bitmap, pos);
                bmp_father_idx = nh_bucket->get_bmp_idx(nhPointer, offset);
                break;
            }
        }
    }
    uint32_t bmp_current_idx = bmp_manager->insert_prefix(prefix128, len128, nh, bmp_father_idx);
    if (ins_len / 6 >= path_len || current_node->nodeType == TYPE4 || current_node->nodeType == TYPE5) { // NEW NODE or UPGRADE CURRENT NODE
        ScampTrieNode* new_node = nullptr;

        PrefixInfo prefixInfo = {prefix64, len64, static_cast<uint32_t>(nh), bmp_current_idx, prefix128, len128};
        switch (current_node->nodeType) {
            case TYPE4: {
                ScampTrieNode4* current_node4 = static_cast<ScampTrieNode4*>(current_node);
                new_node = gen_from_2pf(
                    path_len >= 2 ? path[path_len - 2]->len_128 : len_0_18_64 - 6, // path_len == 1: current_node is root
                    path_len >= 2 ? path[path_len - 2]->depth : -1,
                    current_node4->bmp,
                    current_node4->prefixInfo[0], prefixInfo);
                break;
            }
            case TYPE5: {
                ScampTrieNode5* current_node5 = static_cast<ScampTrieNode5*>(current_node);
                if (gen_from_3pf(
                    path_len >= 2 ? path[path_len - 2]->len_128 : len_0_18_64 - 6, // path_len == 1: current_node is root
                    path_len >= 2 ? path[path_len - 2]->depth : -1,
                    current_node5->bmp,
                    current_node5->prefixInfo[0], current_node5->prefixInfo[1], prefixInfo,
                    new_node
                )) return SCAMPTRIE_FAIL;
                break;
            }
            case TYPE1: {
                ScampTrieNode1* node1 = static_cast<ScampTrieNode1*>(current_node);
                if (ins_chd_1to12(node1, prefixInfo, bmp_father_idx, new_node))
                    return SCAMPTRIE_FAIL;
                break;
            }
            case TYPE2: {
                ScampTrieNode2* node2 = static_cast<ScampTrieNode2*>(current_node);
                if (ins_chd_2to23(node2, prefixInfo, bmp_father_idx, new_node))
                    return SCAMPTRIE_FAIL;
                break;
            }
            case TYPE3: {
                ScampTrieNode3* node3 = static_cast<ScampTrieNode3*>(current_node);
                if (ins_chd_3to3(node3, prefixInfo, bmp_father_idx, new_node))
                    return SCAMPTRIE_FAIL;
                break;
            }
            default: {
                msg_error("ScampTrieDynamic::insert_prefix: invalid node type");
            }
        }

        if (new_node != current_node) {// replace the node if necessary
            if (path_len == 1) { // replace root
                if (NODE_SPACE[new_node->nodeType] != NODE_SPACE[current_node->nodeType])
                    if (ram_pool->adjust_stage(
                        root_ptr->rootPointer, root_ptr->stageID,
                        higher_stage(current_node->child_highest),
                        new_node->depth,
                        new_node->nodeType
                    )) return SCAMPTRIE_FAIL;
                new_node->cur_stage = root_ptr->stageID;
                ram_pool->set(root_ptr->rootPointer, 0, new_node);
            } else { // replace a normal node
                ScampTrieNode* father_node = path[path_len - 2];

                if (father_node->nodeType == TYPE1) {
                    ScampTrieNode1* father_node1 = static_cast<ScampTrieNode1*>(father_node);
                    for (int i = 0; i < 2; ++i) {
                        ChildInfo* child_info = &father_node1->childInfo[i];
                        if (child_info->stageID != STAGE_NONE && ram_pool->get(child_info->childPointer) == current_node) {
                            if (NODE_SPACE[new_node->nodeType] != NODE_SPACE[current_node->nodeType])
                                if (ram_pool->adjust_stage(
                                    child_info->childPointer, child_info->stageID,
                                    higher_stage(current_node->child_highest),
                                    new_node->depth,
                                    new_node->nodeType
                                )) return SCAMPTRIE_FAIL;
                            new_node->cur_stage = child_info->stageID;
                            ram_pool->set(child_info->childPointer, 0, new_node);
                            break;
                        }
                    }
                } else if (father_node->nodeType == TYPE2) {
                    ScampTrieNode2* father_node2 = static_cast<ScampTrieNode2*>(father_node);
                    for (int i = 0; i < 6; ++i) {
                        ChildInfo* child_info = &father_node2->childInfo[i];
                        if (child_info->stageID != STAGE_NONE && ram_pool->get(child_info->childPointer) == current_node) {
                            if (NODE_SPACE[new_node->nodeType] != NODE_SPACE[current_node->nodeType])
                                if (ram_pool->adjust_stage(
                                    child_info->childPointer, child_info->stageID,
                                    higher_stage(current_node->child_highest),
                                    new_node->depth,
                                    new_node->nodeType
                                )) return SCAMPTRIE_FAIL;
                            new_node->cur_stage = child_info->stageID;
                            ram_pool->set(child_info->childPointer, 0, new_node);
                            break;
                        }
                    }
                } else if (father_node->nodeType == TYPE3) {
                    ScampTrieNode3* father_node3 = static_cast<ScampTrieNode3*>(father_node);
                    uint8_t child_cnt = count_ones_before_pos(father_node3->childBitmap, 64);
                    if (enable_free_child) {
                        for (int j = 0; j < 3; ++j)
                            if (father_node3->freeChild[j].stageID != STAGE_NONE && ram_pool->get(father_node3->freeChild[j].childPointer) == current_node) {
                                new_node->cur_stage = father_node3->freeChild[j].stageID;
                                ram_pool->set(father_node3->freeChild[j].childPointer, 0, new_node);
                                break;
                            }
                    }
                    for (int i = 0; i < child_cnt; ++i) {
                        if (ram_pool->get(father_node3->childPointer, i) == current_node) {
                            new_node->cur_stage = father_node3->childStage;
                            ram_pool->set(father_node3->childPointer, i, new_node);
                            break;
                        }
                    }
                } else {
                    msg_error("ScampTrieDynamic::insert_prefix: invalid father node type");
                    return SCAMPTRIE_FAIL;
                }
            }
            bmp_manager->update_node_ptr(current_node, new_node);
            delete current_node;
            path[path_len - 1] = new_node;
        }
    } else { // store prefix in current node
        uint16_t &nhBitmapPointer = current_node->get_nh_bitmap_pointer();
        uint32_t &nhPointer = current_node->get_nh_pointer();
        uint16_t nh_bitmap_pos = enable_hash ?
            get_nh_bitmap_pos_64(prefix64, len64) : get_nh_bitmap_pos_128(prefix128, len128);
        uint64_t nh_bitmap = nh_bitmap_bucket->get_bitmap(nhBitmapPointer);
        uint32_t insert_pos = count_ones_before_pos(nh_bitmap, nh_bitmap_pos);
        uint64_t new_nh_bitmap = nh_bitmap | (1ull << nh_bitmap_pos);
        nhBitmapPointer = nh_bitmap_bucket->get_bitmap_index(new_nh_bitmap);
        nh_bucket->insert(nhPointer, insert_pos, nh, bmp_current_idx);
    }

    // adjust all disordered stages on the path
    for (int i = path_len - 1; i >= 1; --i) {
        ScampTrieNode* chk_node = path[i];
        
        // stage discrepancy ends here, always check the last node
        if (i != path_len - 1 && chk_node->cur_stage > chk_node->child_highest) break;
        stage_raise_loop_cnt += 1;

        ScampTrieNode* father = path[i - 1];
        if (father->nodeType == TYPE1) {
            ScampTrieNode1* fa1 = static_cast<ScampTrieNode1*>(father);
            for (int j = 0; j < 2; ++j) {
                if (fa1->childInfo[j].stageID != STAGE_NONE && ram_pool->get(fa1->childInfo[j].childPointer) == chk_node) {
                    stage_t old_stage = fa1->childInfo[j].stageID;
                    if (ram_pool->adjust_stage(
                        fa1->childInfo[j].childPointer, fa1->childInfo[j].stageID,
                        higher_stage(chk_node->child_highest),
                        chk_node->depth,
                        chk_node->nodeType
                    )) return SCAMPTRIE_FAIL;
                    break;
                }
            }
            getmax_stage(fa1->child_highest, chk_node->cur_stage);
        } else if (father->nodeType == TYPE2) {
            ScampTrieNode2* fa2 = static_cast<ScampTrieNode2*>(father);
            for (int j = 0; j < 6; ++j) {
                if (fa2->childInfo[j].stageID != STAGE_NONE && ram_pool->get(fa2->childInfo[j].childPointer) == chk_node) {
                    stage_t old_stage = fa2->childInfo[j].stageID;
                    if (ram_pool->adjust_stage(
                        fa2->childInfo[j].childPointer, fa2->childInfo[j].stageID,
                        higher_stage(chk_node->child_highest),
                        chk_node->depth,
                        chk_node->nodeType
                    )) return SCAMPTRIE_FAIL;
                    break;
                }
            }
            getmax_stage(fa2->child_highest, chk_node->cur_stage);
        } else if (father->nodeType == TYPE3) {
            ScampTrieNode3* fa3 = static_cast<ScampTrieNode3*>(father);
            if (enable_free_child) {
                std::vector<ExtraChildInfo> no_child;
                rearrange_free_child(fa3, no_child);
            } else {
                getmax_stage(fa3->child_highest, higher_stage(chk_node->child_highest));
                stage_t old_stage = fa3->childStage;
                // all children of TYPE3 consume a full block (TYPE3)
                if (ram_pool->adjust_stage(
                    fa3->childPointer, fa3->childStage,
                    fa3->child_highest,
                    chk_node->depth,
                    TYPE3
                )) return SCAMPTRIE_FAIL;
                fa3->child_highest = fa3->childStage;
            }
        }
    }

    ScampTrieNode* root_node = path[0];
    if (root_node->cur_stage <= root_node->child_highest && root_node->child_highest != STAGE_DEFAULT) {
        if (ram_pool->adjust_stage(
            root_ptr->rootPointer, root_ptr->stageID,
            higher_stage(root_node->child_highest),
            root_node->depth,
            root_node->nodeType
        )) return SCAMPTRIE_FAIL;
        root_node->cur_stage = root_ptr->stageID;
        ram_pool->set(root_ptr->rootPointer, 0, root_node);
    }
    return SCAMPTRIE_OK;
}

/////// PRIVATE FUNCTIONS //////

uint32_t ScampTrieDynamic::ins_chd_1to12(ScampTrieNode1* hang_node1, PrefixInfo pInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node) {
    uint8_t empty_slot_id = 0;
    while (empty_slot_id < 2 && hang_node1->childInfo[empty_slot_id].stageID != STAGE_NONE) empty_slot_id++;
    if (empty_slot_id < 2) { // at least 1 empty slot
        ChildInfo *ch_info = &hang_node1->childInfo[empty_slot_id];
        ch_info->ip_sub6 = enable_hash ? 
            get_substring_in_subtrie(pInfo.ip_64, hang_node1->depth * 6) :
            get_substring_in_subtrie(pInfo.ip_128, hang_node1->depth * 6);
        ScampTrieNode4* child_node4 = gen_from_1pf(
            hang_node1->len_128, hang_node1->depth, pInfo_bmp_father_idx,
            pInfo
        );
        if (ram_pool->alloc(
            1, TYPE4,
            higher_stage(child_node4->child_highest), child_node4->depth,
            ch_info->childPointer, ch_info->stageID
        )) return SCAMPTRIE_FAIL;
        child_node4->cur_stage = ch_info->stageID;
        getmax_stage(hang_node1->child_highest, child_node4->cur_stage);
        bmp_manager->insert_node(child_node4);
        ram_pool->set(ch_info->childPointer, 0, child_node4);
        ret_node = hang_node1;
        return SCAMPTRIE_OK;
    } else { // 2 full slots, upgrade to Type2
        ScampTrieNode2* upgrade_node2 = new ScampTrieNode2();
        upgrade_node2->nodeType = TYPE2;
        upgrade_node2->ip_128 = pInfo.ip_128;
        upgrade_node2->len_128 = hang_node1->len_128;
        upgrade_node2->depth = hang_node1->depth;
        upgrade_node2->bmp = hang_node1->bmp;
        upgrade_node2->child_highest = hang_node1->child_highest;
        
        upgrade_node2->nhBitmapPointer = hang_node1->nhBitmapPointer;
        upgrade_node2->nhPointer = hang_node1->nhPointer;
        for (int i = 0; i < 2; ++i) {
            upgrade_node2->childInfo[i].ip_sub6 = hang_node1->childInfo[i].ip_sub6;
            upgrade_node2->childInfo[i].stageID = hang_node1->childInfo[i].stageID;
            upgrade_node2->childInfo[i].childPointer = hang_node1->childInfo[i].childPointer;
        }

        ChildInfo *ch_info2 = &upgrade_node2->childInfo[2];
        ch_info2->ip_sub6 = enable_hash ?
            get_substring_in_subtrie(pInfo.ip_64, upgrade_node2->depth * 6) :
            get_substring_in_subtrie(pInfo.ip_128, upgrade_node2->depth * 6);
        ScampTrieNode4* child_node4 = gen_from_1pf(upgrade_node2->len_128, upgrade_node2->depth, pInfo_bmp_father_idx, pInfo);
        if (ram_pool->alloc(
            1, TYPE4,
            higher_stage(child_node4->child_highest), child_node4->depth,
            ch_info2->childPointer, ch_info2->stageID
        )) return SCAMPTRIE_FAIL;
        child_node4->cur_stage = ch_info2->stageID;
        getmax_stage(upgrade_node2->child_highest, child_node4->cur_stage);
        bmp_manager->insert_node(child_node4);
        ram_pool->set(ch_info2->childPointer, 0, child_node4);
        ret_node = upgrade_node2;
        return SCAMPTRIE_OK;
    }
}

uint32_t ScampTrieDynamic::ins_chd_2to23(ScampTrieNode2* hang_node2, PrefixInfo pInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node) {
    uint8_t empty_slot_id = 0;
    while (empty_slot_id < 6 && hang_node2->childInfo[empty_slot_id].stageID != STAGE_NONE) empty_slot_id++;
    if (empty_slot_id < 6) { // at least 1 empty slot
        ChildInfo *ch_info = &hang_node2->childInfo[empty_slot_id];
        ch_info->ip_sub6 = enable_hash ?
            get_substring_in_subtrie(pInfo.ip_64, hang_node2->depth * 6) :
            get_substring_in_subtrie(pInfo.ip_128, hang_node2->depth * 6);
        ScampTrieNode4* child_node4 = gen_from_1pf(hang_node2->len_128, hang_node2->depth, pInfo_bmp_father_idx, pInfo);
        if (ram_pool->alloc(
            1, TYPE4,
            higher_stage(child_node4->child_highest), child_node4->depth,
            ch_info->childPointer, ch_info->stageID
        )) return SCAMPTRIE_FAIL;
        child_node4->cur_stage = ch_info->stageID;
        getmax_stage(hang_node2->child_highest, child_node4->cur_stage);
        bmp_manager->insert_node(child_node4);
        ram_pool->set(ch_info->childPointer, 0, child_node4);
        ret_node = hang_node2;
        return SCAMPTRIE_OK;
    } else {
        ScampTrieNode3* upgrade_node3 = new ScampTrieNode3();
        upgrade_node3->nodeType = TYPE3;
        upgrade_node3->ip_128 = pInfo.ip_128;
        upgrade_node3->len_128 = hang_node2->len_128;
        upgrade_node3->depth = hang_node2->depth;
        upgrade_node3->bmp = hang_node2->bmp;
        upgrade_node3->child_highest = STAGE_DEFAULT;

        upgrade_node3->nhBitmapPointer = hang_node2->nhBitmapPointer;
        upgrade_node3->nhPointer = hang_node2->nhPointer;
        
        upgrade_node3->childBitmap = 0;
        uint8_t sub6 = enable_hash ? 
            get_substring_in_subtrie(pInfo.ip_64, upgrade_node3->depth * 6) :
            get_substring_in_subtrie(pInfo.ip_128, upgrade_node3->depth * 6);
        upgrade_node3->childBitmap |= (1ull << sub6);
        if (enable_free_child) { // store as 7 continuous or 6 + 1
            std::vector<ExtraChildInfo> extra_child;
            extra_child.clear();
            ScampTrieNode4* new_child4 = gen_from_1pf(upgrade_node3->len_128, upgrade_node3->depth, pInfo_bmp_father_idx, pInfo);
            bmp_manager->insert_node(new_child4);
            extra_child.push_back(ExtraChildInfo{sub6, new_child4});
            for (int i = 0; i < 6; ++i) {
                ScampTrieNode* child_node = ram_pool->get(hang_node2->childInfo[i].childPointer);
                extra_child.push_back(ExtraChildInfo{hang_node2->childInfo[i].ip_sub6, child_node});
                ram_pool->free(hang_node2->childInfo[i].childPointer, hang_node2->childInfo[i].stageID);
            }
            upgrade_node3->childBitmap = 0; // default as no children
            rearrange_free_child(upgrade_node3, extra_child);
        } else { // store as 7 continuous
            for (int i = 0; i < 6; ++i)
                upgrade_node3->childBitmap |= (1ull << hang_node2->childInfo[i].ip_sub6);
            
            uint32_t child_offset = count_ones_before_pos(upgrade_node3->childBitmap, sub6);
            ScampTrieNode4* child_node4 = gen_from_1pf(upgrade_node3->len_128, upgrade_node3->depth, pInfo_bmp_father_idx, pInfo);
            getmax_stage(upgrade_node3->child_highest, higher_stage(child_node4->child_highest));
            for (int i = 0; i < 6; ++i) {
                ScampTrieNode* child_node = ram_pool->get(hang_node2->childInfo[i].childPointer);
                getmax_stage(upgrade_node3->child_highest, higher_stage(child_node->child_highest));
            }
            if (ram_pool->alloc(
                6+1, TYPE3,
                upgrade_node3->child_highest, child_node4->depth,
                upgrade_node3->childPointer, upgrade_node3->childStage
            )) return SCAMPTRIE_FAIL;
            child_node4->cur_stage = upgrade_node3->childStage;
            // otherwise, its father will use theoretical stage(highest_stage+1) instead of the real stage(childStage)
            upgrade_node3->child_highest = upgrade_node3->childStage;
            for (int i = 0; i < 6; ++i) {
                child_offset = count_ones_before_pos(upgrade_node3->childBitmap, hang_node2->childInfo[i].ip_sub6);
                ScampTrieNode* child_node = ram_pool->get(hang_node2->childInfo[i].childPointer);
                child_node->cur_stage = upgrade_node3->childStage;
                ram_pool->set(upgrade_node3->childPointer, child_offset, child_node);
                ram_pool->free(hang_node2->childInfo[i].childPointer, hang_node2->childInfo[i].stageID);
            }
            
            child_offset = count_ones_before_pos(upgrade_node3->childBitmap, sub6);
            bmp_manager->insert_node(child_node4);
            ram_pool->set(upgrade_node3->childPointer, child_offset, child_node4);
        }
        ret_node = upgrade_node3;
        return SCAMPTRIE_OK;
    }
}

uint32_t ScampTrieDynamic::ins_chd_3to3(ScampTrieNode3* hang_node3, PrefixInfo prefixInfo, uint32_t pInfo_bmp_father_idx, ScampTrieNode* &ret_node) {
    uint8_t sub6 = enable_hash ?
        get_substring_in_subtrie(prefixInfo.ip_64, hang_node3->depth * 6) :
        get_substring_in_subtrie(prefixInfo.ip_128, hang_node3->depth * 6);
    if (enable_free_child) {
        ScampTrieNode4* new_child4 = gen_from_1pf(hang_node3->len_128, hang_node3->depth, pInfo_bmp_father_idx, prefixInfo);
        bmp_manager->insert_node(new_child4);
        bool found_position = false;
        for (int i = 0; i < 3; ++ i) {
            if (hang_node3->freeChild[i].stageID == STAGE_NONE) {
                hang_node3->freeChild[i].ip_sub6 = sub6;
                if (ram_pool->alloc(
                    1, TYPE4,
                    higher_stage(new_child4->child_highest), new_child4->depth,
                    hang_node3->freeChild[i].childPointer, hang_node3->freeChild[i].stageID
                )) return SCAMPTRIE_FAIL;
                new_child4->cur_stage = hang_node3->freeChild[i].stageID;
                getmax_stage(hang_node3->child_highest, new_child4->cur_stage);
                ram_pool->set(hang_node3->freeChild[i].childPointer, 0, new_child4);
                ret_node = hang_node3;
                found_position = true;
                break;
            }
        }
        if (!found_position) { // no empty slot, need to rearrange all children
            std::vector<ExtraChildInfo> extra_child;
            extra_child.push_back(ExtraChildInfo{sub6, new_child4});
            rearrange_free_child(hang_node3, extra_child);
        }
    } else {
        hang_node3->childBitmap |= (1ull << sub6);
        uint8_t insert_pos = count_ones_before_pos(hang_node3->childBitmap, sub6);
        ScampTrieNode4* child_node4 = gen_from_1pf(hang_node3->len_128, hang_node3->depth, pInfo_bmp_father_idx, prefixInfo);
        getmax_stage(hang_node3->child_highest, higher_stage(child_node4->child_highest));
        stage_t origin_stage = hang_node3->childStage;
        if (ram_pool->insert(
            hang_node3->childPointer, hang_node3->childStage,
            insert_pos, child_node4,
            hang_node3->child_highest, child_node4->depth
        )) return SCAMPTRIE_FAIL;
        hang_node3->child_highest = hang_node3->childStage;

        bmp_manager->insert_node(child_node4);
    }
    ret_node = hang_node3;
    return SCAMPTRIE_OK;
}

uint32_t ScampTrieDynamic::rearrange_free_child(ScampTrieNode3* fa, std::vector<ExtraChildInfo> &extra_child) {
    // Reserve to avoid repeated realloc (profile: 911597 _M_realloc_insert for ExtraChildInfo)
    extra_child.reserve(64);
    // Step1. Collect all children, including 3 free children and N children in ChildPointer
    for (int i = 0; i < 3; ++ i) {
        if (fa->freeChild[i].stageID != STAGE_NONE) {
            ScampTrieNode* child_node = ram_pool->get(fa->freeChild[i].childPointer);
            extra_child.push_back(ExtraChildInfo{fa->freeChild[i].ip_sub6, child_node});
            ram_pool->free(fa->freeChild[i].childPointer, fa->freeChild[i].stageID);
            fa->freeChild[i].stageID = STAGE_NONE;
        }
    }
    int c_child_cnt = 0;
    for (uint8_t sub6 = 0; sub6 < 64; ++ sub6) {
        if ((1llu << sub6) & fa->childBitmap) {
            ScampTrieNode* child_node = ram_pool->get(fa->childPointer, c_child_cnt);
            extra_child.push_back(ExtraChildInfo{sub6, child_node});
            c_child_cnt += 1;
        }
    }
    if (c_child_cnt > 0) ram_pool->free(fa->childPointer, fa->childStage);

    // Step2. Find the highest child, and extract it to freeChild if it's >= 3 level higher than others
    stage_t stage_rk1 = 0, itr_rk1, stage_rk2 = 0;
    for (int i = 0; i < extra_child.size(); ++ i) {
        stage_t s = higher_stage(extra_child[i].node->child_highest);
        if (s >= stage_rk1) {
            stage_rk2 = stage_rk1;
            stage_rk1 = s;
            itr_rk1 = i;
        } else if (s >= stage_rk2) {
            stage_rk2 = s;
        }
    }
    fa->child_highest = STAGE_DEFAULT;

    if (stage_rk1 - stage_rk2 >= 1) {
        fa->freeChild[0].ip_sub6 = extra_child[itr_rk1].ip_sub6;
        if (ram_pool->alloc(
            1, extra_child[itr_rk1].node->nodeType,
            stage_rk1, extra_child[itr_rk1].node->depth,
            fa->freeChild[0].childPointer, fa->freeChild[0].stageID
        )) return SCAMPTRIE_FAIL;
        extra_child[itr_rk1].node->cur_stage = fa->freeChild[0].stageID;
        getmax_stage(fa->child_highest, fa->freeChild[0].stageID);
        ram_pool->set(fa->freeChild[0].childPointer, 0, extra_child[itr_rk1].node);
        extra_child.erase(extra_child.begin() + itr_rk1);
        stage_rk1 = stage_rk2;
    }

    // Step3. Store the remaining children to ChildPointer
    std::sort(extra_child.begin(), extra_child.end());
    fa->childBitmap = 0;
    for (int i = 0; i < extra_child.size(); ++ i)
        fa->childBitmap |= (1ull << extra_child[i].ip_sub6);
    if (ram_pool->alloc(
        extra_child.size(), TYPE3,
        stage_rk1, fa->depth + 1,
        fa->childPointer, fa->childStage
    )) return SCAMPTRIE_FAIL;
    for (int i = 0; i < extra_child.size(); ++ i) {
        ram_pool->set(fa->childPointer, i, extra_child[i].node);
        extra_child[i].node->cur_stage = fa->childStage;
    }
    getmax_stage(fa->child_highest, fa->childStage);
    return SCAMPTRIE_OK;
}

#define GET_LEN(pInfo) (pInfo.len_64)

ScampTrieNode4* ScampTrieDynamic::gen_from_1pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo) {
    ScampTrieNode4* upgrade_node4 = new ScampTrieNode4();
    upgrade_node4->nodeType = TYPE4;
    upgrade_node4->ip_128 = pInfo.ip_128;
    upgrade_node4->len_128 = fa_len128 + 6;
    upgrade_node4->depth = fa_depth + 1;
    upgrade_node4->bmp = node_bmp;
    upgrade_node4->cur_stage = STAGE_DEFAULT;

    if (GET_LEN(pInfo) == upgrade_node4->depth * 6) // the new prefix could also be the node bmp
        upgrade_node4->bmp = pInfo.bmp_entry_idx;

    upgrade_node4->prefixInfo[0] = pInfo;
    return upgrade_node4;
}

ScampTrieNode5* ScampTrieDynamic::gen_from_2pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo0, PrefixInfo pInfo1) {
    ScampTrieNode5* upgrade_node5 = new ScampTrieNode5();
    upgrade_node5->nodeType = TYPE5;
    upgrade_node5->ip_128 = pInfo0.ip_128;
    upgrade_node5->len_128 = fa_len128 + 6;
    upgrade_node5->depth = fa_depth + 1;
    upgrade_node5->bmp = node_bmp;
    upgrade_node5->cur_stage = STAGE_DEFAULT;

    if (GET_LEN(pInfo0) == upgrade_node5->depth * 6) // the new prefix could also be the node bmp
        upgrade_node5->bmp = pInfo0.bmp_entry_idx;
    if (GET_LEN(pInfo1) == upgrade_node5->depth * 6)
        upgrade_node5->bmp = pInfo1.bmp_entry_idx;

    upgrade_node5->prefixInfo[0] = pInfo0;
    upgrade_node5->prefixInfo[1] = pInfo1;
    return upgrade_node5;
}

uint32_t ScampTrieDynamic::gen_from_3pf(uint8_t fa_len128, uint8_t fa_depth, uint32_t node_bmp, PrefixInfo pInfo0, PrefixInfo pInfo1, PrefixInfo pInfo2, ScampTrieNode* &ret_node) {
    uint8_t base_len = (fa_depth + 1) * 6;
    if (GET_LEN(pInfo0) == base_len) node_bmp = pInfo0.bmp_entry_idx;
    if (GET_LEN(pInfo1) == base_len) node_bmp = pInfo1.bmp_entry_idx;
    if (GET_LEN(pInfo2) == base_len) node_bmp = pInfo2.bmp_entry_idx;
    
    PrefixInfo inner_pfx[3], outer_pfx[3];
    int inner_pfx_cnt = 0, outer_pfx_cnt = 0;
    inner_pfx_cnt = 0;
    outer_pfx_cnt = 0;
    if (GET_LEN(pInfo0) < base_len + 6) inner_pfx[inner_pfx_cnt++] = pInfo0;
    else                                outer_pfx[outer_pfx_cnt++] = pInfo0;
    if (GET_LEN(pInfo1) < base_len + 6) inner_pfx[inner_pfx_cnt++] = pInfo1;
    else                                outer_pfx[outer_pfx_cnt++] = pInfo1;
    if (GET_LEN(pInfo2) < base_len + 6) inner_pfx[inner_pfx_cnt++] = pInfo2;
    else                                outer_pfx[outer_pfx_cnt++] = pInfo2;

    uint8_t sub6_p[3];
    for (int i = 0; i < outer_pfx_cnt; ++i)
        sub6_p[i] = enable_hash ?
            get_substring_in_subtrie(outer_pfx[i].ip_64, base_len) :
            get_substring_in_subtrie(outer_pfx[i].ip_128, base_len);

    // All 3 prefixes enter different children, this node should be Type2
    if (outer_pfx_cnt == 3 && sub6_p[0] != sub6_p[1] && sub6_p[0] != sub6_p[2] && sub6_p[1] != sub6_p[2]) {
        ScampTrieNode2* upgrade_node2 = new ScampTrieNode2();
        upgrade_node2->nodeType = TYPE2;
        upgrade_node2->ip_128 = pInfo0.ip_128;
        upgrade_node2->len_128 = fa_len128 + 6;
        upgrade_node2->depth = fa_depth + 1;
        upgrade_node2->bmp = node_bmp;
        upgrade_node2->child_highest = STAGE_DEFAULT;
        
        upgrade_node2->nhBitmapPointer = nh_bitmap_bucket->get_bitmap_index(0); // No Inner Prefix
        upgrade_node2->nhPointer = nh_bucket->alloc();
        for (int i = 0; i < outer_pfx_cnt; ++i) {
            ChildInfo *ch_info = &upgrade_node2->childInfo[i];
            ch_info->ip_sub6 = sub6_p[i];
            ScampTrieNode4* ch_node4 = gen_from_1pf(
                upgrade_node2->len_128, upgrade_node2->depth, node_bmp,
                outer_pfx[i]
            );
            if (ram_pool->alloc(
                1, TYPE4,
                higher_stage(ch_node4->child_highest), ch_node4->depth,
                ch_info->childPointer, ch_info->stageID
            )) return SCAMPTRIE_FAIL;
            ch_node4->cur_stage = ch_info->stageID;
            getmax_stage(upgrade_node2->child_highest, ch_node4->cur_stage);
            bmp_manager->insert_node(ch_node4);
            ram_pool->set(ch_info->childPointer, 0, ch_node4);
        }
        ret_node = upgrade_node2;
        return SCAMPTRIE_OK;
    }

    // All the cases below are Type1
    ScampTrieNode1* upgrade_node1 = new ScampTrieNode1();
    upgrade_node1->nodeType = TYPE1;
    upgrade_node1->ip_128 = pInfo0.ip_128;
    upgrade_node1->len_128 = fa_len128 + 6;
    upgrade_node1->depth = fa_depth + 1;
    upgrade_node1->bmp = node_bmp;
    upgrade_node1->cur_stage = STAGE_DEFAULT;
    
    // generate NH Bitmap & NH Pointer
    upgrade_node1->nhPointer = nh_bucket->alloc();
    uint64_t nh_bitmap = 0;
    for (int i = 0; i < inner_pfx_cnt; ++i) {
        uint16_t nh_bitmap_pos = enable_hash ?
            get_nh_bitmap_pos_64(inner_pfx[i].ip_64, inner_pfx[i].len_64) :
            get_nh_bitmap_pos_128(inner_pfx[i].ip_128, inner_pfx[i].len_128);
        nh_bitmap |= (1ull << nh_bitmap_pos);
        uint32_t insert_pos = count_ones_before_pos(nh_bitmap, nh_bitmap_pos);
        nh_bucket->insert(upgrade_node1->nhPointer, insert_pos, inner_pfx[i].nexthop, inner_pfx[i].bmp_entry_idx);
    }
    upgrade_node1->nhBitmapPointer = nh_bitmap_bucket->get_bitmap_index(nh_bitmap);

    if (outer_pfx_cnt == 3) {
        // 3 -> (3) : the child will be generated recursively
        if (sub6_p[0] == sub6_p[1] && sub6_p[0] == sub6_p[2]) {
            ChildInfo *ch_info = &upgrade_node1->childInfo[0];
            ch_info->ip_sub6 = sub6_p[0];
            ScampTrieNode* ch_node3;
            if (gen_from_3pf(
                upgrade_node1->len_128, upgrade_node1->depth, node_bmp,
                outer_pfx[0], outer_pfx[1], outer_pfx[2], ch_node3
            )) return SCAMPTRIE_FAIL;
            if (ram_pool->alloc(
                1, ch_node3->nodeType,
                higher_stage(ch_node3->child_highest), ch_node3->depth,
                ch_info->childPointer, ch_info->stageID
            )) return SCAMPTRIE_FAIL;
            ch_node3->cur_stage = ch_info->stageID;
            getmax_stage(upgrade_node1->child_highest, ch_node3->cur_stage);
            bmp_manager->insert_node(ch_node3);
            ram_pool->set(ch_info->childPointer, 0, ch_node3);
            ret_node = upgrade_node1;
            return SCAMPTRIE_OK;
        }
        // 3 -> (2, 1)
        if (sub6_p[0] == sub6_p[1] || sub6_p[0] == sub6_p[2] || sub6_p[1] == sub6_p[2]) {
            ChildInfo *ch_info0 = &upgrade_node1->childInfo[0];
            ChildInfo *ch_info1 = &upgrade_node1->childInfo[1];
            uint32_t i, j; // (ik)(j)
            if      (sub6_p[0] == sub6_p[1]) i = 0, j = 2;
            else if (sub6_p[0] == sub6_p[2]) i = 0, j = 1;
            else if (sub6_p[1] == sub6_p[2]) i = 1, j = 0;
            else msg_error("gen_from_3pf: 3 prefixes enter same child node");
            uint32_t k = 3 - i - j;
            upgrade_node1->childInfo[0].ip_sub6 = sub6_p[j]; // j
            upgrade_node1->childInfo[1].ip_sub6 = sub6_p[i]; // i, k
            ScampTrieNode4* child1 = gen_from_1pf(
                upgrade_node1->len_128, upgrade_node1->depth, node_bmp,
                outer_pfx[j]
            );
            ScampTrieNode5* child2 = gen_from_2pf(
                upgrade_node1->len_128, upgrade_node1->depth, node_bmp,
                outer_pfx[i], outer_pfx[k]
            );
            if (ram_pool->alloc(
                1, TYPE4,
                higher_stage(child1->child_highest), child1->depth,
                ch_info0->childPointer, ch_info0->stageID
            )) return SCAMPTRIE_FAIL;
            if (ram_pool->alloc(
                1, TYPE5,
                higher_stage(child2->child_highest), child2->depth,
                ch_info1->childPointer, ch_info1->stageID
            )) return SCAMPTRIE_FAIL;
            child1->cur_stage = ch_info0->stageID;
            child2->cur_stage = ch_info1->stageID;
            getmax_stage(upgrade_node1->child_highest, child1->cur_stage);
            getmax_stage(upgrade_node1->child_highest, child2->cur_stage);
            bmp_manager->insert_node(child1);
            bmp_manager->insert_node(child2);
            ram_pool->set(ch_info0->childPointer, 0, child1);
            ram_pool->set(ch_info1->childPointer, 0, child2);
            ret_node = upgrade_node1;
            return SCAMPTRIE_OK;
        }
    }

    uint32_t outer_bmp[3];
    for (int i = 0; i < outer_pfx_cnt; ++i) {
        outer_bmp[i] = node_bmp;
        uint8_t longest_len = 0;
        for (int j = 0; j < inner_pfx_cnt; ++j)
            if (inner_pfx[j].len_128 >= longest_len &&
                is_A_prefix_of_B(inner_pfx[j].ip_128, inner_pfx[j].len_128, outer_pfx[i].ip_128, outer_pfx[i].len_128)) {
                longest_len = inner_pfx[j].len_128;
                outer_bmp[i] = inner_pfx[j].bmp_entry_idx;
            }
    }

    // 2 -> (2)
    if (outer_pfx_cnt == 2 && sub6_p[0] == sub6_p[1]) {
        ChildInfo *ch_info = &upgrade_node1->childInfo[0];
        ch_info->ip_sub6 = sub6_p[0];
        uint32_t child_bmp = bmp_manager->shallowest_bmp_idx2(outer_bmp[0], outer_bmp[1]);
        ScampTrieNode5* ch_node5 = gen_from_2pf(
            upgrade_node1->len_128, upgrade_node1->depth, child_bmp,
            outer_pfx[0], outer_pfx[1]
        );
        if (ram_pool->alloc(
            1, TYPE5,
            higher_stage(ch_node5->child_highest), ch_node5->depth,
            ch_info->childPointer, ch_info->stageID
        )) return SCAMPTRIE_FAIL;
        ch_node5->cur_stage = ch_info->stageID;
        getmax_stage(upgrade_node1->child_highest, ch_node5->cur_stage);
        bmp_manager->insert_node(ch_node5);
        ram_pool->set(ch_info->childPointer, 0, ch_node5);
        ret_node = upgrade_node1;
        return SCAMPTRIE_OK;
    }

    // (1) or (1, 1) or (1, 1, 1)
    for (int i = 0; i < outer_pfx_cnt; ++ i) {
        ChildInfo *ch_info = &upgrade_node1->childInfo[i];
        ch_info->ip_sub6 = sub6_p[i];
        ScampTrieNode4* ch_node4 = gen_from_1pf(
            upgrade_node1->len_128, upgrade_node1->depth, outer_bmp[i],
            outer_pfx[i]
        );
        if (ram_pool->alloc(
            1, TYPE4,
            higher_stage(ch_node4->child_highest), ch_node4->depth,
            ch_info->childPointer, ch_info->stageID
        )) return SCAMPTRIE_FAIL;
        ch_node4->cur_stage = ch_info->stageID;
        getmax_stage(upgrade_node1->child_highest, ch_node4->cur_stage);
        bmp_manager->insert_node(ch_node4);
        ram_pool->set(ch_info->childPointer, 0, ch_node4);
    }
    ret_node = upgrade_node1;
    return SCAMPTRIE_OK;
}

#undef GET_LEN