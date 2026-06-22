#ifndef OLP_H
#define OLP_H

#include "auxtrie.h"
#include "seg_queue.h"
#include <iostream>

#define IPV6_LEN 128

#define ALL_STAGE 120

using namespace std;

static inline uint32_t get_bit(ip6_t ip, int pos)
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

struct OLP
{
    uint8_t direct_len = 0;
    AUXTRIE auxtrie;
    SEG_QUEUE seg_queue;

    OLP() {}
    ~OLP() {}

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

    void Node_pushing(AUXTRIE_NODE *current_node, AUXTRIE_NODE *pre_solid_node)
    {
        uint8_t level = current_node->level;
        if (level > IPV6_LEN)
        {
            cout << "ERROR : Outliers in node pushing!" << endl;
            return;
        }

        if (current_node->soild_node)
            pre_solid_node = current_node;

        if (pre_solid_node == NULL)
        {
            cout << "ERROR : Pre-solid-node is NULL in SkiptrieNodePushing!" << endl;
            return;
        }

        current_node->nh = pre_solid_node->nh;

        if (auxtrie.empty_node(current_node->child[0]) &&
            auxtrie.empty_node(current_node->child[1])) // no son
            return;

        for (int i = 0; i <= 1; i++)
        {
            if (auxtrie.empty_node(current_node->child[i])) // Full binary tree
                current_node->child[i] = auxtrie.alloc_node(pre_solid_node->nh, level + 1);
            else
                Node_pushing(auxtrie.get_node(current_node->child[i]), pre_solid_node);
        }
    }

    void Partition()
    {
        uint32_t all_node = auxtrie.count_node(IPV6_LEN, *auxtrie.root);
        for (uint8_t i = 1; i < 48; i++)
        {
            uint32_t direct = auxtrie.count_node(i, *auxtrie.root);
            uint32_t avg = (all_node - direct) / (ALL_STAGE - 1);

            if (avg > ((uint32_t)1 << (i - 1)) && avg < ((uint32_t)1 << i))
            {
                direct_len = i;
                printf("avg : %d, direct_len : %d\n", avg, direct_len);
                return;
            }
        }
    }

    void BFS(AUXTRIE_NODE current_node)
    {
        TRIE_QUEUE trie_queue;
        queue<TRIE_NODE> segment;

        queue<AUXTRIE_NODE> aux_queue;
        aux_queue.push(current_node);
        uint8_t layer = 0;

        while (!aux_queue.empty())
        {
            AUXTRIE_NODE pop_node = aux_queue.front();
            aux_queue.pop();

            // new segment
            if (pop_node.level != layer)
            {
                layer = pop_node.level;
                if (segment.size() != 0)
                {
                    queue<TRIE_NODE> push_seg = segment;
                    while (!segment.empty())
                        segment.pop();
                    trie_queue.segments.push(push_seg);
                }
            }

            for (int i = 0; i <= 1; i++)
            {
                if (!auxtrie.empty_node(pop_node.child[i]))
                {
                    AUXTRIE_NODE push_node = *auxtrie.get_node(pop_node.child[i]);
                    segment.push(TRIE_NODE());
                    aux_queue.push(push_node);
                }
            }
        }
        seg_queue.queues.push_back(trie_queue);
    }

    void Make_queue(AUXTRIE_NODE current_node)
    {
        if (auxtrie.empty_node(current_node.child[0]) &&
            auxtrie.empty_node(current_node.child[1])) // no son, direct search
            return;

        if (current_node.level == direct_len)
        {
            BFS(current_node);
            return;
        }

        for (int i = 0; i <= 1; i++)
        {
            if (!auxtrie.empty_node(current_node.child[i]))
                Make_queue(*auxtrie.get_node(current_node.child[i]));
        }
    }

    bool Map_stage(bool correction, uint32_t cap, vector<uint32_t> &stage)
    {
        SEG_QUEUE seg_queue_copy = seg_queue;

        uint16_t stage_num = 0;
        uint32_t node_count = auxtrie.count_node(IPV6_LEN, *auxtrie.root) -
                              auxtrie.count_node(direct_len, *auxtrie.root);
        uint32_t capacity = 0;

        while (!seg_queue_copy.is_empty())
        {
            stage_num++;
            if (stage_num == ALL_STAGE)
                return false;
            /* Essay : every stage need to update capacity. */
            if (correction)
                capacity = cap;
            else
                capacity = (node_count + (ALL_STAGE - stage_num) - 1) / (ALL_STAGE - stage_num);
            uint32_t size = 0;
            uint16_t queue_num = 0;

            seg_queue_copy.sortSEG_QUEUE();
            // printf("stage : %d ", stage_num);
            // for (int i = 0; i < 10; i++)
            //     printf("queue[%d] : %d ", i, seg_queue_copy.queues[i]->segments.size());
            // printf("\n");

            if (!correction)
            {
                while (seg_queue_copy.queues[queue_num].segments.size() >= size_t(ALL_STAGE - stage_num))
                {
                    queue<TRIE_NODE> *seg = &seg_queue_copy.queues[queue_num].segments.front();
                    while (!seg_queue_copy.queues[queue_num].segments.empty() && !seg->empty())
                    {
                        stage[stage_num]++;
                        seg->pop();
                        size++;
                        node_count--;
                    }
                    if (!seg_queue_copy.queues[queue_num].segments.empty() && seg->empty())
                        seg_queue_copy.queues[queue_num].segments.pop();
                    queue_num++;
                    if (queue_num >= seg_queue_copy.queues.size())
                        break;
                }
            }

            while (size < capacity)
            {
                if (queue_num >= seg_queue_copy.queues.size())
                    break;
                queue<TRIE_NODE> *seg = &seg_queue_copy.queues[queue_num].segments.front();
                while (!seg_queue_copy.queues[queue_num].segments.empty() && !seg->empty())
                {
                    stage[stage_num]++;
                    seg->pop();
                    size++;
                    node_count--;
                    if (size >= capacity)
                        break;
                }
                if (!seg_queue_copy.queues[queue_num].segments.empty() && seg->empty())
                    seg_queue_copy.queues[queue_num].segments.pop();
                queue_num++;
            }
        }
        return true;
    }
};

#endif // OLP_H