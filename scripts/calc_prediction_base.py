#!/usr/bin/env python3
"""
以某一 FIB 的运行数据为基准，计算预测其他场景 FIB 所需存储时的基准指标。
导出 AA_prediction_base.csv 到 result 目录，含 6 行 3 列数据：
  数据集规模、eta_pp（平均每条 prefix 分配空间）、eta_util（空间利用率）
前 3 行均为 rrc 预测（基准 rrc00），后 3 行分别为 isp1、cloud1、host1 的基准数据。
"""

import os
import csv

# 算法名，用于读取 ratio 文件
ALGO = "tamptrie"
OUTPUT_BASE = os.path.join("output", ALGO)
DATASET_DIR = "dataset"
RESULT_DIR = "result"
OUTPUT_CSV = "AA_prediction_base.csv"

# 6 行对应的数据源：(场景名, output 下文件名, dataset 下 .fib 路径)
# 前 3 行都用 rrc00 的基准数据
ROWS_SPEC = [
    ("rrc", "rrc00_v6", "rrc00_v6.fib"),
    ("rrc", "rrc00_v6", "rrc00_v6.fib"),
    ("rrc", "rrc00_v6", "rrc00_v6.fib"),
    ("isp1", "isp1_kab", "isp1_kab.fib"),
    ("cloud1", "AWS_256k_v6", "AWS_256k_v6.fib"),
    ("host1", "host-rd64", "host-rd64.fib"),
]


def load_ratio():
    """加载 ratio 文件，返回 (ratio_n, ratio_r_i) 或 (None, None)。"""
    ratio_path = os.path.join("ratio", ALGO)
    if not os.path.exists(ratio_path):
        return None, None
    with open(ratio_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]
    if len(lines) < 2:
        return None, None
    ratio_n = int(lines[0])
    R_i = list(map(float, lines[1].split()))
    if len(R_i) != ratio_n:
        return None, None
    sum_R = sum(R_i)
    if sum_R <= 0:
        return None, None
    ratio_r_i = [R / sum_R for R in R_i]
    return ratio_n, ratio_r_i


def get_prefix_count(fib_path):
    """数据集规模：.fib 文件行数（等价于 wc -l）。"""
    if not os.path.exists(fib_path):
        return 0
    with open(fib_path, "r", encoding="utf-8") as f:
        return sum(1 for _ in f)


def compute_metrics_for_output(output_file_path, fib_path, ratio_n, ratio_r_i):
    """
    根据 util.py 的逻辑，从单份 output 文件计算：
    - dataset_scale: 前缀条数（.fib 行数）
    - eta_pp: 总使用量 / 前缀条数（平均每条 prefix 分配空间）
    - eta_util: 总使用量 / 总分配量（空间利用率）
    总分配量 = pipeline_total_capacity + fixed_storage_size + elastic_storage_aligned（与 util.py 一致）
    """
    dataset_scale = get_prefix_count(fib_path)
    if dataset_scale == 0:
        return dataset_scale, 0.0, 0.0

    with open(output_file_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]

    if len(lines) < 2:
        return dataset_scale, 0.0, 0.0

    n = int(lines[0])
    values = list(map(int, lines[1].split()))
    if len(values) != n:
        return dataset_scale, 0.0, 0.0

    fixed_storage_size = 0
    fixed_storage_used = 0
    elastic_storage_used = 0
    if len(lines) >= 5:
        if lines[2]:
            try:
                fixed_storage_size = int(lines[2])
            except ValueError:
                pass
        if lines[3]:
            try:
                fixed_storage_used = int(lines[3])
            except ValueError:
                pass
        if lines[4]:
            try:
                elastic_storage_used = int(lines[4])
            except ValueError:
                pass

    pipeline_used_memory = sum(values)
    if ratio_r_i is not None and ratio_n == n:
        pipeline_total_capacity = max(values[i] / ratio_r_i[i] for i in range(n))
    else:
        C = max(values)
        pipeline_total_capacity = C * n
    pipeline_total_capacity = ((int(pipeline_total_capacity) + 1023) // 1024) * 1024

    elastic_storage_aligned = (
        ((elastic_storage_used + 1023) // 1024) * 1024 if elastic_storage_used > 0 else 0
    )

    total_used = pipeline_used_memory + fixed_storage_used + elastic_storage_used
    total_capacity = pipeline_total_capacity + fixed_storage_size + elastic_storage_aligned

    eta_pp = total_used / dataset_scale if dataset_scale > 0 else 0.0
    eta_util = total_used / total_capacity if total_capacity > 0 else 0.0

    return dataset_scale, eta_pp, eta_util


def main():
    os.makedirs(RESULT_DIR, exist_ok=True)
    ratio_n, ratio_r_i = load_ratio()

    rows = []
    for scene, output_name, fib_name in ROWS_SPEC:
        output_path = os.path.join(OUTPUT_BASE, output_name)
        fib_path = os.path.join(DATASET_DIR, fib_name)
        if not os.path.exists(output_path):
            print(f"Warning: missing {output_path}, using zeros for {scene}")
            scale, eta_pp, eta_util = 0, 0.0, 0.0
        else:
            scale, eta_pp, eta_util = compute_metrics_for_output(
                output_path, fib_path, ratio_n, ratio_r_i
            )
        rows.append((scene, scale, eta_pp, eta_util))

    out_path = os.path.join(RESULT_DIR, OUTPUT_CSV)
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scene", "dataset_scale", "eta_pp", "eta_util"])
        for r in rows:
            w.writerow([r[0], r[1], f"{r[2]:.4f}", f"{r[3]:.4f}"])

    print(f"Exported: {out_path}")


if __name__ == "__main__":
    main()
