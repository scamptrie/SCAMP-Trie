#!/usr/bin/env bash
#
# 生成 cloud2_1.fib .. cloud2_6.fib（cloud-gateway，Azure 策略：/56 再 /64，80/20）
# 规模与 isp2/host2 对等：256K～1024K 均匀取 6 个点，以 Azure 为准。
#

set -e

# ---------- 可调参数：6 个规模点（与 gen_isp2_host2.sh 一致）----------
SCALES=(256000 409600 563200 716800 870400 1024000)

DATAGEN_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="${DATAGEN_DIR}/data"
SCENARIOS_DIR="${DATAGEN_DIR}/scenarios"
APP="${SCENARIOS_DIR}/build/scenarios.app"

# 编译
echo "[build] scenarios (cloud-gateway) ..."
(cd "$SCENARIOS_DIR" && make data)
if [[ ! -x "$APP" ]]; then
  echo "Error: binary not found: $APP" 1>&2
  exit 1
fi

# 生成 cloud2_1 .. cloud2_6（Azure 风格，-O 4 cloud-gateway，-N 规模，-o 输出路径）
echo "[gen] cloud2_1.fib .. cloud2_6.fib (cloud-gateway, Azure)"
for i in 1 2 3 4 5 6; do
  n="${SCALES[$((i-1))]}"
  (cd "$SCENARIOS_DIR" && "$APP" -O 4 -N "$n" -o "../data/cloud2_${i}")
  echo "  cloud2_${i}.fib (N=$n)"
done

echo "[done] cloud2_*.fib under: ${DATA_DIR}"
