#!/usr/bin/env bash
#
# 生成 isp2_1.fib..isp2_6.fib（isp-edge，取 orag 即 isp2）和 host2_1.fib..host2_6.fib（host param=72）
# 规模在 256K～1024K 之间均匀取 6 个点，下面 SCALES 可改。
#

set -e

# ---------- 可调参数：6 个规模点（256K～1024K 均匀采样，单位：条数，K=1000）----------
# 需修改时只改下面这一行即可
SCALES=(256000 409600 563200 716800 870400 1024000)

# ISP2 对应的是 orag（isp_edge 生成的其中一个 ISP）
ISP2_SOURCE="orag"
# host 使用的 param
HOST_PARAM=72

# 数据目录：相对本脚本所在目录
DATAGEN_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="${DATAGEN_DIR}/data"
TEST_DATA_DIR="${DATA_DIR}/test-data"
SCENARIOS_DIR="${DATAGEN_DIR}/scenarios"
APP="${SCENARIOS_DIR}/build/scenarios.app"

mkdir -p "$TEST_DATA_DIR"

# 编译
echo "[build] scenarios ..."
(cd "$SCENARIOS_DIR" && make data)
if [[ ! -x "$APP" ]]; then
  echo "Error: binary not found: $APP" 1>&2
  exit 1
fi

# 生成 isp2_1 .. isp2_6（isp-edge，每次用不同 -N，然后取 orag.fib 为 isp2_x.fib）
echo "[gen] isp2_1.fib .. isp2_6.fib (isp-edge, source=${ISP2_SOURCE})"
for i in 1 2 3 4 5 6; do
  n="${SCALES[$((i-1))]}"
  (cd "$SCENARIOS_DIR" && "$APP" -O 2 -N "$n")
  cp "${TEST_DATA_DIR}/${ISP2_SOURCE}.fib" "${DATA_DIR}/isp2_${i}.fib"
  echo "  isp2_${i}.fib (N=$n)"
done

# 生成 host2_1 .. host2_6（host param=72，-o 指定输出路径）
echo "[gen] host2_1.fib .. host2_6.fib (host param=${HOST_PARAM})"
for i in 1 2 3 4 5 6; do
  n="${SCALES[$((i-1))]}"
  (cd "$SCENARIOS_DIR" && "$APP" -O 5 -N "$n" -o "../data/host2_${i}" -P "$HOST_PARAM")
  echo "  host2_${i}.fib (N=$n)"
done

echo "[done] isp2_*.fib and host2_*.fib under: ${DATA_DIR}"
