#!/usr/bin/env python3

import sys
import os
import json  # 导入json模块以读取配置
from pathlib import Path

from fib_parser import read_fib_file
from layer_calculator import calculate_all_layers


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <dataset_name>", file=sys.stderr)
        sys.exit(1)

    # --- 新增：定位根目录并加载 config.json ---
    root_dir = Path(__file__).resolve().parent.parent.parent
    config_path = Path(__file__).resolve().parent / "config.json"

    if not config_path.exists():
        print(f"Error: config.json not found at {config_path}", file=sys.stderr)
        sys.exit(1)

    with open(config_path, 'r', encoding='utf-8') as f:
        config = json.load(f)

    # 从配置中提取参数，若不存在则使用默认值
    num_layers = config.get("num_layers", 4)
    per_node_memory = config.get("per_node_memory", 256)
    extra_space = config.get("extra_space", 0)
    # ----------------------------------------

    dataset_name = sys.argv[1]
    # 使用 root_dir 拼接输入路径，确保路径绝对正确
    input_file = root_dir / "data" / f"{dataset_name}.fib"
    
    subdir_name = config.get("output_subdir", "2-phase")
    output_dir = root_dir / "output" / subdir_name
    output_file = output_dir / dataset_name
    output_dir.mkdir(parents=True, exist_ok=True)

    # 读取FIB文件
    prefixes = read_fib_file(str(input_file))

    # 修改：传入从 config.json 读取的 num_layers 和 per_node_memory
    layer_sizes = calculate_all_layers(
        prefixes, 
        num_layers=num_layers, 
        per_node_memory=per_node_memory
    )

    # 写入输出文件
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            # 修改：写入动态的 num_layers
            f.write(f"{num_layers}\n")
            f.write(" ".join(str(size) for size in layer_sizes) + "\n")
            # 修改：写入来自 config.json 的 extra_space
            f.write(f"{extra_space}\n")
    except Exception as e:
        print(f"Error writing output file {output_file}: {str(e)}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()