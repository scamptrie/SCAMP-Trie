#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
calc_num_prefixes.py - 在给定 tamptrie 总空间 C 下，二分各算法最多能容纳的前缀条数

对每个数据集：
1. 计算前缀条数 P，用 tamptrie 得到总分配量 C
2. 生成随机排列，按该顺序依次“插入”前缀
3. 对每个算法二分前缀条数 n，使该算法在前 n 条前缀上的总分配量 C0 <= C，得到最大 n0
4. 输出 result/num_prefixes.csv（或指定算法时为 num_prefixes_<算法名>.csv）
"""

import os
import sys
import argparse
import random
import subprocess
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Optional

# 与 launch.sh 中 ALGO_COMMANDS 的键一致（用于枚举算法、过滤临时文件名）
ALGOS = [
    "random", "olp", "biolp", "treebitmap", "poptrie", "pop1",
    "numpy", "minmax", "2-phase", "cpe", "tamptrie-s",
]

def get_algos_for_num_prefixes():
    """参与“前缀条数”对比的算法：除 tamptrie 外所有在 launch.sh 中配置的算法。"""
    return list(ALGOS)

# tamptrie 仅用于读取 C，不参与二分
TAMPTRIE_ALGO = "tamptrie"

# 调试：是否在 output 文件末尾追加空行、总使用量、总分配量（与 util.py 一致，总分配量即与 tamptrie 对比的值）
APPEND_TOTAL_USAGE = True

# 与 util.py 一致的数据集顺序（不含 host-inc 系列与 sample 系列）
DESIRED_DATASET_ORDER = [
    "rrc00", "rrc01", "rrc03", "rrc04", "rrc05", "rrc06", "rrc07", "rrc10",
    "rrc11", "rrc12", "rrc13", "rrc14", "rrc15", "rrc16", "rrc18", "rrc19",
    "rrc20", "rrc21", "rrc22", "rrc23", "rrc24", "rrc25", "rrc26",
    "isp1_kab_256k", "isp2_orag_256k", "isp3_dtag_256k", "isp4_sky_256k",
    "isp5_free_256k", "isp6_netclg_256k", "isp7_cmt_256k",
    "AWS_256k", "Azure_256k", "Google_256k",
    "host_rd64_256k", "host_rd72_256k", "host_rd80_256k",
]
# 只跑这些数据集时在此列出，None 或 [] 表示跑全部（仍可被 --datasets 进一步限制）
RUN_DATASETS = None  # 例如 ["rrc00", "rrc01", "rrc03"]
# RUN_DATASETS = ["tmp0", "tmp1", "tmp2"]

def _normalize_dataset_name(n):
    """与 util.py 的 normalize_name 一致，用于匹配数据集。"""
    nn = n.lower().replace("-", "_")
    if nn.endswith("_v6"):
        nn = nn[:-3]
    if nn.endswith(".fib"):
        nn = nn[:-4]
    return nn


def _is_excluded_dataset(name: str) -> bool:
    """排除 host-inc 系列与 sample 系列数据集。"""
    nn = _normalize_dataset_name(name)
    if "host_inc" in nn or nn.startswith("host-inc"):
        return True
    if "sample" in nn.lower():
        return True
    return False


def order_base_datasets(dataset_names: list) -> list:
    """按 util.py 的 desired_order 对数据集名排序，未在顺序表中的按原顺序接在后面。"""
    normalized = {name: _normalize_dataset_name(name) for name in dataset_names}
    ordered = []
    used = set()
    for desired in DESIRED_DATASET_ORDER:
        dnorm = desired.lower().replace("-", "_")
        dnorm_short = dnorm[:-5] if dnorm.endswith("_256k") else dnorm
        for name, nn in normalized.items():
            if name in used:
                continue
            if nn == dnorm or nn == dnorm_short or nn.startswith(dnorm_short) or dnorm_short.startswith(nn) or dnorm_short in nn:
                ordered.append(name)
                used.add(name)
                break
    for name in dataset_names:
        if name not in used:
            ordered.append(name)
    return ordered


def _get_total_used_from_output_file(filepath: str) -> Optional[int]:
    """从算法输出文件中解析总使用量（与 util.py 一致：流水级使用量 + fixed_used + elastic_used）。"""
    if not os.path.isfile(filepath):
        return None
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            lines = [line.rstrip("\n\r") for line in f.readlines()]
    except Exception:
        return None
    if len(lines) < 2:
        return None
    try:
        n = int(lines[0])
        values = list(map(int, lines[1].split()))
        if len(values) != n:
            return None
        fixed_storage_used = 0
        elastic_storage_used = 0
        if len(lines) >= 5:
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
        return sum(values) + fixed_storage_used + elastic_storage_used
    except (ValueError, IndexError):
        return None


def _append_total_usage_to_output_file(filepath: str, algo: str, ratio_dir: str) -> None:
    """在 output 文件末尾追加一行空行、总使用量、总分配量（仅当 APPEND_TOTAL_USAGE 为 True 时生效）。"""
    if not APPEND_TOTAL_USAGE:
        return
    total_used = _get_total_used_from_output_file(filepath)
    total_capacity = get_total_capacity(filepath, algo, ratio_dir)
    if total_used is None or total_capacity is None:
        return
    cap_str = str(int(total_capacity))
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            lines = [line.rstrip("\n\r") for line in f.readlines()]
    except Exception:
        return
    # 避免重复追加：若末尾已是总使用量、总分配量则跳过
    if len(lines) >= 7 and lines[-2].strip() == str(total_used) and lines[-1].strip() == cap_str:
        return
    try:
        with open(filepath, "a", encoding="utf-8") as f:
            f.write("\n\n")
            f.write(str(total_used))
            f.write("\n")
            f.write(cap_str)
            f.write("\n")
    except Exception:
        pass


def _read_ratio_file(ratio_dir: str, algo: str):
    """读取 ratio/<algo>，返回 (n, ratio_r_i) 或 (None, None)。"""
    path = os.path.join(ratio_dir, algo)
    if not os.path.exists(path):
        return None, None
    with open(path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]
    if len(lines) < 2:
        return None, None
    n = int(lines[0])
    R_i = list(map(float, lines[1].split()))
    if len(R_i) != n:
        return None, None
    s = sum(R_i)
    if s <= 0:
        return None, None
    ratio_r_i = [x / s for x in R_i]
    return n, ratio_r_i


def get_total_capacity(output_file_path: str, algo: str, ratio_dir: str = "ratio"):
    """
    从算法输出文件中计算总分配量（与 util.py 一致）。
    若文件不存在或格式错误返回 None。
    """
    if not os.path.exists(output_file_path):
        return None
    with open(output_file_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]
    if len(lines) < 2:
        return None
    n = int(lines[0])
    values = list(map(int, lines[1].split()))
    if len(values) != n:
        return None

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

    C = max(values)
    ratio_n, ratio_r_i = _read_ratio_file(ratio_dir, algo)
    if ratio_r_i is not None and ratio_n == n:
        pipeline_total_capacity = max(values[i] / ratio_r_i[i] for i in range(n))
    else:
        pipeline_total_capacity = C * n
    pipeline_total_capacity = ((int(pipeline_total_capacity) + 1023) // 1024) * 1024
    elastic_storage_aligned = ((elastic_storage_used + 1023) // 1024) * 1024 if elastic_storage_used > 0 else 0
    total_capacity = pipeline_total_capacity + fixed_storage_size + elastic_storage_aligned
    return total_capacity


def get_base_datasets(dataset_dir: str, algos: list):
    """列出“基础”数据集：dataset/*.fib，排除临时文件 <名>_<算法>_<数字>.fib。"""
    if not os.path.isdir(dataset_dir):
        return []
    out = []
    for f in os.listdir(dataset_dir):
        if not f.endswith(".fib"):
            continue
        name = f[:-4]
        parts = name.rsplit("_", 2)
        if len(parts) == 3 and parts[1] in algos and parts[2].isdigit():
            continue
        out.append(name)
    return sorted(out)


def cleanup_generated_output(output_dir: str, algos: list) -> int:
    """
    删除 output 中由本脚本生成的所有输出（即 <某名>_<算法>_<数字> 形式的文件）。
    返回删除的文件数。
    """
    removed = 0
    for algo in algos:
        algo_out_dir = os.path.join(output_dir, algo)
        if not os.path.isdir(algo_out_dir):
            continue
        suffix_pattern = "_" + algo + "_"
        for f in os.listdir(algo_out_dir):
            if suffix_pattern in f:
                parts = f.rsplit("_", 2)
                if len(parts) == 3 and parts[1] == algo and parts[2].isdigit():
                    p = os.path.join(algo_out_dir, f)
                    try:
                        os.remove(p)
                        removed += 1
                    except Exception:
                        pass
    return removed


def cleanup_generated_dataset(dataset_dir: str, algos: list) -> int:
    """
    删除 dataset 中由本脚本生成的所有临时 .fib 文件（即 <某名>_<算法>_<数字>.fib 形式的文件）。
    返回删除的文件数。
    """
    if not os.path.isdir(dataset_dir):
        return 0
    removed = 0
    for f in os.listdir(dataset_dir):
        if not f.endswith(".fib"):
            continue
        name = f[:-4]
        parts = name.rsplit("_", 2)
        if len(parts) == 3 and parts[1] in algos and parts[2].isdigit():
            p = os.path.join(dataset_dir, f)
            try:
                os.remove(p)
                removed += 1
            except Exception:
                pass
    return removed


def run_algo(algo: str, dataset_name: str, project_root: str) -> bool:
    """通过 launch.sh 运行算法：launch.sh -a <algo> -d <dataset_name>。返回是否成功。"""
    launch_sh = os.path.join(project_root, "launch.sh")
    if not os.path.isfile(launch_sh):
        return False
    # 统一用 bash 调用，避免依赖脚本可执行位；Windows 下需安装 bash（如 Git Bash）
    cmd = ["bash", launch_sh, "-a", algo, "-d", dataset_name]
    try:
        ret = subprocess.run(
            cmd,
            cwd=project_root,
            capture_output=True,
            timeout=600,
        )
        return ret.returncode == 0
    except Exception:
        return False


def _process_one_dataset(
    index: int,
    dataset_name: str,
    project_root: str,
    dataset_dir: str,
    output_dir: str,
    ratio_dir: str,
    algos: list,
    seed: Optional[int],
    print_lock: Optional[threading.Lock],
):
    """
    处理单个数据集，返回 (index, row) 或 (index, None)（跳过时）。
    每个数据集只读写以 dataset_name 为前缀的文件，并行时无冲突。
    """
    def log(msg: str):
        if print_lock:
            with print_lock:
                print(msg)
        else:
            print(msg)

    fib_path = os.path.join(dataset_dir, f"{dataset_name}.fib")
    if not os.path.isfile(fib_path):
        log(f"  跳过（无 .fib）: {dataset_name}")
        return (index, None)

    with open(fib_path, "r", encoding="utf-8") as f:
        all_lines = [line.rstrip("\n\r") for line in f]
    P = len(all_lines)
    if P == 0:
        log(f"  跳过（空）: {dataset_name}")
        return (index, None)

    tamptrie_out = os.path.join(output_dir, TAMPTRIE_ALGO, dataset_name)
    C = get_total_capacity(tamptrie_out, TAMPTRIE_ALGO, ratio_dir)
    if C is None:
        log(f"  跳过（无 tamptrie 输出或无法计算 C）: {dataset_name}")
        return (index, None)

    rng = random.Random(seed)
    permuted = all_lines.copy()
    rng.shuffle(permuted)

    row = [dataset_name, str(P), str(int(C))]
    for algo in algos:
        low, high = 1, P
        n0 = 0
        temp_fib_base = f"{dataset_name}_{algo}_"
        while low <= high:
            mid = (low + high) // 2
            temp_name = f"{temp_fib_base}{mid}"
            temp_fib_path = os.path.join(dataset_dir, f"{temp_name}.fib")
            try:
                with open(temp_fib_path, "w", encoding="utf-8") as tf:
                    for i in range(mid):
                        tf.write(permuted[i] + "\n")
            except Exception as e:
                log(f"    写临时文件失败 {temp_name}: {e}")
                high = mid - 1
                continue

            try:
                if not run_algo(algo, temp_name, project_root):
                    high = mid - 1
                    continue
                algo_out = os.path.join(output_dir, algo, temp_name)
                C0 = get_total_capacity(algo_out, algo, ratio_dir)
                if C0 is not None and C0 <= C:
                    n0 = mid
                    low = mid + 1
                else:
                    high = mid - 1
            finally:
                if os.path.exists(temp_fib_path):
                    try:
                        os.remove(temp_fib_path)
                    except Exception:
                        pass

        # 删除 output 中该 (dataset, algo) 的临时结果，仅保留：满足条件的最大 n0、不满足条件的最小 n0（即 n0+1）
        algo_out_dir = os.path.join(output_dir, algo)
        prefix = f"{dataset_name}_{algo}_"
        keep_n = {n0}
        if n0 + 1 <= P:
            keep_n.add(n0 + 1)
        if os.path.isdir(algo_out_dir):
            for f in os.listdir(algo_out_dir):
                if f.startswith(prefix):
                    suffix = f[len(prefix) :]
                    if suffix.isdigit() and int(suffix) not in keep_n:
                        p = os.path.join(algo_out_dir, f)
                        try:
                            os.remove(p)
                        except Exception:
                            pass

        # 调试：对保留的 output 文件末尾追加空行、总使用量、总分配量
        for n in keep_n:
            out_path = os.path.join(algo_out_dir, f"{dataset_name}_{algo}_{n}")
            if os.path.isfile(out_path):
                _append_total_usage_to_output_file(out_path, algo, ratio_dir)

        row.append(str(n0))
        log(f"  {dataset_name} / {algo}: n0={n0}")

    return (index, row)


def main():
    parser = argparse.ArgumentParser(
        description="在 tamptrie 总空间 C 下，二分各算法最多能容纳的前缀条数，输出 num_prefixes.csv"
    )
    parser.add_argument(
        "algo",
        nargs="?",
        default=None,
        help="若指定，则只对该算法执行并输出 num_prefixes_<算法名>.csv",
    )
    parser.add_argument("--dataset-dir", default="dataset", help="数据集目录")
    parser.add_argument("--output-dir", default="output", help="算法输出目录")
    parser.add_argument("--result-dir", default="result", help="结果目录")
    parser.add_argument("--ratio-dir", default="ratio", help="比例文件目录")
    parser.add_argument("--seed", type=int, default=None, help="随机排列的种子，默认随机")
    parser.add_argument("--datasets", nargs="*", help="限定数据集名列表，默认全部基础数据集")
    parser.add_argument("-j", "--jobs", type=int, default=1, help="数据集层面并行数（默认 1 即串行）")
    parser.add_argument("-n", "--rounds", type=int, default=1, help="每个数据集运行几轮，取 n 轮的平均值（默认 1）")
    parser.add_argument("--cleanup", action="store_true", help="仅清理 output 中由本脚本生成的临时输出文件，然后退出")
    args = parser.parse_args()

    project_root = os.path.abspath(os.path.dirname(__file__))
    dataset_dir = os.path.join(project_root, args.dataset_dir)
    output_dir = os.path.join(project_root, args.output_dir)
    result_dir = os.path.join(project_root, args.result_dir)
    ratio_dir = os.path.join(project_root, args.ratio_dir)

    if args.algo:
        algos = [args.algo]
        if args.algo not in ALGOS:
            print(f"错误: 未知算法 '{args.algo}'，或该算法未在 launch.sh 中配置")
            sys.exit(1)
        if args.algo == TAMPTRIE_ALGO:
            print("错误: 不能仅指定 tamptrie，请指定其他算法或留空以对所有算法执行")
            sys.exit(1)
    else:
        algos = get_algos_for_num_prefixes()

    if args.cleanup:
        n_out = cleanup_generated_output(output_dir, algos)
        n_ds = cleanup_generated_dataset(dataset_dir, algos)
        print(f"已从 output 中删除 {n_out} 个、从 dataset 中删除 {n_ds} 个由本脚本生成的临时文件。")
        return

    # 基础数据集列表（排除临时文件、host-inc 系列、sample 系列），并按 util.py 的顺序排列
    all_algos_for_filter = set(ALGOS) | {TAMPTRIE_ALGO}
    base_datasets = get_base_datasets(dataset_dir, list(all_algos_for_filter))
    base_datasets = [d for d in base_datasets if not _is_excluded_dataset(d)]
    if RUN_DATASETS:
        base_datasets = [d for d in base_datasets if d in RUN_DATASETS]
    if args.datasets:
        base_datasets = [d for d in base_datasets if d in args.datasets]
    base_datasets = order_base_datasets(base_datasets)
    if not base_datasets:
        print("错误: 没有找到任何基础数据集")
        sys.exit(1)

    if args.rounds < 1:
        args.rounds = 1

    os.makedirs(result_dir, exist_ok=True)

    # 表头：数据集名, 前缀条数P, tamptrie总空间C, 各算法的 n0
    csv_rows = []
    header = ["dataset", "num_prefixes", "tamptrie_capacity"] + algos
    csv_rows.append(header)

    if args.jobs < 1:
        args.jobs = 1
    print_lock = threading.Lock() if args.jobs > 1 else None

    def _average_rows(round_rows: list) -> list:
        """对多轮结果按列取平均，前三列（dataset, num_prefixes, tamptrie_capacity）取首行，算法列取平均并四舍五入。"""
        if len(round_rows) == 1:
            return round_rows[0]
        avg_row = [round_rows[0][0], round_rows[0][1], round_rows[0][2]]
        for col_idx in range(3, len(round_rows[0])):
            vals = [float(r[col_idx]) for r in round_rows]
            avg_row.append(str(round(sum(vals) / len(vals))))
        return avg_row

    if args.jobs <= 1:
        # 串行：按 base_datasets 顺序依次处理，每个数据集跑 n 轮后取平均
        for idx, dataset_name in enumerate(base_datasets):
            round_rows = []
            for round_idx in range(args.rounds):
                seed = (args.seed + round_idx) if args.seed is not None else round_idx
                _, row = _process_one_dataset(
                    idx,
                    dataset_name,
                    project_root,
                    dataset_dir,
                    output_dir,
                    ratio_dir,
                    algos,
                    seed,
                    print_lock,
                )
                if row is not None:
                    round_rows.append(row)
            if round_rows:
                csv_rows.append(_average_rows(round_rows))
    else:
        # 数据集层面并行：每个数据集跑 n 轮后取平均，提交顺序即 base_datasets 顺序
        def _process_one_dataset_multi_round(idx: int, dataset_name: str):
            round_rows = []
            for round_idx in range(args.rounds):
                seed = (args.seed + round_idx) if args.seed is not None else round_idx
                _, row = _process_one_dataset(
                    idx,
                    dataset_name,
                    project_root,
                    dataset_dir,
                    output_dir,
                    ratio_dir,
                    algos,
                    seed,
                    print_lock,
                )
                if row is not None:
                    round_rows.append(row)
            if not round_rows:
                return (idx, None)
            return (idx, _average_rows(round_rows))

        results_by_index = {}
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {
                executor.submit(_process_one_dataset_multi_round, idx, dataset_name): idx
                for idx, dataset_name in enumerate(base_datasets)
            }
            for future in as_completed(futures):
                idx = futures[future]
                try:
                    i, row = future.result()
                    results_by_index[i] = row
                except Exception as e:
                    results_by_index[idx] = None
                    with print_lock:
                        print(f"  数据集 {base_datasets[idx]} 处理异常: {e}")
        for idx in range(len(base_datasets)):
            row = results_by_index.get(idx)
            if row is not None:
                csv_rows.append(row)

    out_name = "num_prefixes_" + args.algo + ".csv" if args.algo else "num_prefixes.csv"
    out_path = os.path.join(result_dir, out_name)
    with open(out_path, "w", encoding="utf-8") as f:
        for row in csv_rows:
            f.write(",".join(row) + "\n")
    print(f"结果已写入: {out_path}")


if __name__ == "__main__":
    main()
