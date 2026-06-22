#ifndef __SCAMPTRIE_BASE_H__
#define __SCAMPTRIE_BASE_H__

#include <cstdint>

typedef uint64_t nexthop_t;
static constexpr nexthop_t LPM_INVALID_NH = static_cast<nexthop_t>(-1);

struct ip6_t {
    uint64_t hi;
    uint64_t lo;
};

#define SCAMPTRIE_OK 0
#define SCAMPTRIE_FAIL 1

// First level bits configuration (default: 18)
#ifndef FIRST_LEVEL_BITS
#define FIRST_LEVEL_BITS 16
#endif

// mask for the bigendian FIRST_LEVEL_BITS bits
#define MASK_FIRST_LEVEL (((~0ULL) << (64 - FIRST_LEVEL_BITS)) & 0xFFFFFFFFFFFFFFFFULL)
// Keep MASK18 for backward compatibility, but it's now based on FIRST_LEVEL_BITS

#define RAM_NUM 11
#define STAGE_NUM 22
#define STAGE_NONE 60       // stage for Invalid Child
#define STAGE_DEFAULT 59    // stage for child which have not been assigned a stage
#define MAX_FIB_NUM 15

#define MEGA_BYTES (1024 * 1024)
#define KILO_BYTES (1024)
#define BYTES_128b (16)
#define BYTES_256b (32)
constexpr uint32_t NODE_SPACE[6] = {
    0,
    BYTES_128b,
    BYTES_256b,
    BYTES_256b,
    BYTES_128b,
    BYTES_256b
};

// Function declarations
void msg_error(const char* msg);
void msg_warn(const char* msg);
void msg_info(const char* msg);

// equal for first 'len' bits
// check a64[:len] == b64[:len]
bool is_prefix_equal(const uint64_t a64, const uint64_t b64, const uint8_t len);

// equal for first 'len' bits
// check a128[:len] == b128[:len]
bool is_prefix_equal(const ip6_t& a, const ip6_t& b, const uint8_t len);

// count the number of '1' in the bitmap[0,pos)
uint16_t count_ones_before_pos(uint64_t bitmap, uint16_t pos);

uint8_t round_to_6(uint8_t len);

/// @brief prefix in subtrie node
/// @param prefix the prefix (64 bits)
/// @param len the length of the prefix
/// @return prefix in subtrie node (< 6 bits)
/// @note sample: prefix=(xxxxxx)(110101)(xxxxxx)..., len=9 -> return 110101
uint16_t get_substring_in_subtrie(uint64_t prefix, uint8_t len);

/// @brief prefix in subtrie node
/// @param prefix the prefix (128 bits)
/// @param len the length of the prefix
/// @return prefix in subtrie node (< 6 bits)
/// @note sample: prefix=(xxxxxx)(110101)(xxxxxx)(xxxxxx)..., len=9 -> return 110101
uint16_t get_substring_in_subtrie(const ip6_t& prefix, const uint8_t len);

/// @brief nexthop bitmap position for the prefix (|prefix| = 6, len < 6)
/// @param prefix the prefix (6 bits)
/// @param len extract the first len bits (< 6)
/// @return nexthop bitmap position
/// @note sample: prefix=(110101), len=3 -> return (1|110) = 14
uint16_t get_nh_bitmap_pos_6(const uint16_t prefix, const uint8_t len);

/// @brief nexthop bitmap position for the prefix (|prefix| = 64, len < 64)
/// @param prefix the prefix (64 bits)
/// @param len extract the first len bits (< 64)
/// @return nexthop bitmap position
/// @note prefix = (xxxxxx)(110101)(xxxxxx)..., len = 6k+3 -> return (1|110) = 14
uint16_t get_nh_bitmap_pos_64(const uint64_t prefix, const uint8_t len);

/// @brief nexthop bitmap position for the prefix (|prefix| = 128, len < 128)
/// @param prefix the prefix (128 bits)
/// @param len extract the first len bits (< 128)
/// @return nexthop bitmap position
/// @note sample: prefix=(xxxxxx)(110101)(xxxxxx)(xxxxxx)..., len=9 -> return (1|110) = 14
uint16_t get_nh_bitmap_pos_128(const ip6_t& prefix, const uint8_t len);

/// @brief mask the address with the length
/// @param address the address to mask
/// @param len the length to mask
/// @note ip = 0x1234567890ABCDEF.0/12 -> 0x1230000000000000.0/12
void mask_address(ip6_t& address, uint8_t len);

/// @brief check if A is a prefix of B
/// @return true if A is a prefix of B, false otherwise
bool is_A_prefix_of_B(const ip6_t& A, const uint8_t A_len, const ip6_t& B, const uint8_t B_len);

typedef uint8_t stage_t;

void getmax_stage(stage_t &stage1, stage_t stage2);

inline stage_t higher_stage(stage_t stage) {
    return stage == STAGE_DEFAULT ? (stage_t)0 : stage + 1;
}

inline stage_t lower_stage(stage_t stage) {
    return stage == 0 ? STAGE_DEFAULT : stage - 1;
}

enum ScampNodeType : unsigned int {
    NONE = 0,
    TYPE1 = 1, // 2 children
    TYPE2,     // 6 children
    TYPE3,     // bitmap + pointer (>= 7 children)
    TYPE4,     // 1 prefix
    TYPE5      // 2 prefixes
};

// 6 + 4 + 22 = 32 bits
struct ChildInfo {
    uint8_t ip_sub6; // 6 bits
    stage_t stageID; // 4 bits
    uint32_t childPointer; // 22 bits
    ChildInfo(): stageID(STAGE_NONE), childPointer(UINT32_MAX) {}
    bool operator<(const ChildInfo& other) const {
        return ip_sub6 < other.ip_sub6;
    }
};

// stored out of 11 stages
struct RootInfo {
    stage_t stageID;
    uint32_t rootPointer;
    RootInfo(): stageID(STAGE_NONE), rootPointer(UINT32_MAX) {}
};

//prefix info
// 64 + 8 + 22 = 94 bits
struct PrefixInfo {
    uint64_t ip_64;
    uint8_t len_64;
    uint32_t nexthop; // 22 bits
    uint32_t bmp_entry_idx;
    ip6_t ip_128;
    uint8_t len_128;
    uint16_t get_nh_bitmap_pos() {
        return ::get_nh_bitmap_pos_64(ip_64, len_64);
    }
    bool operator<(const PrefixInfo& other) const {
        return get_nh_bitmap_pos_64(ip_64, len_64) < get_nh_bitmap_pos_64(other.ip_64, other.len_64);
    }
    bool match64(uint64_t ip64) { return is_prefix_equal(ip64, ip_64, len_64); }
    bool match128(const ip6_t& ip) { return is_prefix_equal(ip, ip_128, len_128); }
};

// base scamptrie node
// 1 + 2 + 22 = 25 bits
struct ScampTrieNode {
    ScampTrieNode() {nodeType = NONE; child_highest = STAGE_DEFAULT;}
    virtual ~ScampTrieNode() = default;
    
    ScampNodeType nodeType; // 3 bits
    // bmp refers to an index in BMPManager when enable DYNAMIC
    ip6_t ip_128;
    uint8_t len_128;
    uint8_t depth;
    uint32_t bmp; // 22 bits
    stage_t cur_stage;
    stage_t child_highest; // among its children
    uint32_t topo_export_idx; // used for topo export (only for debug)
    uint32_t mem_addr; // the real address on hardware memory (at each layer)

    /// Only Type1/Type2/Type3 have nhBitmapPointer; base/Type4/Type5 return a static dummy (must not be used for write).
    virtual uint16_t& get_nh_bitmap_pointer() {
        static uint16_t dummy = 0;
        msg_error("ScampTrieNode::get_nh_bitmap_pointer: not implemented for this node type");
        return dummy;
    }
    virtual uint32_t& get_nh_pointer() {
        msg_error("ScampTrieNode::get_nh_pointer: not implemented");
        return bmp; // just for compiler
    }
};

// Type 1: 2 children
// (25) + 16 + 22 + 2 * 32 + 1 = 128 bits
struct ScampTrieNode1 : public ScampTrieNode {
    ScampTrieNode1() {nodeType = TYPE1;}
    ~ScampTrieNode1() = default;
    
    uint16_t nhBitmapPointer;
    uint32_t nhPointer; // 22 bits
    ChildInfo childInfo[2];
    uint8_t reserved; // 1 bit

    uint16_t& get_nh_bitmap_pointer() override { return nhBitmapPointer; }
    uint32_t& get_nh_pointer() override { return nhPointer; }
};

/// @brief  Type 2: 6 children
/// (25) + 16 + 22 + 6 * 32 + 1 = 256 bits
struct ScampTrieNode2 : public ScampTrieNode {
    ScampTrieNode2() {nodeType = TYPE2;}
    ~ScampTrieNode2() = default;
    
    uint16_t nhBitmapPointer;
    uint32_t nhPointer; // 22 bits
    ChildInfo childInfo[6];
    uint8_t reserved; // 1 bit

    uint16_t& get_nh_bitmap_pointer() override { return nhBitmapPointer; }
    uint32_t& get_nh_pointer() override { return nhPointer; }
};

/// @brief Type 3: bitmap + pointer (>= 7 children)
/// (25) + 16 + 22 + 64 + 22 + 5 + 102 = 256 bits
struct ScampTrieNode3 : public ScampTrieNode {
    ScampTrieNode3() {nodeType = TYPE3;}
    ~ScampTrieNode3() = default;
    
    uint16_t nhBitmapPointer;
    uint32_t nhPointer; // 22 bits
    uint64_t childBitmap;
    uint32_t childPointer; // 22 bits
    stage_t childStage; // 5 bits
    ChildInfo freeChild[3]; // 3 * 32 = 96 bits
    // uint64_t reserved1;
    // uint64_t reserved2; // 64 + 38 = 102 bits

    uint16_t& get_nh_bitmap_pointer() override { return nhBitmapPointer; }
    uint32_t& get_nh_pointer() override { return nhPointer; }
};

/// @brief Type 4: 1 prefix
/// (25) + 16 + 22 + 94 + 9 = 128
struct ScampTrieNode4 : public ScampTrieNode {
    ScampTrieNode4() {nodeType = TYPE4;}
    ~ScampTrieNode4() = default;
    
    PrefixInfo prefixInfo[1]; // 94 bits
    uint16_t reserved; // 9 bits
};

// TRIE_NODE_5
// (25) + 16 + 22 + 2 * 94 + 43 = 256 bits
struct ScampTrieNode5 : public ScampTrieNode {
    ScampTrieNode5() {nodeType = TYPE5;}
    ~ScampTrieNode5() = default;
    
    PrefixInfo prefixInfo[2]; // 188 bits
    uint64_t reserved; // 64 + 43 = 107 bits
};

#endif // __SCAMPTRIE_BASE_H__