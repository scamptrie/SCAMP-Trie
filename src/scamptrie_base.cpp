#include "scamptrie_base.h"
#include <cstdio>
#include <cstdlib>

void msg_error(const char* msg) {
    printf("[ERROR] %s\n", msg);
    exit(1);
}

void msg_warn(const char* msg) {
    printf("[WARN] %s\n", msg);
}

void msg_info(const char* msg) {
    printf("[INFO] %s\n", msg);
}

uint8_t round_to_6(uint8_t len) {
    return (len / 6) * 6;
}

// equal for first 'len' bits
bool is_prefix_equal(const uint64_t a64, const uint64_t b64, const uint8_t len) {
    uint64_t mask = len == 0 ? 0 : (~0ULL) << (64 - len);
    return (a64 & mask) == (b64 & mask);
}

bool is_prefix_equal(const ip6_t& a, const ip6_t& b, const uint8_t len) {
    if (len <= 64) return is_prefix_equal(a.hi, b.hi, len);
    return a.hi == b.hi && is_prefix_equal(a.lo, b.lo, len - 64);
}

uint16_t count_ones_before_pos(uint64_t bitmap, uint16_t pos) {
    if (pos == 0) return 0;
    if (pos >= 64) return __builtin_popcountll(bitmap);
    uint64_t mask = (1ULL << pos) - 1;
    return (uint16_t)__builtin_popcountll(bitmap & mask);
}

uint16_t get_substring_in_subtrie(uint64_t prefix, uint8_t len) {
    if (len >= 60) {
        return (uint16_t)(prefix & 0xf) << 2; // mask bit[61-64], then fill 2 0s
    }
    uint8_t subtrie_base_len = len / 6 * 6 + 6; // 0~5->6, 6~11->12, ...
    return (uint16_t)((prefix >> (64 - subtrie_base_len)) & 0x3F);
}

uint16_t get_substring_in_subtrie(const ip6_t& prefix, const uint8_t len) {
    if (len < 60) return get_substring_in_subtrie(prefix.hi, len);
    if (len >= 66) return get_substring_in_subtrie(prefix.lo << 2, len - 66);
    return ((prefix.hi & 0xf) << 2) | (prefix.lo >> 62);
}

uint16_t get_nh_bitmap_pos_6(const uint16_t prefix, const uint8_t len) {
    return (1ull << len) + (prefix >> (6 - len));
}

uint16_t get_nh_bitmap_pos_64(const uint64_t prefix, const uint8_t len) {
    return get_nh_bitmap_pos_6(get_substring_in_subtrie(prefix, len), len % 6);
}

uint16_t get_nh_bitmap_pos_128(const ip6_t& prefix, const uint8_t len) {
    if (len < 60) return get_nh_bitmap_pos_64(prefix.hi, len);
    if (len >= 66) return get_nh_bitmap_pos_64(prefix.lo << 2, len - 66);
    return get_nh_bitmap_pos_6(get_substring_in_subtrie(prefix, len), len % 6);
}

void mask_address(ip6_t& address, uint8_t len) {
    if (len > 64) {
        address.lo >>= 128 - len;
        address.lo <<= 128 - len;
    } else {
        address.hi >>= 64 - len;
        address.hi <<= 64 - len;
        address.lo = 0;
    }
}

bool is_A_prefix_of_B(const ip6_t& A, const uint8_t A_len, const ip6_t& B, const uint8_t B_len) {
    if (A_len > B_len) return false;
    if (A_len <= 64) return is_prefix_equal(A.hi, B.hi, A_len);
    return A.hi == B.hi && is_prefix_equal(A.lo, B.lo, A_len - 64);
}

void getmax_stage(uint8_t &stage1, uint8_t stage2) {
    if (stage1 == STAGE_DEFAULT || stage1 < stage2) stage1 = stage2;
}
