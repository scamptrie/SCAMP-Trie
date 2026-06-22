#ifndef BIOLP_H
#define BIOLP_H

#include "auxtrie.h"
#include "seg_queue.h"
#include <iostream>
#include <queue>

#define ALL_STAGE 120

#define DIRECT_LEN 12

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

struct BiOLP
{
    uint8_t direct_len = DIRECT_LEN;
    AUXTRIE auxtrie;
    TRIE_VECTOR trie_vector;

    BiOLP() {}

    ~BiOLP() {}

    void Insert_prefix(ip6_t pfx, nexthop_t nh)
    {
        /*update auxtrie*/
        AUXTRIE_NODE *aux_node, *child_node;
        uint32_t level, bit;

        aux_node = auxtrie.root;

        for (level = 0; level < pfx.length; ++level)
        {
            bit = get_bit(pfx, level);
            if (auxtrie.empty_node(aux_node->child[bit]))
            {
                aux_node->child[bit] = auxtrie.alloc_node(0, level + 1);
                child_node = auxtrie.get_node(aux_node->child[bit]);
                child_node->self = aux_node->child[bit];
                child_node->parent = aux_node->self;
            }
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
            {
                current_node->child[i] = auxtrie.alloc_node(pre_solid_node->nh, level + 1);
                AUXTRIE_NODE *child_node = auxtrie.get_node(current_node->child[i]);
                child_node->self = current_node->child[i];
                child_node->parent = current_node->self;
            }
            else
                Node_pushing(auxtrie.get_node(current_node->child[i]), pre_solid_node);
        }
    }

    void Making_trie(AUXTRIE_NODE current_node)
    {
        TRIE trie;
        trie.root = current_node;
        queue<TRIE_NODE> segment;

        queue<AUXTRIE_NODE> aux_queue;
        aux_queue.push(current_node);

        while (!aux_queue.empty())
        {
            AUXTRIE_NODE pop_node = aux_queue.front();
            aux_queue.pop();

            if (auxtrie.empty_node(pop_node.child[0]) && auxtrie.empty_node(pop_node.child[1])) // leaf node
                trie.leaves.emplace_back(pop_node);
            for (int i = 0; i <= 1; i++)
            {
                if (!auxtrie.empty_node(pop_node.child[i]))
                    aux_queue.push(*auxtrie.get_node(pop_node.child[i]));
            }
        }
        trie.Update(direct_len);
        trie_vector.tries.emplace_back(trie);
    }

    void Creating_trie(AUXTRIE_NODE current_node)
    {
        if (auxtrie.empty_node(current_node.child[0]) &&
            auxtrie.empty_node(current_node.child[1])) // no son, direct search
            return;

        if (current_node.level == direct_len)
        {
            Making_trie(current_node);
            return;
        }

        for (int i = 0; i <= 1; i++)
        {
            if (!auxtrie.empty_node(current_node.child[i]))
                Creating_trie(*auxtrie.get_node(current_node.child[i]));
        }
    }

    uint16_t Selecting_inverted_subtree(uint16_t TN, uint8_t IFR)
    {
        uint64_t K = trie_vector.tries.size();
        if (K == 0)
            return true;

        uint64_t N = auxtrie.count_node(IPV6_LEN, *auxtrie.root) - auxtrie.count_node(direct_len, *auxtrie.root) + K;
        uint16_t H = ALL_STAGE, V = 0;
        uint64_t W = K;

        uint64_t threshold = IFR * ((N + H - 1) / H); // ⌈N / H⌉ × IFR

        while (V < TN && W < threshold)
        {
            TRIE *t = &trie_vector.tries[V];

            if (!t->inverted)
                t->inverted = true;

            V++;
            W += t->leaf_cnt - 1;
        }
        printf("select %d tries inverted in %ld tries at IFR : %d and TN : %d\n", V, K, IFR, TN);

        return V;
    }

    bool Map_stage(vector<uint32_t> &stage)
    {
        uint32_t All_node = auxtrie.count_node(IPV6_LEN, *auxtrie.root) - auxtrie.count_node(direct_len, *auxtrie.root) + trie_vector.tries.size();
        // printf("All node : %d\n", All_node);
        uint8_t stage_idx = 0;
        READY_LIST Ready, NextReady;
        for (size_t i = 0; i < trie_vector.tries.size(); i++)
        {
            TRIE trie = trie_vector.tries[i];
            if (trie.inverted)
            {
                for (size_t j = 0; j < trie.leaves.size(); j++)
                {
                    trie.leaves[j].priority = trie.leaves[j].level - direct_len;
                    trie.leaves[j].Critical = trie.leaves[j].priority >= (ALL_STAGE - 1);
                    trie.leaves[j].inverted = true;
                    Ready.push(trie.leaves[j]);
                }
            }
            else
            {
                trie.root.priority = trie.height;
                trie.root.Critical = trie.root.priority >= (ALL_STAGE - 1);
                trie.root.inverted = false;
                Ready.push(trie.root);
            }
        }

        while (!Ready.empty())
        {
            if (stage_idx == ALL_STAGE)
                return false;

            uint32_t Mi = 0;
            uint32_t capacity = (All_node + (ALL_STAGE - stage_idx) - 1) / (ALL_STAGE - stage_idx);
            bool critical_exist = true;

            Ready.sort_list();

            while (critical_exist || (Mi < capacity && !Ready.empty()))
            {
                if (Ready.empty())
                    break;

                AUXTRIE_NODE aux = Ready.pop_front();

                critical_exist = aux.Critical;

                Mi++;

                if (aux.inverted)
                {
                    AUXTRIE_NODE *aux_parent = auxtrie.get_node(aux.parent);
                    if (aux_parent->level <= direct_len)
                        continue;
                    if (aux_parent->child_mapped)
                    {
                        aux_parent->priority = aux.priority - 1;
                        aux_parent->Critical = aux_parent->priority >= (ALL_STAGE - stage_idx - 2);
                        aux_parent->inverted = true;
                        aux_parent->child_mapped = false; // reset
                        NextReady.push(*aux_parent);
                    }
                    else
                        aux_parent->child_mapped = true;
                }
                else
                {
                    for (int i = 0; i < 2; i++)
                    {
                        if (!auxtrie.empty_node(aux.child[i]))
                        {
                            AUXTRIE_NODE child_aux = *auxtrie.get_node(aux.child[i]);

                            child_aux.priority = aux.priority - 1;
                            child_aux.Critical = child_aux.priority >= (ALL_STAGE - stage_idx - 2);
                            child_aux.inverted = false;

                            NextReady.push(child_aux);
                        }
                    }
                }
            }

            stage[stage_idx] = Mi;
            All_node -= Mi;
            stage_idx++;

            Ready.compact();
            // printf("Stage %d done, cap : %d, Mi is %d, Ready Size is %d, NextReady size is %d\n", stage_idx, capacity, Mi, Ready.nodes.size(), NextReady.nodes.size());
            for (size_t i = Ready.head; i < Ready.nodes.size(); i++)
                Ready.nodes[i].Critical = Ready.nodes[i].priority >= (ALL_STAGE - stage_idx - 1);

            while (!NextReady.empty())
                Ready.nodes.push_back(NextReady.pop_front());
            NextReady.clear();
        }
        return true;
    }
};

#endif // BIOLP_H