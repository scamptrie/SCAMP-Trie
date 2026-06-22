#ifndef TRIE_H
#define TRIE_H

#include "auxtrie.h"
#include <iostream>
#include <vector>

#define IPV6_LEN 128

#define ALL_STAGE 19

using namespace std;

struct TRIE_NODE
{
    uint64_t prefix_bitmap;
    uint32_t prefix_offset;
    uint64_t child_bitmap;
    uint32_t child_offset;
};

static inline uint32_t
get_bit(ip6_t ip, int pos)
{
    if (pos < 64)
    {
        return (ip.hi & (1ULL << (63 - pos))) > 0 ? 1 : 0;
    }
    else
    {
        return (ip.lo & (1ULL << (127 - pos))) > 0 ? 1 : 0;
    }
}

struct TRIE
{
    uint8_t direct_len = 0;
    AUXTRIE auxtrie;

    TRIE() {}
    ~TRIE() {}

    void Insert_prefix(ip6_t pfx, nexthop_t nh)
    {
        /*update auxtrie*/
        AUXTRIE_NODE *aux_node;
        uint32_t level, bit;

        aux_node = auxtrie.root;

        for (level = 0; level < pfx.length; ++level)
        {
            bit = get_bit(pfx, level);
            if (auxtrie.empty_node(aux_node->child[bit]))
                aux_node->child[bit] = auxtrie.alloc_node(0, level + 1);
            aux_node = auxtrie.get_node(aux_node->child[bit]);
        }
        aux_node->nh = nh;
        aux_node->soild_node = true;
    }

    void Map_stage(vector<uint32_t> &stage, int &layer16_count, AUXTRIE_NODE current_node)
    {
        // if (current_node.level % 6 == 4 && current_node.level >= 16)
        //     stage[current_node.level / 6 - 2]++;
        if (current_node.level == 16)
            layer16_count++;

        if (auxtrie.empty_node(current_node.child[0]) &&
            auxtrie.empty_node(current_node.child[1])) // no son, direct search
            return;
        
        if (current_node.level % 6 == 4 && current_node.level >= 16)
            stage[current_node.level / 6 - 2]++;
        
        for (int i = 0; i <= 1; i++)
        {
            if (!auxtrie.empty_node(current_node.child[i]))
                Map_stage(stage, layer16_count, *auxtrie.get_node(current_node.child[i]));
        }
    }
};

#endif // TRIE_H