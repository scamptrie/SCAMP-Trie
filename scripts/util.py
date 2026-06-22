import os
import sys
import csv as csv_module

# ========== --detail 多算法配置（参数化，便于调试与双方理解一致）==========
# 四个板块（数据集）顺序
DETAIL_DATASETS = ["rrc00", "isp1", "AWS", "host_rd64"]

# 10 个算法统一配置：每项 (逻辑名, output子目录, 是否用ratio 0/1, ratio文件名)
#   - 逻辑名：CSV 中显示及查找用
#   - output子目录：实际读取的 output/<子目录>，可与他人共用
#   - 是否用ratio：1=用 ratio 文件算总容量，0=用 C*n（最大单级×流水级数）
#   - ratio文件名：仅当 用ratio=1 时有效，为 ratio/ 下文件名；用ratio=0 时填 "" 即可
DETAIL_ALGO_CONFIG = [
    ("olp",     "olp",   0, ""),
    ("biolp",   "biolp", 0, ""),
    ("minmax",  "minmax", 0, ""),
    ("2-phase", "2-phase", 0, ""),
    ("treebitmap", "treebitmap", 0, ""),
    ("treebitmap-opt", "treebitmap", 1, "treebitmap"),
    ("poptrie", "pop1", 0, ""),
    ("poptrie-opt", "pop1", 1, "pop1"),
    ("scamp-trie-basic", "scamptrie", 1, "tamptrie-s"),
    ("scamp-trie", "scamptrie-opt", 1, "tamptrie"),
]

# 由上面配置派生的顺序与查找表（勿改，仅便于代码使用）
DETAIL_ALGORITHM_ORDER = [row[0] for row in DETAIL_ALGO_CONFIG]
DETAIL_ALGO_LOOKUP = {row[0]: row for row in DETAIL_ALGO_CONFIG}


def _normalize_name(n):
    nn = n.lower().replace("-", "_")
    if nn.endswith("_v6"):
        nn = nn[:-3]
    if nn.endswith(".fib"):
        nn = nn[:-4]
    return nn


def _detail_dataset_key(normalized_name):
    nn = normalized_name
    if nn == "rrc00":
        return "rrc00"
    if nn.startswith("isp1_kab") or nn == "isp1":
        return "isp1"
    if nn == "aws_256k" or nn == "aws":
        return "AWS"
    if nn == "host_rd64_256k" or nn == "host_rd64":
        return "host_rd64"
    return None


def collect_detail_for_algo(algo: str):
    """
    仅收集单算法的 4 个数据集的每级分配/使用量。
    返回 (P, detail_data) 或 (None, {})。detail_data: dkey -> (alloc_list, used_list)。
    使用顶部 DETAIL_ALGO_CONFIG 查表：output 子目录、是否用 ratio、ratio 文件名。
    """
    cfg = DETAIL_ALGO_LOOKUP.get(algo)
    if not cfg:
        return None, {}
    _name, output_subdir, use_ratio_flag, ratio_name = cfg
    input_dir = os.path.join("output", output_subdir)
    if not os.path.isdir(input_dir):
        return None, {}
    filenames = [f for f in os.listdir(input_dir) if os.path.isfile(os.path.join(input_dir, f))]
    if not filenames:
        return None, {}
    desired_order = [
        "rrc00", "rrc01", "rrc03", "rrc04", "rrc05", "rrc06", "rrc07", "rrc10",
        "rrc11", "rrc12", "rrc13", "rrc14", "rrc15", "rrc16", "rrc18", "rrc19",
        "rrc20", "rrc21", "rrc22", "rrc23", "rrc24", "rrc25", "rrc26",
        "isp1_kab_256k", "isp2_orag_256k", "isp3_dtag_256k", "isp4_sky_256k",
        "isp5_free_256k", "isp6_netclg_256k", "isp7_cmt_256k",
        "AWS_256k", "Azure_256k", "Google_256k",
        "host_rd64_256k", "host_rd72_256k", "host_rd80_256k",
        "host-inc1", "host-inc3", "host-inc300",
    ]
    normalized_files = {fn: _normalize_name(fn) for fn in filenames}
    ordered = []
    used = set()
    for desired in desired_order:
        dnorm = desired.lower().replace("-", "_")
        dnorm_short = dnorm[:-5] if dnorm.endswith("_256k") else dnorm
        for fn, nn in normalized_files.items():
            if fn in used:
                continue
            if nn == dnorm or nn == dnorm_short or nn.startswith(dnorm_short) or dnorm_short.startswith(nn) or dnorm_short in nn:
                ordered.append(fn)
                used.add(fn)
                break
    for fn in filenames:
        if fn not in used:
            ordered.append(fn)
    filenames = ordered

    ratio_r_i = None
    ratio_n = None
    use_ratio = use_ratio_flag == 1 and ratio_name
    ratio_file_path = os.path.join("ratio", ratio_name) if ratio_name else ""
    if use_ratio and ratio_file_path and os.path.exists(ratio_file_path):
        with open(ratio_file_path, "r", encoding="utf-8") as rf:
            ratio_lines = [line.strip() for line in rf.readlines() if line.strip()]
        if len(ratio_lines) >= 2:
            ratio_n = int(ratio_lines[0])
            R_i = list(map(float, ratio_lines[1].split()))
            if len(R_i) == ratio_n:
                sum_R = sum(R_i)
                if sum_R > 0:
                    ratio_r_i = [R / sum_R for R in R_i]

    detail_data = {}
    detail_p = None

    for filename in filenames:
        file_path = os.path.join(input_dir, filename)
        with open(file_path, "r", encoding="utf-8") as f:
            lines = [line.strip() for line in f.readlines() if line.strip()]
        if len(lines) < 2:
            continue
        n = int(lines[0])
        values = list(map(int, lines[1].split()))
        if len(values) != n:
            continue
        C = max(values)
        if ratio_r_i is not None and ratio_n == n:
            pipeline_total_capacity = max(values[i] / ratio_r_i[i] for i in range(n))
        else:
            pipeline_total_capacity = C * n
        pipeline_total_capacity = ((int(pipeline_total_capacity) + 1023) // 1024) * 1024

        dkey = _detail_dataset_key(normalized_files.get(filename, ""))
        if dkey is None:
            continue
        if ratio_r_i is not None and ratio_n == n:
            if detail_p is None:
                detail_p = ratio_n
            r_i = ratio_r_i
        else:
            if detail_p is None:
                detail_p = n
            r_i = [1.0 / n] * n
        if detail_p is not None and n == detail_p:
            allocated_row = [round(pipeline_total_capacity * r_i[i]) for i in range(n)]
            detail_data[dkey] = (allocated_row, list(values))

    return detail_p, detail_data


def write_detail_csv_all_algorithms(output_path: str):
    """
    按 4 板块 × 10 算法 × 2 行 写入 detail.csv；首列为算法名，列数为各算法流水级数，不足留空。
    固定按 DETAIL_ALGORITHM_ORDER 的 10 个算法输出，无数据的算法该两行留空。
    """
    algo_list = []
    for algo in DETAIL_ALGORITHM_ORDER:
        p, data = collect_detail_for_algo(algo)
        algo_list.append((algo, p, data))
    P_max = max((p for _, p, _ in algo_list if p is not None), default=0)
    if P_max == 0:
        print("No algorithm output found for --detail")
        return
    with open(output_path, "w", encoding="utf-8", newline="") as out:
        writer = csv_module.writer(out)
        header = ["Algorithm"] + [f"Stage{i+1}" for i in range(P_max)]
        writer.writerow(header)
        for dkey in DETAIL_DATASETS:
            for algo, P, data in algo_list:
                if P is not None and dkey in data:
                    alloc_row, used_row = data[dkey]
                    writer.writerow([algo] + alloc_row + [""] * (P_max - P))
                    writer.writerow([algo] + used_row + [""] * (P_max - P))
                else:
                    writer.writerow([algo] + [""] * P_max)
                    writer.writerow([algo] + [""] * P_max)
    print(f"Detail exported to: {output_path}")


def calculate_utilization(input_dir: str, output_file: str, algo: str = None, detail: bool = False):
    results = []
    detail_data = {}
    detail_p = None
    # Find the maximum filename length for column width formatting
    filenames = []
    for filename in os.listdir(input_dir):
        file_path = os.path.join(input_dir, filename)
        if os.path.isfile(file_path):
            filenames.append(filename)
    
    if not filenames:
        return

    # Desired explicit ordering for datasets (user-specified)
    desired_order = [
        "rrc00", "rrc01", "rrc03", "rrc04", "rrc05", "rrc06", "rrc07", "rrc10",
        "rrc11", "rrc12", "rrc13", "rrc14", "rrc15", "rrc16", "rrc18", "rrc19",
        "rrc20", "rrc21", "rrc22", "rrc23", "rrc24", "rrc25", "rrc26",
        "isp1_kab_256k", "isp2_orag_256k", "isp3_dtag_256k", "isp4_sky_256k",
        "isp5_free_256k", "isp6_netclg_256k", "isp7_cmt_256k",
        "AWS_256k", "Azure_256k", "Google_256k",
        "host_rd64_256k", "host_rd72_256k", "host_rd80_256k",
        "host-inc1", "host-inc3", "host-inc300",
    ]

    normalized_files = {fn: _normalize_name(fn) for fn in filenames}

    ordered = []
    used = set()
    for desired in desired_order:
        dnorm = desired.lower().replace('-', '_')
        # strip common suffix _256k for matching
        dnorm_short = dnorm[:-5] if dnorm.endswith('_256k') else dnorm
        for fn, nn in normalized_files.items():
            if fn in used:
                continue
            if nn == dnorm or nn == dnorm_short or nn.startswith(dnorm_short) or dnorm_short.startswith(nn) or dnorm_short in nn:
                ordered.append(fn)
                used.add(fn)
                break

    # append remaining files preserving original order
    for fn in filenames:
        if fn not in used:
            ordered.append(fn)

    filenames = ordered

    max_filename_len = max(len(f) for f in filenames)
    filename_width = max(max_filename_len, 15)  # At least 15 characters wide
    
    # ========== 在算法层面读取比例文件 ==========
    ratio_r_i = None  # 归一化后的比例列表
    ratio_n = None    # 比例文件中的流水级数
    
    if algo:
        ratio_file_path = os.path.join("ratio", algo)
        if os.path.exists(ratio_file_path):
            # 读取比例文件
            with open(ratio_file_path, "r", encoding="utf-8") as ratio_file:
                ratio_lines = [line.strip() for line in ratio_file.readlines() if line.strip()]
            
            if len(ratio_lines) >= 2:
                ratio_n = int(ratio_lines[0])  # 第一行：流水级数
                R_i = list(map(float, ratio_lines[1].split()))  # 第二行：每个流水级的比例 R_i
                
                if len(R_i) == ratio_n:
                    # 计算归一化比例 r_i = R_i / sum(R_i)
                    sum_R = sum(R_i)
                    if sum_R > 0:
                        ratio_r_i = [R / sum_R for R in R_i]
    
    # Dataset directory path
    dataset_dir = "data"
    
    for filename in filenames:
        file_path = os.path.join(input_dir, filename)

        with open(file_path, "r", encoding="utf-8") as f:
            lines = [line.strip() for line in f.readlines() if line.strip()]  # Remove empty lines

        if len(lines) < 2:
            continue

        n = int(lines[0])
        values = list(map(int, lines[1].split()))

        if len(values) != n:
            continue

        # Read additional storage information (lines 3-5)
        # Line 3: Fixed storage size (存储器大小)
        # Line 4: Fixed storage used (实际使用空间)
        # Line 5: Elastic storage used (弹性存储的实际使用空间)
        # 如果文件不满5行，说明是老算法没有额外开销，所有存储值都设为0
        fixed_storage_size = 0
        fixed_storage_used = 0
        elastic_storage_used = 0
        
        if len(lines) >= 5:
            # 文件满5行，正常读取存储信息
            if lines[2]:
                try:
                    fixed_storage_size = int(lines[2])
                except ValueError:
                    fixed_storage_size = 0
            if lines[3]:
                try:
                    fixed_storage_used = int(lines[3])
                except ValueError:
                    fixed_storage_used = 0
            if lines[4]:
                try:
                    elastic_storage_used = int(lines[4])
                except ValueError:
                    elastic_storage_used = 0
        # 如果文件不满5行（只有2或3行），所有存储值保持为0（已在上面初始化）

        # ========== 计算流水线存储 ==========
        C = max(values)
        pipeline_used_memory = sum(values)  # 流水级使用量：第2行所有数相加
        
        # 使用已读取的比例信息计算 pipeline_total_capacity
        if ratio_r_i is not None and ratio_n == n:
            # 使用比例文件中的归一化比例计算
            # pipeline_total_capacity = max(values[i] / r_i)
            pipeline_total_capacity = max(values[i] / ratio_r_i[i] for i in range(n))
        else:
            # 默认值：max(values) * n
            pipeline_total_capacity = C * n
        
        # 向上取整到1024的倍数
        pipeline_total_capacity = ((int(pipeline_total_capacity) + 1023) // 1024) * 1024
        
        # ========== 为 --detail 收集 4 个数据集的每级分配/使用量 ==========
        if detail and algo and n > 0:
            dkey = _detail_dataset_key(normalized_files.get(filename, ""))
            if dkey is not None:
                if ratio_r_i is not None and ratio_n == n:
                    if detail_p is None:
                        detail_p = ratio_n
                    r_i = ratio_r_i
                else:
                    if detail_p is None:
                        detail_p = n
                    r_i = [1.0 / n] * n
                if detail_p is not None and n == detail_p:
                    allocated_row = [round(pipeline_total_capacity * r_i[i]) for i in range(n)]
                    detail_data[dkey] = (allocated_row, list(values))
        
        # ========== 计算弹性存储对齐 ==========
        # 弹性存储的空间对齐1024向上取整（第5行数字+1023再整除1024再乘回来）
        elastic_storage_aligned = ((elastic_storage_used + 1023) // 1024) * 1024 if elastic_storage_used > 0 else 0
        
        # ========== 计算总使用量和总分配量 ==========
        total_used = pipeline_used_memory + fixed_storage_used + elastic_storage_used
        total_capacity = pipeline_total_capacity + fixed_storage_size + elastic_storage_aligned
        
        # ========== 计算统计指标 ==========
        # 总存储效率：总使用量 / 总分配量
        total_efficiency = total_used / total_capacity if total_capacity > 0 else 0.0
        
        # 平均每条前缀使用空间：总使用量 / 前缀条数
        fib_file_path = os.path.join(dataset_dir, f"{filename}.fib")
        prefix_count = 0
        if os.path.exists(fib_file_path):
            with open(fib_file_path, "r", encoding="utf-8") as fib_file:
                prefix_count = sum(1 for _ in fib_file)
        avg_per_prefix = total_used / prefix_count if prefix_count > 0 else 0.0
        
        # ========== 格式化输出 ==========
        avg_per_prefix_str = f"{avg_per_prefix:.2f}"
        total_efficiency_str = f"{total_efficiency:.4f}"
        
        # 格式化6个存储数值（右对齐，宽度15）
        pipeline_used_str = f"{pipeline_used_memory:>15}"
        pipeline_capacity_str = f"{pipeline_total_capacity:>15}"
        fixed_used_str = f"{fixed_storage_used:>15}"
        fixed_capacity_str = f"{fixed_storage_size:>15}"
        elastic_used_str = f"{elastic_storage_used:>15}"
        elastic_capacity_str = f"{elastic_storage_aligned:>15}"
        total_capacity_str = f"{total_capacity:>15}"
        
        # 格式化行：Filename, AvgPerPrefix, TotalEfficiency, 6个存储数值, 总分配量
        formatted_line = (
            f"{filename:<{filename_width}}, "
            f"{avg_per_prefix_str:>15}, {total_efficiency_str:>15}, "
            f"{pipeline_used_str}, {pipeline_capacity_str}, "
            f"{fixed_used_str}, {fixed_capacity_str}, "
            f"{elastic_used_str}, {elastic_capacity_str}, "
            f"{total_capacity_str}"
        )
        
        results.append((
            filename, avg_per_prefix_str, total_efficiency_str,
            pipeline_used_memory, pipeline_total_capacity,
            fixed_storage_used, fixed_storage_size,
            elastic_storage_used, elastic_storage_aligned,
            formatted_line
        ))

    # ========== 写入CSV文件 ==========
    with open(output_file, "w", encoding="utf-8") as out:
        # 写入表头：Filename, AvgPerPrefix, TotalEfficiency, 6个存储数值, 总分配量
        header = (
            f"{'Filename':<{filename_width}}, "
            f"{'AvgPerPrefix':>15}, {'TotalEfficiency':>15}, "
            f"{'PipelineUsed':>15}, {'PipelineCapacity':>15}, "
            f"{'FixedUsed':>15}, {'FixedCapacity':>15}, "
            f"{'ElasticUsed':>15}, {'ElasticCapacity':>15}, "
            f"{'TotalCapacity':>15}"
        )
        out.write(header + "\n")
        
        # 写入数据行
        for result in results:
            out.write(result[-1] + "\n")  # result[-1] 是 formatted_line

    # ========== --detail 时写入单算法 detail_xxx.csv（保留兼容：指定 algo 且 --detail 时）==========
    if detail and algo and detail_p is not None:
        detail_path = os.path.join(os.path.dirname(output_file), f"detail_{algo}.csv")
        with open(detail_path, "w", encoding="utf-8", newline="") as detail_out:
            writer = csv_module.writer(detail_out)
            writer.writerow([f"Stage{i+1}" for i in range(detail_p)])
            for dkey in DETAIL_DATASETS:
                if dkey in detail_data:
                    alloc_row, used_row = detail_data[dkey]
                    writer.writerow(alloc_row)
                    writer.writerow(used_row)
                else:
                    writer.writerow([""] * detail_p)
                    writer.writerow([""] * detail_p)
        print(f"Detail exported to: {detail_path}")


if __name__ == "__main__":
    detail_only = "--detail" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--detail"]
    if detail_only:
        # --detail：不指定算法，按固定顺序汇总所有算法，生成 result/detail.csv
        result_dir = "result"
        if not os.path.exists(result_dir):
            os.makedirs(result_dir)
        write_detail_csv_all_algorithms(os.path.join(result_dir, "detail.csv"))
    else:
        if len(args) < 1:
            print("Usage: python util.py <algorithm_name>")
            print("       python util.py --detail   # 生成多算法汇总 result/detail.csv")
            sys.exit(1)
        algo = args[0]
        input_path = os.path.join("output", algo)
        result_dir = "result"
        if not os.path.exists(result_dir):
            os.makedirs(result_dir)
        output_path = os.path.join(result_dir, f"{algo}.csv")
        if not os.path.exists(input_path):
            print(f"Error: Input directory '{input_path}' does not exist")
            sys.exit(1)
        calculate_utilization(input_path, output_path, algo, detail=False)
        print(f"Results exported to: {output_path}")
