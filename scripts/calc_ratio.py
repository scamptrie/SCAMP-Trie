#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
calc_ratio.py - 按“最大需求”直接计算存储分配比例

算法思路（与 gen_ratio.py 的二分法不同，此处为闭式解法）：
1. 第 t 个 FIB 在流水级 i 的用量为 f[t][i]（即 values[t][i]）
2. 比例通用，故第 i 级至少需要满足所有 FIB 中该级需求的最大相对比例：
   r[i] = max_t ( f[t][i] / Σ_j f[t][j] )  即第 i 级取“各 FIB 中该级占比”的最大值
3. 分配比例总和为 1，故归一化：r'[i] = r[i] / Σ r
4. 该比例下，各 FIB 最小利用率的最大值为 1 / Σ r
"""

# 默认是否施加每级比例下限；可用 --no-min-floor 关闭
APPLY_MIN_FLOOR_DEFAULT = True
MIN_FLOOR = 0.02

import os
import sys
import argparse

# 默认数据集（与 gen_ratio.py 一致）
DEFAULT_DATASETS = [
    "rrc00_v6",
    "isp1_kab",
    "AWS_256k_v6",
    "host-rd64",
]


def read_dataset_memory_distribution(output_dir, algo, dataset_name):
    """
    从 output/<算法名>/<数据集名> 文件中读取内存分布

    返回: (n, values) 其中 n 是流水级数，values 是每个流水级的使用量列表
    """
    file_path = os.path.join(output_dir, algo, dataset_name)

    if not os.path.exists(file_path):
        raise FileNotFoundError(f"文件不存在: {file_path}")

    with open(file_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]

    if len(lines) < 2:
        raise ValueError(f"文件格式错误: {file_path}，至少需要2行")

    n = int(lines[0])
    values = list(map(int, lines[1].split()))

    if len(values) != n:
        raise ValueError(f"文件格式错误: {file_path}，流水级数不匹配")

    return n, values


def compute_ratios_by_max_share(datasets_data):
    """
    按思路计算比例与最大最小利用率。

    对每个流水级 i：r[i] = max_t ( values[t][i] / sum(values[t]) )
    归一化：r'[i] = r[i] / sum(r)
    最大最小利用率 = 1 / sum(r)

    参数:
        datasets_data: 列表，每个元素为 (n, values) 表示一个数据集的内存分布

    返回: (unnorm_sum, normalized_ratios, max_min_utilization)
        unnorm_sum: Σ r（归一化前比例之和）
        normalized_ratios: 归一化后的比例列表 r'
        max_min_utilization: 1 / Σ r
    """
    if not datasets_data:
        return 0.0, [], 0.0

    n = datasets_data[0][0]
    for dataset_n, _ in datasets_data:
        if dataset_n != n:
            raise ValueError(f"数据集流水级数不一致: {n} vs {dataset_n}")

    # r[i] = max_t ( f[t][i] / sum(f[t]) )
    r = [0.0] * n
    for dataset_n, values in datasets_data:
        total = sum(values)
        if total <= 0:
            continue
        for i in range(n):
            share = values[i] / total
            if share > r[i]:
                r[i] = share

    # 避免全 0
    for i in range(n):
        r[i] = max(r[i], 1e-9)

    unnorm_sum = sum(r)
    if unnorm_sum <= 0:
        return 0.0, [1.0 / n] * n, 0.0

    # r'[i] = r[i] / Σ r
    normalized = [x / unnorm_sum for x in r]
    # 各 FIB 最小利用率的最大值 = 1 / Σ r
    max_min_util = 1.0 / unnorm_sum

    return unnorm_sum, normalized, max_min_util


def apply_min_floor(ratios, min_floor):
    """
    保证每一级比例至少为 min_floor，总和仍为 1。

    做法：每级先占满 min_floor，剩余 (1 - n*min_floor) 按“超出下限的部分”比例分配。
    若某级原始比例 <= min_floor，则只保留 min_floor，不参与剩余分配。
    """
    if not ratios or min_floor <= 0:
        return ratios
    n = len(ratios)
    if n * min_floor > 1.0:
        # 级数太多时无法保证每级都 >= min_floor，退化为均分
        return [1.0 / n] * n
    remainder = 1.0 - n * min_floor
    excess = [max(0.0, r - min_floor) for r in ratios]
    total_excess = sum(excess)
    if total_excess > 0:
        return [min_floor + remainder * (excess[i] / total_excess) for i in range(n)]
    # 所有级都不超过 min_floor，剩余部分均分
    return [min_floor + remainder / n for _ in range(n)]


def normalize_ratios(ratios):
    """
    归一化比例，确保总和为 1（与 gen_ratio.py 一致）。
    若总和 < 1，将最后一级设为 1 - sum(其他级)。
    """
    if not ratios:
        return ratios
    total = sum(ratios)
    if total >= 1.0:
        return [x / total for x in ratios]
    normalized = ratios.copy()
    other_sum = sum(ratios[:-1])
    normalized[-1] = 1.0 - other_sum
    return normalized


def write_ratio_file(output_path, n, ratios):
    """
    将比例写入文件（格式与 gen_ratio.py 一致）：
    第一行: n
    第二行: 各比例乘以 1000000 的整数，空格分隔
    """
    ratio_ints = [int(r * 1000000) for r in ratios]
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"{n}\n")
        f.write(" ".join(map(str, ratio_ints)) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="按最大需求比例计算存储分配（闭式解法），并输出比例与最大最小利用率"
    )
    parser.add_argument("algo", help="算法名称（如 treebitmap）")
    parser.add_argument(
        "--datasets",
        nargs="+",
        default=DEFAULT_DATASETS,
        help=f"数据集名称列表（默认: {' '.join(DEFAULT_DATASETS)}）",
    )
    parser.add_argument("--output-dir", default="output", help="输出目录（默认: output）")
    parser.add_argument("--ratio-dir", default="ratio", help="比例文件目录（默认: ratio）")
    parser.add_argument(
        "--no-min-floor",
        action="store_true",
        help="不施加每级比例下限（忽略 MIN_FLOOR）",
    )

    args = parser.parse_args()
    use_min_floor = APPLY_MIN_FLOOR_DEFAULT and not args.no_min_floor

    print("正在读取数据集的内存分布...")
    datasets_data = []
    for dataset_name in args.datasets:
        try:
            n, values = read_dataset_memory_distribution(
                args.output_dir, args.algo, dataset_name
            )
            datasets_data.append((n, values))
            total_usage = sum(values)
            print(f"  数据集 {dataset_name}: {n} 级流水线, 总使用量 {total_usage}")
        except (OSError, ValueError) as exc:
            parser.exit(1, f"Cannot compute the allocation without {dataset_name}: {exc}\n")

    if not datasets_data:
        print("错误: 没有成功读取任何数据集")
        sys.exit(1)

    n = datasets_data[0][0]
    for dataset_n, _ in datasets_data:
        if dataset_n != n:
            print(f"错误: 数据集流水级数不一致: {n} vs {dataset_n}")
            sys.exit(1)

    print(f"\n所有数据集共有 {n} 级流水线")
    print("按闭式解法计算比例（r[i]=max_t 该级占比, r'=r/Σr, 最大最小利用率=1/Σr）...")

    unnorm_sum, normalized_ratios, max_min_util = compute_ratios_by_max_share(
        datasets_data
    )

    print(f"归一化前比例之和 Σr = {unnorm_sum:.6f}")
    print(f"最大最小利用率 = 1/Σr = {max_min_util:.4f} ({max_min_util*100:.2f}%)")
    print(f"归一化后比例之和: {sum(normalized_ratios):.6f}")

    # 若开启每级容量下限，则每级至少 MIN_FLOOR，再归一化
    if use_min_floor:
        ratios_before_final = apply_min_floor(normalized_ratios, min_floor=MIN_FLOOR)
    else:
        ratios_before_final = normalized_ratios
    # 与 gen_ratio 一致：若和 < 1 则最后一级补齐
    final_ratios = normalize_ratios(ratios_before_final)
    print(f"最终写入比例之和: {sum(final_ratios):.6f}")

    print("\n各流水级比例:")
    for i, ratio in enumerate(final_ratios):
        print(f"  第 {i+1} 级: {ratio:.6f} ({ratio*100:.4f}%)")

    output_path = os.path.join(args.ratio_dir, args.algo)
    write_ratio_file(output_path, n, final_ratios)
    print(f"\n比例文件已写入: {output_path}")


if __name__ == "__main__":
    main()
