#ifndef AUX_TRIE_H
#define AUX_TRIE_H

#include "ipv6.h"
#include <cstdbool>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define AUXTRIE_OK 0
#define AUXTRIE_FAILURE 1

/* The Max # of groups is 32K. */
#define MAX_GROUP_NUM (1u << 15)

/* Each group has 4K nodes. */
#define GROUP_SIZE (1u << 12)

/* The node of the auxiliary trie that
 * helps to support incremental updates. */
struct INDEX
{
    uint16_t group_idx;
    uint16_t node_idx;
};

struct AUXTRIE_NODE
{
    struct INDEX child[2];
    nexthop_t nh;
    bool soild_node;
    uint8_t level;
};

struct AUXTRIE
{
    struct AUXTRIE_NODE **node_arrs;
    struct AUXTRIE_NODE *root;
    uint32_t current_group_idx;
    uint32_t current_node_idx;

    AUXTRIE()
    {
        // allocate pointers of node arrays
        this->node_arrs =
            (struct AUXTRIE_NODE **)malloc(sizeof(struct AUXTRIE_NODE *) * MAX_GROUP_NUM);
        memset(this->node_arrs, 0, sizeof(struct AUXTRIE_NODE *) * MAX_GROUP_NUM);

        // allocate the first node array (reserve 0)
        this->node_arrs[1] =
            (struct AUXTRIE_NODE *)malloc(sizeof(struct AUXTRIE_NODE) * GROUP_SIZE);
        memset(this->node_arrs[1], 0, sizeof(struct AUXTRIE_NODE) * GROUP_SIZE);

        // allocate root node
        this->root = &(this->node_arrs[1][0]);
        this->root->soild_node = true;

        // set current group idx and node idx
        this->current_group_idx = this->current_node_idx = 1;
    }

    ~AUXTRIE()
    {
        for (uint32_t i = 1; i <= this->current_group_idx; ++i)
        {
            if (this->node_arrs[i])
            {
                free(this->node_arrs[i]);
            }
        }

        if (this->node_arrs)
        {
            free(this->node_arrs);
        }
    }

    struct INDEX alloc_node(nexthop_t nh, uint8_t level)
    {
        struct INDEX idx;

        idx.group_idx = this->current_group_idx;
        idx.node_idx = this->current_node_idx++;
        this->node_arrs[idx.group_idx][idx.node_idx].nh = nh;
        this->node_arrs[idx.group_idx][idx.node_idx].level = level;
        this->node_arrs[idx.group_idx][idx.node_idx].soild_node = false;

        if (this->current_node_idx >= GROUP_SIZE)
        {
            ++this->current_group_idx;
            this->current_node_idx = 0;
            this->node_arrs[this->current_group_idx] =
                (struct AUXTRIE_NODE *)malloc(sizeof(struct AUXTRIE_NODE) * GROUP_SIZE);
            memset(this->node_arrs[this->current_group_idx], 0,
                   sizeof(struct AUXTRIE_NODE) * GROUP_SIZE);
        }

        return idx;
    }

    struct AUXTRIE_NODE *get_node(struct INDEX idx)
    {
        return &(this->node_arrs[idx.group_idx][idx.node_idx]);
    }

    bool empty_node(struct INDEX idx) { return idx.group_idx == 0; }

    uint32_t count_node(uint8_t level, AUXTRIE_NODE current_node)
    {
        uint32_t count = 1;
        if (current_node.level == level)
            return count;
        if (empty_node(current_node.child[0]) && empty_node(current_node.child[1]))
            return count;
        if (!empty_node(current_node.child[0]))
            count += count_node(level, *get_node(current_node.child[0]));
        if (!empty_node(current_node.child[1]))
            count += count_node(level, *get_node(current_node.child[1]));
        return count;
    }
};
#endif // AUX_TRIE_H