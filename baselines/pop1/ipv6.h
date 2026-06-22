#ifndef IPV6_H
#define IPV6_H

#include <cstdint>
#include <unordered_map>

#define IPV6_LEN 128

typedef uint64_t nexthop_t;

struct ip6_t
{
    uint64_t hi;
    uint64_t lo;
    uint8_t length;

    bool operator==(const ip6_t &other) const
    {
        return hi == other.hi && lo == other.lo && length == other.length;
    }
};

inline ip6_t mask_ipv6_with_len(ip6_t ip, uint8_t len)
{
    if (len > 64)
    {
        ip.lo >>= 128 - len;
        ip.lo <<= 128 - len;
    }
    else
    {
        ip.hi >>= 64 - len;
        ip.hi <<= 64 - len;
        ip.lo = 0;
    }
    ip.length = len;
    return ip;
}

struct ip6_hash
{
    std::size_t operator()(const ip6_t &ip) const
    {
        return std::hash<uint64_t>()(ip.hi) ^ std::hash<uint64_t>()(ip.lo) ^
               std::hash<int>()(ip.length);
    }
};

inline int lookup(std::unordered_map<ip6_t, int, ip6_hash> addr_maps[], ip6_t ip)
{
    for (int i = 128; i >= 0; i--)
    {
        auto key = mask_ipv6_with_len(ip, i);
        auto it = addr_maps[i].find(key);
        if (it != addr_maps[i].end())
        {
            return it->second;
        }
    }
    return -1;
}

#endif // IPV6_H