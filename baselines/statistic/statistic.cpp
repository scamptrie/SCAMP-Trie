#include "statistic.h"
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

void StatisticCollector::collect_statistics(Trie& trie, StatisticResult& result) {
    // 初始化结果
    result.inverse_distance_dist.assign(22, 0.0);
    result.child_count_cdf.assign(11, 0.0);
    result.similarity_ratio = 0.0;
    
    // 1. 统计逆距离
    std::vector<int> distance_count(22, 0);
    int total_nodes = 0;
    collect_inverse_distance(trie.root.get(), distance_count, total_nodes);
    
    if (total_nodes > 0) {
        for (int i = 0; i < 22; i++) {
            result.inverse_distance_dist[i] = static_cast<double>(distance_count[i]) / total_nodes;
        }
    }
    
    // 2. 统计子节点数
    std::vector<int> child_count_dist(11, 0);  // 0-10
    total_nodes = 0;
    collect_child_count(trie.root.get(), child_count_dist, total_nodes);
    
    if (total_nodes > 0) {
        // 计算CDF
        int cumulative = 0;
        for (int i = 0; i < 11; i++) {
            cumulative += child_count_dist[i];
            result.child_count_cdf[i] = static_cast<double>(cumulative) / total_nodes;
        }
        printf("Child count stats: total_nodes: %d\n", total_nodes);
    } else {
        printf("Warning: Child count stats: total_nodes is 0!\n");
    }
    
    // 3. 统计结构相似度
    std::map<uint64_t, int> bitmap_count;
    total_nodes = 0;
    collect_similarity(trie.root.get(), bitmap_count, total_nodes);
    
    if (total_nodes > 0) {
        // 统计特征值仅出现1次的节点数
        int unique_count = 0;
        for (const auto& pair : bitmap_count) {
            if (pair.second == 1) {
                unique_count++;
            }
        }
        
        // 计算占比：1 - (唯一特征值节点数 / 总节点数)
        result.similarity_ratio = 1.0 - (static_cast<double>(unique_count) / total_nodes);
        printf("Similarity stats: NH_bitmap_count: %d, total_nodes: %d, unique_count: %d, similarity_ratio: %.6f\n", 
               (int)bitmap_count.size(), total_nodes, unique_count, result.similarity_ratio);
    } else {
        printf("Warning: Similarity stats: total_nodes is 0!\n");
    }
}

void StatisticCollector::collect_inverse_distance(TrieNode* node,
                                                  std::vector<int>& distance_count,
                                                  int& total_nodes) {
    if (node == nullptr) return;
    
    total_nodes++;
    
    // 统计当前节点的逆距离
    if (node->inverse_distance >= 0 && node->inverse_distance < 22) {
        distance_count[node->inverse_distance]++;
    }
    
    // 递归统计子节点
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            collect_inverse_distance(node->children[i].get(), distance_count, total_nodes);
        }
    }
}

void StatisticCollector::collect_child_count(TrieNode* node,
                                             std::vector<int>& child_count_dist,
                                             int& total_nodes) {
    if (node == nullptr) return;
    
    total_nodes++;
    
    // 统计当前节点的子节点数
    int child_count = node->get_child_count();
    if (child_count <= 10) {
        child_count_dist[child_count]++;
    }
    
    // 递归统计子节点
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            collect_child_count(node->children[i].get(), child_count_dist, total_nodes);
        }
    }
}

void StatisticCollector::collect_similarity(TrieNode* node,
                                            std::map<uint64_t, int>& bitmap_count,
                                            int& total_nodes) {
    if (node == nullptr) return;
    
    // collapsed节点不参与统计
    if (!node->collapsed) {
        total_nodes++;
        bitmap_count[node->bitmap]++;
    }
    
    // 递归统计子节点
    for (int i = 0; i < TrieNode::MAX_CHILDREN; i++) {
        if (node->children[i] != nullptr) {
            collect_similarity(node->children[i].get(), bitmap_count, total_nodes);
        }
    }
}

void write_csv_results(const std::vector<std::string>& fib_names,
                       const std::vector<StatisticResult>& results,
                       const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << output_file << " for writing" << std::endl;
        return;
    }
    
    file << std::fixed << std::setprecision(6);
    
    // 写入表头和数据
    for (size_t i = 0; i < fib_names.size() && i < results.size(); i++) {
        file << fib_names[i];
        
        const auto& result = results[i];
        
        // 根据输出文件类型写入不同的列
        if (output_file.find("res_ori_inv") != std::string::npos || output_file.find("res_inv") != std::string::npos) {
            // 逆距离统计：22列（包括res_inv.csv和res_ori_inv.csv）
            for (int j = 0; j < 22; j++) {
                file << "," << result.inverse_distance_dist[j];
            }
        } else if (output_file.find("res_ori_count") != std::string::npos || output_file.find("res_count") != std::string::npos) {
            // 子节点数统计：11列（0-10）（包括res_count.csv和res_ori_count.csv）
            for (int j = 0; j < 11; j++) {
                file << "," << result.child_count_cdf[j];
            }
        } else if (output_file.find("res_ori_simi") != std::string::npos || output_file.find("res_simi") != std::string::npos) {
            // 结构相似度统计：1列（包括res_simi.csv和res_ori_simi.csv）
            file << "," << result.similarity_ratio;
        }
        
        file << "\n";
    }
    
    file.close();
}
