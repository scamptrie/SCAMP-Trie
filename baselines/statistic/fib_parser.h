#ifndef FIB_PARSER_H
#define FIB_PARSER_H

#include <string>
#include <vector>
#include <cstdint>

struct Prefix {
    uint64_t ip_high;  // 高64位
    uint64_t ip_low;   // 低64位
    uint8_t length;    // 前缀长度
    
    Prefix(uint64_t hi, uint64_t lo, uint8_t len) 
        : ip_high(hi), ip_low(lo), length(len) {}
};

std::vector<Prefix> read_fib_file(const std::string& filepath);

#endif // FIB_PARSER_H
