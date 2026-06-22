#include "fib_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::vector<Prefix> read_fib_file(const std::string& filepath) {
    std::vector<Prefix> prefixes;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filepath << std::endl;
        return prefixes;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 去除空白字符
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // 查找 '/' 分隔符
        size_t slash_pos = line.find('/');
        if (slash_pos == std::string::npos) continue;
        
        // 解析前缀长度
        std::string len_str = line.substr(slash_pos + 1);
        uint8_t length = static_cast<uint8_t>(std::stoul(len_str));
        
        // 解析IP地址（格式：<IP_High>.<IP_Low>）
        std::string ip_str = line.substr(0, slash_pos);
        size_t dot_pos = ip_str.find('.');
        if (dot_pos == std::string::npos) continue;
        
        std::string hi_str = ip_str.substr(0, dot_pos);
        std::string lo_str = ip_str.substr(dot_pos + 1);
        
        uint64_t ip_high = std::stoull(hi_str, nullptr, 16);
        uint64_t ip_low = std::stoull(lo_str, nullptr, 16);
        
        prefixes.emplace_back(ip_high, ip_low, length);
    }
    
    file.close();
    return prefixes;
}
