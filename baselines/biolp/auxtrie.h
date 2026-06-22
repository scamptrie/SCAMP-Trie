#ifndef AUX_TRIE_H
#define AUX_TRIE_H

#include "ipv6.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>

#define AUXTRIE_OK 0
#define AUXTRIE_FAILURE 1

/* The Max # of groups is 32K. */
#define MAX_GROUP_NUM (1u << 15)

/* Each group has 4K nodes. */
#define GROUP_SIZE (1u << 12)

using namespace std;

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
    struct INDEX parent, self;
    bool child_mapped = false;
    nexthop_t nh;
    bool soild_node;
    uint8_t level;

    uint8_t priority = 0;
    bool Critical = false;
    bool inverted = false;
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
        fill(this->node_arrs[1], this->node_arrs[1] + GROUP_SIZE, AUXTRIE_NODE{});

        // allocate root node
        this->root = &(this->node_arrs[1][0]);
        this->root->soild_node = true;
        this->root->self.group_idx = 1;
        this->root->self.node_idx = 0;

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
            fill(this->node_arrs[this->current_group_idx], this->node_arrs[this->current_group_idx] + GROUP_SIZE, AUXTRIE_NODE{});
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

struct TRIE
{
    AUXTRIE_NODE root;

    vector<AUXTRIE_NODE> leaves;
    uint32_t leaf_cnt = 0;

    uint8_t height = 0;

    bool inverted = false;

    uint32_t leaf_depth_sum = 0;
    double leaf_per_height = 0.0;
    double avg_leaf_depth = 0;

    void Update(uint8_t direct_len)
    {
        leaf_cnt = 0;
        height = 0;
        leaf_depth_sum = 0;

        for (const auto &leaf : leaves)
        {
            uint8_t depth = leaf.level - direct_len;
            leaf_cnt++;
            height = max(height, depth);
            leaf_depth_sum += depth;
        }

        leaf_per_height = (height > 0)
                              ? static_cast<double>(leaf_cnt) / height
                              : 0.0;
        avg_leaf_depth = (leaf_cnt > 0)
                             ? static_cast<double>(leaf_depth_sum) / leaf_cnt
                             : 0.0;
    }
};

struct TRIE_VECTOR
{
    vector<TRIE> tries;

    static bool compare(const TRIE a, const TRIE b)
    {
        constexpr double EPS = 1e-9;

        // if (a.leaf_cnt != b.leaf_cnt)
        //     return a.leaf_cnt > b.leaf_cnt;

        // if (a.height != b.height)
        //     return a.height < b.height;

        if (fabs(a.leaf_per_height - b.leaf_per_height) > EPS)
            return a.leaf_per_height > b.leaf_per_height;

        // if (fabs(a.avg_leaf_depth - b.avg_leaf_depth) > EPS)
        //     return a.avg_leaf_depth < b.avg_leaf_depth;

        return false;
    }

    void sortTRIE_VECTOR() { sort(tries.begin(), tries.end(), compare); }
};
#endif // AUX_TRIE_H