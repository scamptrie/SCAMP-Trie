#!/usr/bin/env python3
"""
计算 stride-k 多比特 Trie 的 bitmap 地址位数上界。

给定前缀数 N 和 stride k，地址长度上界为:
  ceil( log2( sum_{i=1}^{L+1} C(2^k-1, i) ) )
其中 L 满足:
  sum_{i=1}^{L}   C(2^k-1, i) * i  <=  N  <=  sum_{i=1}^{L+1} C(2^k-1, i) * i
表示不同内部前缀分布数量的上界。

参考文献中: N=1e6, k=6 时地址最多 20 bits，远少于 63。

另：bitmap 种类上确界：按「消耗 N」从少到多依次统计，前 C(K,0) 个不消耗，
之后 C(K,1) 个各消耗 1，依此类推，直至 N 耗尽（或下一档会超过 N），
此时已构建的 bitmap 个数即为极限情况下不同 bitmap 种类的上确界。
"""

import math
import argparse


def comb(n: int, k: int) -> int:
    """二项式系数 C(n,k)，k>n 时返回 0。"""
    if k < 0 or k > n:
        return 0
    return math.comb(n, k)


def find_L(N: int, m: int) -> int:
    """
    找到 L 使得 sum_{i=1}^{L} C(m,i)*i <= N <= sum_{i=1}^{L+1} C(m,i)*i。
    m = 2^k - 1。
    """
    if N <= 0:
        return 0
    L = 0
    # sum_lower = sum_{i=1}^{L} C(m,i)*i
    # sum_upper = sum_{i=1}^{L+1} C(m,i)*i
    sum_lower = 0
    while True:
        sum_upper = sum_lower + comb(m, L + 1) * (L + 1)
        if sum_lower <= N <= sum_upper:
            return L
        if sum_upper < N and L + 1 > m:
            # 已到 i 的上限 m，仍不足则取 L = m
            return m
        sum_lower = sum_upper
        L += 1
        if L > m:
            return m


def address_bits(N: int, k: int) -> int:
    """
    给定前缀数 N 和 stride k，返回 bitmap 地址位数上界（bits）。
    """
    m = (1 << k) - 1  # 2^k - 1
    if m <= 0:
        return 0
    L = find_L(N, m)
    # S = sum_{i=1}^{L+1} C(m, i)
    S = sum(comb(m, i) for i in range(1, L + 2))
    if S <= 0:
        return 0
    return math.ceil(math.log2(S))


def bitmap_types_supremum(N: int, k: int) -> int:
    """
    给定前缀数 N 和 stride k，计算不同 bitmap 种类的上确界。

    依次按档位统计：K=2^k-1 时，前 C(K,0) 个 bitmap 消耗 0，接下来 C(K,1) 个各消耗 1，
    C(K,2) 个各消耗 2，…，直至 N 被耗尽（或下一档总消耗超过 N）。
    返回此时已构建的 bitmap 总数（上确界）。
    """
    K = (1 << k) - 1
    if K <= 0:
        return 0
    used = 0
    count = 0
    for i in range(0, K + 1):
        num_types = comb(K, i)
        cost_this_tier = num_types * i
        if used + cost_this_tier <= N:
            count += num_types
            used += cost_this_tier
        else:
            remaining = N - used
            if i == 0:
                count += num_types
            else:
                count += remaining // i
            break
    return count


def main():
    parser = argparse.ArgumentParser(
        description="计算 stride-k 多比特 Trie 的 bitmap 地址位数上界"
    )
    parser.add_argument("N", type=int, help="前缀数量 (e.g. 1000000)")
    parser.add_argument("k", type=int, help="stride 位数 (e.g. 6)")
    args = parser.parse_args()
    N, k = args.N, args.k
    bits = address_bits(N, k)
    m = (1 << k) - 1
    L = find_L(N, m)
    S = sum(comb(m, i) for i in range(1, L + 2))
    supremum = bitmap_types_supremum(N, k)
    print(f"N = {N}, k = {k}")
    print(f"2^k - 1 = {m}")
    print(f"L = {L}")
    print(f"sum_{{i=1}}^{{L+1}} C({{2^k-1}}, i) = {S}")
    print(f"地址位数上界 = ceil(log2({S})) = {bits} bits")
    print(f"bitmap 种类上确界 = {supremum}")


if __name__ == "__main__":
    main()
