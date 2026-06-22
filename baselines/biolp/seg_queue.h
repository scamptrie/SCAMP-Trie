#ifndef SEGMENT_QUEUE_H
#define SEGMENT_QUEUE_H

#include "ipv6.h"
#include "auxtrie.h"
#include <algorithm>
#include <vector>

using namespace std;

struct DIT_NODE
{
    uint64_t pfx : 12;

    uint64_t distance : 7;
    uint64_t address : 24;
    uint64_t direction : 1;
};

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

struct READY_LIST
{
    std::vector<AUXTRIE_NODE> nodes;
    size_t head = 0;

    static bool compare(AUXTRIE_NODE a, AUXTRIE_NODE b)
    {

        if (a.Critical != b.Critical)
            return a.Critical == true;

        return a.priority > b.priority;
    }

    void sort_list()
    {
        std::sort(nodes.begin(), nodes.end(), compare);
    }

    bool empty() const
    {
        return head >= nodes.size();
    }

    AUXTRIE_NODE pop_front()
    {
        return nodes[head++];
    }

    void push(AUXTRIE_NODE &p)
    {
        nodes.push_back(p);
    }

    void clear()
    {
        nodes.clear();
        head = 0;
    }

    void compact()
    {
        if (head > 0)
        {
            nodes.erase(nodes.begin(), nodes.begin() + head);
            head = 0;
        }
    }
};

#endif // SEGMENT_QUEUE_H