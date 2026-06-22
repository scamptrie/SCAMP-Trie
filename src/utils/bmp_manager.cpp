#include "bmp_manager.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

inline bool less_than(ip6_t a_ip, uint8_t a_len, ip6_t b_ip, uint8_t b_len) {
    if (a_ip.hi == b_ip.hi && a_ip.lo == b_ip.lo && a_len == b_len) return false; // equal
    if (is_A_prefix_of_B(a_ip, a_len, b_ip, b_len)) return true;
    if (is_A_prefix_of_B(b_ip, b_len, a_ip, a_len)) return false;
    return a_ip.hi != b_ip.hi ? a_ip.hi < b_ip.hi : a_ip.lo < b_ip.lo;
}

template<typename Container, typename Compare>
int binary_search_pos(const Container& container, const typename Container::value_type& target, Compare comp) {
    int l = 0, r = container.size() - 1, pos = r + 1;
    while (l <= r) {
        int mid = (l + r) / 2;
        if (comp(container[mid], target)) l = mid + 1;
        else r = mid - 1, pos = mid;
    }
    return pos;
}

struct BMPEntryLessThan { bool operator()(BMPEntry* a, BMPEntry* b) const; };
struct BMPEntryIsPrefixOf { bool operator()(BMPEntry* a, BMPEntry* b) const; };

struct ScampTrieNodeLessThan { bool operator()(ScampTrieNode* a, ScampTrieNode* b) const; };
struct ScampTrieNodeIsPrefixOf { bool operator()(ScampTrieNode* a, ScampTrieNode* b) const; };

bool BMPEntryLessThan::operator()(BMPEntry* a, BMPEntry* b) const { return less_than(a->ip, a->len, b->ip, b->len); }
bool BMPEntryIsPrefixOf::operator()(BMPEntry* a, BMPEntry* b) const { return is_A_prefix_of_B(a->ip, a->len, b->ip, b->len); }
bool ScampTrieNodeLessThan::operator()(ScampTrieNode* a, ScampTrieNode* b) const {
    return less_than(a->ip_128, a->len_128, b->ip_128, b->len_128);
}
bool ScampTrieNodeIsPrefixOf::operator()(ScampTrieNode* a, ScampTrieNode* b) const {
    return is_A_prefix_of_B(a->ip_128, a->len_128, b->ip_128, b->len_128);
}

BMPManager::BMPManager() {
    for (int i = 0; i < BMP_ENTRIES_NUM; ++i)
        bmp_entries[i] = nullptr;
    bmp_entries[0] = new BMPEntry();
    bmp_entries_size = 1; // 0 is virtual root
}

BMPManager::~BMPManager() {
    for (int i = 0; i < BMP_ENTRIES_NUM; ++i)
        if (bmp_entries[i] != nullptr)
            delete bmp_entries[i];
}

nexthop_t BMPManager::get_bmp_nexthop(uint32_t idx) {
    if (idx >= bmp_entries_size) {
        printf("[ERROR] BMPManager::get_bmp_nexthop - idx %d >= bmp_entries_size %d\n", idx, bmp_entries_size);
        exit(1);
    }
    if (idx >= BMP_ENTRIES_NUM) {
        printf("[ERROR] BMPManager::get_bmp_nexthop - idx %d >= BMP_ENTRIES_NUM %d\n", idx, BMP_ENTRIES_NUM);
        exit(1);
    }
    if (bmp_entries[idx] == nullptr) {
        printf("[ERROR] BMPManager::get_bmp_nexthop - bmp_entries[%d] is nullptr\n", idx);
        exit(1);
    }
    return bmp_entries[idx]->nh;
}

uint32_t BMPManager::shallowest_bmp_idx2(uint32_t i0, uint32_t i1) {
    if (i0 >= bmp_entries_size || i1 >= bmp_entries_size)
        msg_error("BMPManager::shallowest_bmp_idx2: Index out of range");
    return bmp_entries[i0]->len < bmp_entries[i1]->len ? i0 : i1;
}

uint32_t BMPManager::shallowest_bmp_idx3(uint32_t i0, uint32_t i1, uint32_t i2) {
    if (i0 >= bmp_entries_size || i1 >= bmp_entries_size || i2 >= bmp_entries_size)
        msg_error("BMPManager::shallowest_bmp_idx3: Index out of range");
    return shallowest_bmp_idx2(shallowest_bmp_idx2(i0, i1), i2);
}

uint32_t BMPManager::insert_prefix(ip6_t ip, uint8_t len128, nexthop_t nh, uint32_t father_idx) {
    bool DTAG = false;
    if (father_idx >= bmp_entries_size)
        msg_error("BMPManager::insert_prefix: father_idx out of range");

    if (DTAG) printf("[DEBUG] BMPManager::insert_prefix call, father_idx: %d\n", father_idx);
    
    BMPEntry* father_entry = bmp_entries[father_idx];
    if (father_entry == nullptr) {
        printf("[ERROR] BMPManager::insert_prefix, father_entry is nullptr!\n");
        exit(1);
    }

    uint32_t new_entry_idx = bmp_entries_size++;
    if (new_entry_idx >= BMP_ENTRIES_NUM)
        msg_error("BMPManager::insert_prefix: ENTRIES FULL!");

    bmp_entries[new_entry_idx] = new BMPEntry();
    BMPEntry* new_entry = bmp_entries[new_entry_idx];
    new_entry->ip = ip;
    new_entry->len = len128;
    new_entry->nh = nh;
    if (DTAG) printf("[DEBUG] BMPManager::insert_prefix, new_entry_idx: %d\n", new_entry_idx);
    // Reserve hints for block lists.
    new_entry->child_list.reserve_hint(4);
    new_entry->nodes_list.reserve_hint(8);

    // search all children(Other Prefixes) of father_entry
    BMPEntryBlockList& father_child_list = father_entry->child_list;
    int pos = static_cast<int>(father_child_list.lower_bound_index(new_entry, BMPEntryLessThan()));

    int l_bound = pos, r_bound = l_bound;
    const int child_size = static_cast<int>(father_child_list.size());
    r_bound = static_cast<int>(father_child_list.scan_while(static_cast<size_t>(l_bound), [&](BMPEntry* x) {
        return BMPEntryIsPrefixOf()(new_entry, x);
    }));

    const int insert_pos_child = l_bound;
    if (r_bound > l_bound) {
        std::vector<BMPEntry*> moved_children;
        father_child_list.copy_range_to(static_cast<size_t>(l_bound), static_cast<size_t>(r_bound), moved_children);
        new_entry->child_list.assign_from_vector(moved_children);
        father_child_list.erase_range(static_cast<size_t>(l_bound), static_cast<size_t>(r_bound));
    }

    // search all nodes(ScampTrieNode) of father_entry
    ScampTrieNode tmp_node;
    tmp_node.ip_128 = ip;
    tmp_node.len_128 = len128;
    
    ScampTrieNodeBlockList& father_nodes_list = father_entry->nodes_list;
    pos = static_cast<int>(father_nodes_list.lower_bound_index(&tmp_node, ScampTrieNodeLessThan()));

    l_bound = pos, r_bound = l_bound;
    r_bound = static_cast<int>(father_nodes_list.scan_while(static_cast<size_t>(l_bound), [&](ScampTrieNode* x) {
        return ScampTrieNodeIsPrefixOf()(&tmp_node, x);
    }));
    for (int i = l_bound; i < r_bound; ++i) {
        father_nodes_list.at(static_cast<size_t>(i))->bmp = new_entry_idx;
    }

    if (r_bound > l_bound) {
        std::vector<ScampTrieNode*> moved_nodes;
        father_nodes_list.copy_range_to(static_cast<size_t>(l_bound), static_cast<size_t>(r_bound), moved_nodes);
        new_entry->nodes_list.assign_from_vector(moved_nodes);
        father_nodes_list.erase_range(static_cast<size_t>(l_bound), static_cast<size_t>(r_bound));
    }

    // insert new_entry to father_entry->child_list at saved position
    father_child_list.insert_at(static_cast<size_t>(insert_pos_child), new_entry);

    new_entry->father = father_entry;
    return new_entry_idx;
}


void BMPManager::insert_node(ScampTrieNode* new_node) {
    mask_address(new_node->ip_128, new_node->len_128);
    uint32_t bmp_idx = new_node->bmp;
    if (bmp_idx >= bmp_entries_size)
        msg_error("BMPManager::insert_node: bmp_idx out of range");
    
    BMPEntry* new_entry = bmp_entries[bmp_idx];
    int pos = static_cast<int>(new_entry->nodes_list.lower_bound_index(new_node, ScampTrieNodeLessThan()));
    new_entry->nodes_list.insert_at(static_cast<size_t>(pos), new_node);
}

void BMPManager::update_node_ptr(ScampTrieNode* old_node, ScampTrieNode* new_node) {
    mask_address(new_node->ip_128, new_node->len_128);
    BMPEntry* old_entry = bmp_entries[old_node->bmp];
    int pos = static_cast<int>(old_entry->nodes_list.lower_bound_index(old_node, ScampTrieNodeLessThan()));
    
    if (old_node->bmp == new_node->bmp) {
        old_entry->nodes_list.set_at(static_cast<size_t>(pos), new_node);
        return;
    } 
    
    old_entry->nodes_list.erase_range(static_cast<size_t>(pos), static_cast<size_t>(pos + 1));
    
    BMPEntry* new_entry = bmp_entries[new_node->bmp];
    pos = static_cast<int>(new_entry->nodes_list.lower_bound_index(new_node, ScampTrieNodeLessThan()));
    new_entry->nodes_list.insert_at(static_cast<size_t>(pos), new_node);
}
