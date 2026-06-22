#ifndef IPV6_HPP
#define IPV6_HPP

#include <random>
#include <unordered_map>
#include <stdint.h>

static std::random_device __rd;
static std::mt19937 __gen(__rd());
static std::uniform_int_distribution<uint64_t> __dis64;
static std::uniform_int_distribution<uint32_t> __dis32;
static std::uniform_int_distribution<uint8_t> __dis8;

/* IPv6 */

typedef struct ipv6_t {
    uint64_t hi;
    uint64_t lo;

    bool operator==(const struct ipv6_t& s) const {
        return hi == s.hi && lo == s.lo;
    }

    bool operator!=(const struct ipv6_t& s) const {
        return hi != s.hi || lo != s.lo;
    }
} ipv6_t;

static inline uint32_t
hash_add(uint32_t hash, uint32_t data) {
	data *= 0xcc9e2d51;
	data = (data << 15) | (data >> (32 - 15));
	data *= 0x1b873593;
	hash = hash ^ data;
	hash = (hash << 13) | (hash >> (32 - 13));
	return hash * 5 + 0xe6546b64;
}

static inline uint32_t
hash_finish(uint32_t hash, uint32_t final) {
	hash ^= final;
	hash ^= hash >> 16;
	hash *= 0x85ebca6b;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35;
	hash ^= hash >> 16;
	return hash;
}

struct ipv6_hash {
	size_t operator()(const struct ipv6_t& x) const {
		uint32_t seed = hash_add(0, x.hi >> 32);
		seed = hash_add(seed, x.hi & 0xffffffff);
		seed = hash_add(seed, x.lo >> 32);
		seed = hash_add(seed, x.lo & 0xffffffff);
		return (size_t)hash_finish(seed, 16);
	}
};

/* Prefix */
struct Prefix {
    ipv6_t ipv6;
    uint8_t len;
};

/* FIB */

struct FIB {
    std::unordered_map<ipv6_t, bool, struct ipv6_hash> pfx_maps[129];

    void clear() {
        for (int i = 0; i < 129; ++ i) {
            pfx_maps[i].clear();
        }
    }
};

static inline ipv6_t
mask_ipv6_with_len(ipv6_t ip, uint8_t len)
{
	if (len > 64) {
		ip.lo >>= 128 - len;
		ip.lo <<= 128 - len;
	} else {
		ip.hi >>= 64 - len;
		ip.hi <<= 64 - len;
		ip.lo = 0;
	}
	return ip;
}

static inline ipv6_t
getRandomIPv6(uint8_t len)
{
    ipv6_t ip;

    if (len > 64) {
        ip.hi = 0;
        ip.lo = __dis64(__gen);

        auto rest = 128 - len; // [0, 64)
        ip.lo &= ((1ULL << rest) - 1);
		// if (len == 72) {
		// 	printf("len=%d, ip=%016llX.%016llX\n", len, ip.hi, ip.lo);
		// 	printf("rest=%d, mask=%016llX\n", rest, (1ULL << rest) - 1);
		// 	exit(1);
		// }
    } else {
        ip.hi = __dis64(__gen);
        ip.lo = __dis64(__gen);

        if (len > 0) {
            ip.hi &= ((1ULL << (64 - len)) - 1);
        }
    }

    return ip;
}

static inline ipv6_t& operator |= (ipv6_t& a, const ipv6_t& b)
{
	a.hi |= b.hi;
	a.lo |= b.lo;
	return a;
}

#endif  // IPV6_HPP