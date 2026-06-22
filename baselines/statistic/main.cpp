/*
 * 压缩策略存储规模统计：对每个数据集构建森林 Trie（r0 + hash@16 + hash@64），
 * 统计 origin / NI-Comp / Collapsed / Half-Size 四列 bit 数并输出到 res_compress.csv。
 * 实现策略详见 compress_trie.h 与 compress_trie.cpp 开头注释。
 */
#include "fib_parser.h"
#include "trie.h"
#include "statistic.h"
#include "compress_trie.h"
#include <iostream>
#include <vector>
#include <string>

int main() {
    // 定义FIB文件顺序（不按照dataset内的存放顺序）
    std::vector<std::string> fib_files = {
        "rrc00_v6",
        "rrc01_v6",
        "rrc03_v6",
        "rrc04_v6",
        "rrc05_v6",
        "rrc06_v6",
        "rrc07_v6",
        "rrc10_v6",
        "rrc11_v6",
        "rrc12_v6",
        "rrc13_v6",
        "rrc14_v6",
        "rrc15_v6",
        "rrc16_v6",
        "rrc18_v6",
        "rrc19_v6",
        "rrc20_v6",
        "rrc21_v6",
        "rrc22_v6",
        "rrc23_v6",
        "rrc24_v6",
        "rrc25_v6",
        "rrc26_v6",
        "isp1_kab",
        "isp2_orag",
        "isp3_dtag",
        "isp4_sky",
        "isp5_free",
        "isp6_netclg",
        "isp7_cmt",
        "AWS_256k_v6",
        "Azure_256k_v6",
        "Google_256k_v6",
        "host-rd64",
        "host-rd72",
        "host-rd80",
        "host-inc1",
        "host-inc3",
        "host-inc300"
    };
    
    std::vector<StatisticResult> results;
    std::vector<StatisticResult> ori_results;  // 原始trie的统计结果
    std::vector<CompressResult> compress_results;  // 压缩策略存储规模统计
    std::vector<std::string> processed_names;
    
    // 处理每个FIB文件
    for (const auto& fib_name : fib_files) {
        std::string fib_path = "../../dataset/" + fib_name + ".fib";
        
        std::cout << "Processing " << fib_name << "..." << std::endl;
        
        // 读取FIB文件
        std::vector<Prefix> prefixes = read_fib_file(fib_path);
        if (prefixes.empty()) {
            std::cerr << "Warning: No prefixes found in " << fib_path << ", skipping..." << std::endl;
            continue;
        }
        
        std::cout << "  Loaded " << prefixes.size() << " prefixes" << std::endl;
        
        // 构建Trie树
        Trie trie;
        for (const auto& prefix : prefixes) {
            trie.insert_prefix(prefix);
        }
        
        std::cout << "  Built Trie tree" << std::endl;
        
        // 在压缩之前，先计算原始trie的逆距离并收集统计
        trie.calculate_inverse_distance();
        std::cout << "  Calculated inverse distances (original)" << std::endl;
        
        StatisticResult ori_result;
        StatisticCollector::collect_statistics(trie, ori_result);
        std::cout << "  Collected statistics (original)" << std::endl;
        ori_results.push_back(ori_result);
        
        // 压缩
        trie.compress();
        std::cout << "  Compressed Trie tree" << std::endl;
        
        // 重新计算压缩后的逆距离
        trie.calculate_inverse_distance();
        std::cout << "  Calculated inverse distances (compressed)" << std::endl;
        
        // 收集压缩后的统计信息
        StatisticResult result;
        StatisticCollector::collect_statistics(trie, result);
        std::cout << "  Collected statistics (compressed)" << std::endl;
        
        results.push_back(result);
        processed_names.push_back(fib_name);

        // 构建森林 Trie，统计各压缩策略的存储规模
        ForestTrie forest;
        for (const auto& prefix : prefixes) {
            forest.insert_prefix(prefix);
        }
        CompressResult compress_result;
        collect_compress_stats(forest, compress_result);
        compress_results.push_back(compress_result);
        std::cout << "  Collected compress stats" << std::endl;
    }
    
    // 输出结果到CSV文件
    std::cout << "\nWriting results to CSV files..." << std::endl;
    
    write_csv_results(processed_names, results, "res_inv.csv");
    std::cout << "  Written res_inv.csv" << std::endl;
    
    write_csv_results(processed_names, ori_results, "res_ori_inv.csv");
    std::cout << "  Written res_ori_inv.csv" << std::endl;
    
    write_csv_results(processed_names, results, "res_count.csv");
    std::cout << "  Written res_count.csv" << std::endl;
    
    write_csv_results(processed_names, ori_results, "res_ori_count.csv");
    std::cout << "  Written res_ori_count.csv" << std::endl;
    
    write_csv_results(processed_names, results, "res_simi.csv");
    std::cout << "  Written res_simi.csv" << std::endl;
    
    write_csv_results(processed_names, ori_results, "res_ori_simi.csv");
    std::cout << "  Written res_ori_simi.csv" << std::endl;

    write_compress_csv(processed_names, compress_results, "res_compress.csv");
    std::cout << "  Written res_compress.csv" << std::endl;

    write_bitmap_address_bits_csv(processed_names, compress_results, "res_bitmap_address_bits.csv");
    std::cout << "  Written res_bitmap_address_bits.csv" << std::endl;

    std::cout << "\nAll done!" << std::endl;
    
    return 0;
}
