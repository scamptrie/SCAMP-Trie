#ifndef STATISTIC_H
#define STATISTIC_H

#include "trie.h"
#include <string>
#include <vector>
#include <map>

struct StatisticResult {
    // 逆距离统计：22列，每列表示该逆距离的节点占比
    std::vector<double> inverse_distance_dist;
    
    // 子节点数统计：11列（0-10），每列表示子节点数<=K的节点占比
    std::vector<double> child_count_cdf;
    
    // 结构相似度统计：1列，表示特征值出现次数>1的节点占比
    double similarity_ratio;
};

class StatisticCollector {
public:
    static void collect_statistics(Trie& trie, StatisticResult& result);
    
private:
    static void collect_inverse_distance(TrieNode* node, 
                                         std::vector<int>& distance_count,
                                         int& total_nodes);
    
    static void collect_child_count(TrieNode* node,
                                    std::vector<int>& child_count_dist,
                                    int& total_nodes);
    
    static void collect_similarity(TrieNode* node,
                                    std::map<uint64_t, int>& bitmap_count,
                                    int& total_nodes);
};

void write_csv_results(const std::vector<std::string>& fib_names,
                       const std::vector<StatisticResult>& results,
                       const std::string& output_file);

#endif // STATISTIC_H
