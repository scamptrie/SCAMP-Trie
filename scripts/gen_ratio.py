#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_ratio.py - 生成算法在所有数据集上的最优存储分配比例

算法思路：
1. 二分利用率 U (0 < U <= 1)，目标是找到最大的可行利用率
2. 对于每个数据集 k：
   - 计算总存储空间 C[k] = sum(当前级存储使用量) / U
   - 计算每个流水级 i 的最少分配比例 a[k][i] = values[i] / C[k]
3. 对于每个流水级 i，取所有数据集中的最大值：R_i = max_k(a[k][i])
   这样能保证在所有数据集上都能满足需求
4. 检查 sum(R_i) 是否 <= 1
   - 如果 <= 1，说明利用率 U 可行，可以尝试更大的 U
   - 如果 > 1，说明利用率 U 过大，需要减小
5. 二分到可行利用率后（精确度在百分之一之内），计算最终分配比例
6. 如果比例之和小于1，将最后一级设为 1-sum(其他级比例)，确保总和为1
"""

import os
import sys
import argparse
import random

# 默认数据集放在文件最前面，便于统一管理和引用
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
    
    n = int(lines[0])  # 第一行：流水级数
    values = list(map(int, lines[1].split()))  # 第二行：每个流水级的使用量
    
    if len(values) != n:
        raise ValueError(f"文件格式错误: {file_path}，流水级数不匹配")
    
    return n, values


def check_utilization_feasible(datasets_data, utilization):
    """
    检查给定的利用率 U 是否可行
    
    参数:
        datasets_data: 列表，每个元素是 (n, values) 表示一个数据集的内存分布
        utilization: 利用率 U (0 < U <= 1)
    
    返回: (is_feasible, ratios)
        is_feasible: 是否可行
        ratios: 如果可行，返回每个流水级的比例列表；否则返回 None
    """
    if not datasets_data:
        return False, None
    
    # 获取流水级数（所有数据集应该有相同的流水级数）
    n = datasets_data[0][0]
    for dataset_n, _ in datasets_data:
        if dataset_n != n:
            raise ValueError(f"数据集流水级数不一致: {n} vs {dataset_n}")
    
    # 步骤2: 对每个数据集 k，计算总存储空间 C[k] 和每个流水级的最少分配比例 a[k][i]
    # C[k] = sum(values[k]) / U
    # a[k][i] = values[k][i] / C[k] = values[k][i] * U / sum(values[k])
    
    # 步骤3: 对每个流水级 i，取所有数据集中的最大值
    max_ratios = [0.0] * n  # R_i = max_k(a[k][i])
    
    for dataset_n, values in datasets_data:
        total_usage = sum(values)  # 当前数据集的总使用量
        
        if total_usage == 0:
            continue  # 跳过总使用量为0的数据集
        
        # 计算每个流水级的最少分配比例
        for i in range(n):
            # a[k][i] = values[i] * U / total_usage
            ratio = values[i] * utilization / total_usage
            max_ratios[i] = max(max_ratios[i], ratio)
    
    # for i in range(n):
    #     max_ratios[i] = max(max_ratios[i], 0.01)
    
    # 步骤4: 检查所有流水级的比例之和是否 <= 1
    total_ratio = sum(max_ratios)
    is_feasible = total_ratio <= 1.0
    
    return is_feasible, max_ratios if is_feasible else None


def binary_search_utilization(datasets_data, precision=0.01, verbose=False):
    """
    二分搜索最大的可行利用率
    
    参数:
        datasets_data: 列表，每个元素是 (n, values) 表示一个数据集的内存分布
        precision: 精度要求（默认0.01，即百分之一）
        verbose: 是否输出详细调试信息
    
    返回: (best_utilization, best_ratios)
        best_utilization: 找到的最大可行利用率
        best_ratios: 对应的比例列表
    """
    # 二分搜索范围: [low, high]
    low = 0.0
    high = 1.0
    best_utilization = 0.0
    best_ratios = None
    iteration = 0
    
    # 二分搜索，直到精度满足要求
    while high - low > precision:
        iteration += 1
        mid = (low + high) / 2.0
        
        is_feasible, ratios = check_utilization_feasible(datasets_data, mid)
        
        if verbose:
            total_ratio = sum(ratios) if ratios else float('inf')
            status = "可行" if is_feasible else "不可行"
            print(f"  迭代 {iteration}: U={mid:.4f} ({mid*100:.2f}%), 比例之和={total_ratio:.6f}, {status}")
        
        if is_feasible:
            # 可行，尝试更大的利用率
            best_utilization = mid
            best_ratios = ratios
            low = mid
        else:
            # 不可行，减小利用率
            high = mid
    
    # 最后再检查一次 high 边界（可能由于精度问题，mid 略小于最优值）
    is_feasible, ratios = check_utilization_feasible(datasets_data, high)
    if is_feasible:
        best_utilization = high
        best_ratios = ratios
        if verbose:
            print(f"  最终检查: U={high:.4f} ({high*100:.2f}%), 比例之和={sum(ratios):.6f}, 可行")
    
    return best_utilization, best_ratios


def normalize_ratios(ratios):
    """
    归一化比例，确保总和为1
    
    如果比例之和小于1，将最后一级设为 1-sum(其他级比例)
    """
    if not ratios:
        return ratios
    
    total = sum(ratios)
    
    if total >= 1.0:
        # 如果总和 >= 1，直接归一化
        return [r / total for r in ratios]
    else:
        # 如果总和 < 1，将最后一级设为 1-sum(其他级比例)
        normalized = ratios.copy()
        other_sum = sum(ratios[:-1])
        normalized[-1] = 1.0 - other_sum
        return normalized


def write_ratio_file(output_path, n, ratios):
    """
    将比例写入文件
    
    文件格式:
        第一行: 流水级数 n
        第二行: 每个流水级的比例 R_i（空格分隔，整数形式，乘以1000000）
    """
    # 将比例转换为整数（乘以1000000，保留6位小数精度）
    ratio_ints = [int(r * 1000000) for r in ratios]
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"{n}\n")
        f.write(" ".join(map(str, ratio_ints)) + "\n")


def compute_min_utilization_for_ratios(datasets_data, ratios):
    """
    给定一组比例 ratios（已归一化，总和≈1），计算在所有 FIB 上的最小利用率。

    对于某个数据集的 values，给定公共比例 r_i 时：
        C = max_i(values[i] / r_i)
        U = sum(values) / C
    这里忽略 1024 对齐等额外开销，只关注流水线本身的利用率。
    """
    if not datasets_data or not ratios:
        return 0.0

    n = len(ratios)
    min_util = float("inf")

    for dataset_n, values in datasets_data:
        if dataset_n != n:
            raise ValueError(f"数据集流水级数不一致: {n} vs {dataset_n}")

        total_usage = sum(values)
        if total_usage <= 0:
            continue

        max_capacity = 0.0
        for v, r in zip(values, ratios):
            if v <= 0:
                continue  # 该级无使用量，对容量约束无影响
            if r <= 0:
                # 对有使用量的级别分配了 0 比例，视为不可行，利用率记为 0
                return 0.0
            cap = v / r
            if cap > max_capacity:
                max_capacity = cap

        if max_capacity <= 0:
            continue

        U = total_usage / max_capacity
        if U < min_util:
            min_util = U

    if min_util == float("inf"):
        return 0.0
    return min_util


def main():
    parser = argparse.ArgumentParser(description="生成算法在所有数据集上的最优存储分配比例")
    parser.add_argument("algo", help="算法名称（如 treebitmap）")
    parser.add_argument("--datasets", nargs="+", 
                       default=DEFAULT_DATASETS,
                       help=f"数据集名称列表（默认: {' '.join(DEFAULT_DATASETS)}）")
    parser.add_argument("--output-dir", default="output",
                       help="输出目录（默认: output）")
    parser.add_argument("--ratio-dir", default="ratio",
                       help="比例文件目录（默认: ratio）")
    parser.add_argument("--precision", type=float, default=0.0000001,
                       help="利用率精度（默认: 0.01，即百分之一）")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="输出详细的调试信息")
    parser.add_argument("--rand", action="store_true",
                       help="额外随机生成若干比例方案，对比是否存在更优的最小利用率")
    
    args = parser.parse_args()
    
    # 读取所有数据集的内存分布
    print(f"正在读取数据集的内存分布...")
    datasets_data = []
    for dataset_name in args.datasets:
        try:
            n, values = read_dataset_memory_distribution(args.output_dir, args.algo, dataset_name)
            datasets_data.append((n, values))
            total_usage = sum(values)
            print(f"  数据集 {dataset_name}: {n} 级流水线, 总使用量 {total_usage}")
        except Exception as e:
            print(f"  警告: 无法读取数据集 {dataset_name}: {e}")
            continue
    
    if not datasets_data:
        print("错误: 没有成功读取任何数据集")
        sys.exit(1)
    
    # 验证所有数据集的流水级数一致
    n = datasets_data[0][0]
    for dataset_n, _ in datasets_data:
        if dataset_n != n:
            print(f"错误: 数据集流水级数不一致: {n} vs {dataset_n}")
            sys.exit(1)
    
    print(f"\n所有数据集共有 {n} 级流水线")
    print(f"开始二分搜索最优利用率（精度: {args.precision}）...")
    if args.verbose:
        print("详细调试信息:")
    
    # 二分搜索最优利用率
    best_utilization, best_ratios = binary_search_utilization(datasets_data, args.precision, args.verbose)
    
    if best_ratios is None:
        print("错误: 无法找到可行的利用率")
        sys.exit(1)
    
    print(f"找到最优利用率: {best_utilization:.4f} ({best_utilization*100:.2f}%)")
    print(f"比例之和: {sum(best_ratios):.6f}")
    
    # 归一化比例
    normalized_ratios = normalize_ratios(best_ratios)
    print(f"归一化后比例之和: {sum(normalized_ratios):.6f}")

    # 如果需要，做随机比例搜索，对比最小利用率
    if args.rand:
        print("\n开始随机比例搜索，对比最小利用率（1000 组随机方案）...")
        rand_trials = 10000000
        rand_best_util = 0.0
        rand_best_ratios = None

        for t in range(1, rand_trials + 1):
            # 生成一组随机比例（Dirichlet-like）：正数后归一化
            raw = [random.random() for _ in range(n)]
            s = sum(raw)
            if s <= 0:
                continue
            cand_ratios = [x / s for x in raw]

            util = compute_min_utilization_for_ratios(datasets_data, cand_ratios)
            if util > rand_best_util:
                rand_best_util = util
                rand_best_ratios = cand_ratios

        print(f"随机搜索得到的最优最小利用率: {rand_best_util:.4f} ({rand_best_util*100:.2f}%)")
        if rand_best_util > best_utilization:
            print("注意: 随机方案中存在比当前二分结果更优的最小利用率，建议进一步研究该方案。")
        else:
            print("随机方案中未发现明显优于当前二分结果的最小利用率。")

    # 输出比例信息
    print(f"\n各流水级比例:")
    for i, ratio in enumerate(normalized_ratios):
        print(f"  第 {i+1} 级: {ratio:.6f} ({ratio*100:.4f}%)")
    
    # 写入文件
    output_path = os.path.join(args.ratio_dir, args.algo)
    write_ratio_file(output_path, n, normalized_ratios)
    print(f"\n比例文件已写入: {output_path}")


if __name__ == "__main__":
    main()
