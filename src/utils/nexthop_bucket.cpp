#include "nexthop_bucket.h"
#include <cstdio>
#include <cstdlib>

NexthopBucket::NexthopBucket() {
    size = 0;
}

NexthopBucket::~NexthopBucket() {
    for (int i = 0; i < size; ++i)
        buckets[i].clear();
    for (int i = 0; i < NEXTHOP_BUCKET_SIZE; ++i)
        bmp_buckets[i].clear();
    size = 0;
    reallocated_size = 0;
}

nexthop_t NexthopBucket::get_nh(uint32_t ptr, uint32_t pos) const {
    if (ptr >= size) msg_error("NexthopBucket::get: Index out of range");
    if (pos >= buckets[ptr].size()) msg_error("NexthopBucket::get: Position out of range");
    return buckets[ptr][pos];
}

uint32_t NexthopBucket::get_bmp_idx(uint32_t ptr, uint32_t pos) const {
    if (ptr >= size) msg_error("NexthopBucket::get_bmp_idx: Index out of range");
    if (pos >= bmp_buckets[ptr].size()) msg_error("NexthopBucket::get_bmp_idx: Position out of range");
    return bmp_buckets[ptr][pos];
}

uint32_t NexthopBucket::alloc() {
    if (size >= NEXTHOP_BUCKET_SIZE) {
        msg_error("NexthopBucket::alloc: Nexthop bucket full!");
        return UINT32_MAX;
    }
    buckets[size].clear();
    bmp_buckets[size].clear();
    uint32_t ptr;
    if (reallocated_size > 0) {
        ptr = reallocated_ptrs[reallocated_size - 1];
        reallocated_size--;
    } else {
        ptr = size++;
    }
    return ptr;
}

void NexthopBucket::free(uint32_t ptr) {
    if (reallocated_size >= NEXTHOP_BUCKET_SIZE) msg_error("NexthopBucket::free: Reallocated size out of range");
    reallocated_ptrs[reallocated_size++] = ptr;
    buckets[ptr].clear();
    bmp_buckets[ptr].clear();
}

uint32_t NexthopBucket::get_size(uint32_t ptr) const {
    if (ptr >= size) msg_error("NexthopBucket::get_size: Index out of range");
    return buckets[ptr].size();
}

void NexthopBucket::insert(uint32_t &ptr, uint32_t pos, nexthop_t nh, uint32_t bmp) {
    if (ptr >= size) {
        printf("NexthopBucket::insert: Index out of range, ptr: %d, size: %d\n", ptr, size);
        exit(1);
    }
    if (pos > buckets[ptr].size()) msg_error("NexthopBucket::insert: Position out of range");
    buckets[ptr].insert(buckets[ptr].begin() + pos, nh);
    bmp_buckets[ptr].insert(bmp_buckets[ptr].begin() + pos, bmp);
    ptr = ptr; // for the std::vector implementation -> always keep the same segment
}