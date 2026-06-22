#ifndef __NEXTHOP_BITMAP_BUCKET_H__
#define __NEXTHOP_BITMAP_BUCKET_H__

#include "../scamptrie_base.h"

class MemInit;  // Forward declaration

class NexthopBitmapBucket {
    friend class MemInit;
    static const int BUCKET_SIZE = (1 << 16) - 1;
public:
    NexthopBitmapBucket() {size = 0;};
    ~NexthopBitmapBucket() = default;

    void clear() {size = 0;}
    // 获取当前已使用的 bitmap 数量（用于统计弹性存储开销）
    uint16_t get_size() const { return size; }
    // get index of bitmap, if not exist, insert it
    uint16_t get_bitmap_index(uint64_t prefix) {
        for (uint16_t i = 0; i < size; ++i)
            if (buckets[i] == prefix)
                return i;
        if (size < BUCKET_SIZE) {
            buckets[size] = prefix;
            return size++;
        }
        msg_error("NexthopBitmapBucket::get_bitmap_index: Bucket Full");
        return -1;
    }
    
    uint64_t get_bitmap(uint16_t index) const {
        if (index < size) return buckets[index];
        msg_error("NexthopBitmapBucket::get_bitmap: Index out of range");
        return UINT64_MAX;
    }
private:
    uint64_t buckets[BUCKET_SIZE];
    uint16_t size;
};

#endif // __NEXTHOP_BITMAP_BUCKET_H__