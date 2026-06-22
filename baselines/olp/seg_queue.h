#ifndef SEGMENT_QUEUE_H
#define SEGMENT_QUEUE_H

#include "ipv6.h"
#include <algorithm>
#include <queue>
#include <vector>

using namespace std;

struct CHILD_INFO
{
    uint32_t offset : 24;
    uint32_t distance : 8;
};

struct TRIE_NODE
{
    CHILD_INFO child_info[2];
};

struct LEAF_NODE
{
    nexthop_t nh;
};

struct TRIE_QUEUE
{
    queue<queue<TRIE_NODE>> segments;
};

struct SEG_QUEUE
{
    vector<TRIE_QUEUE> queues;

    static bool compareQueues(TRIE_QUEUE a, TRIE_QUEUE b)
    {
        // 1. segments count
        int segmentCountA = a.segments.size();
        int segmentCountB = b.segments.size();

        if (segmentCountA != segmentCountB)
        {
            return segmentCountA > segmentCountB;
        }

        // 2. first segment size
        int firstSegmentSizeA = 0;
        int firstSegmentSizeB = 0;

        if (!a.segments.empty())
            firstSegmentSizeA = a.segments.front().size();
        if (!b.segments.empty())
            firstSegmentSizeB = b.segments.front().size();

        if (firstSegmentSizeA != firstSegmentSizeB)
        {
            return firstSegmentSizeA > firstSegmentSizeB;
        }

        // 3. total element count
        int totalElementCountA = 0;
        int totalElementCountB = 0;

        {
            auto temp = a.segments;
            while (!temp.empty())
            {
                totalElementCountA += temp.front().size();
                temp.pop();
            }
        }

        {
            auto temp = b.segments;
            while (!temp.empty())
            {
                totalElementCountB += temp.front().size();
                temp.pop();
            }
        }

        return totalElementCountA > totalElementCountB;
    }

    void sortSEG_QUEUE() { sort(queues.begin(), queues.end(), compareQueues); }

    bool is_empty()
    {
        for (size_t i = 0; i < queues.size(); i++)
        {
            if (queues[i].segments.size() > 0)
                return false;
        }
        return true;
    }
};

#endif // SEGMENT_QUEUE_H