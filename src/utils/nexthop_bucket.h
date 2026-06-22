#ifndef __NEXTHOP_BUCKET_H__
#define __NEXTHOP_BUCKET_H__

#include "../scamptrie_base.h"

#include <vector>

// allocate memory for nexthop info
class NexthopBucket {
    static const uint32_t NEXTHOP_BUCKET_SIZE = (1e6) - 1;
public:
    NexthopBucket();
    ~NexthopBucket();

    /// Retrieves nexthop at specified position
    nexthop_t get_nh(uint32_t ptr, uint32_t pos) const;
    /// Retrieves bmp at specified position
    uint32_t get_bmp_idx(uint32_t ptr, uint32_t pos) const;
    /// Allocates new nexthop segment
    /// @return Pointer to allocated segment
    uint32_t alloc();
    /// Free the segment
    void free(uint32_t ptr);
    /// Gets number of nexthops in segment
    /// @return Nexthop count
    uint32_t get_size(uint32_t ptr) const;
    /// Inserts nexthop at specified position
    /// @param ptr Segment pointer (may change on reallocation)
    /// @param pos Position in segment
    /// @param nh Nexthop to insert
    /// @param bmp BMP (Index of BMPManager)
    void insert(uint32_t &ptr, uint32_t pos, nexthop_t nh, uint32_t bmp = 0);

private:
    std::vector<nexthop_t> buckets[NEXTHOP_BUCKET_SIZE];
    std::vector<uint32_t> bmp_buckets[NEXTHOP_BUCKET_SIZE];
    uint32_t size;
    uint32_t reallocated_ptrs[NEXTHOP_BUCKET_SIZE];
    uint32_t reallocated_size;
};

#endif // __NEXTHOP_BUCKET_H__