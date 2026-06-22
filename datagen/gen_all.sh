#!/usr/bin/env bash
#
# gen_all.sh — Master script to generate ALL synthetic IPv6 FIB datasets
# for ISP Edge, Cloud Gateway, and Host scenarios in one go.
#
# Usage:
#   ./gen_all.sh        Generate all base + scaling datasets (default)
#   ./gen_all.sh -s     Skip scaling datasets (base only, faster)
#   ./gen_all.sh -h     Print help
#

set -e

# --------------------------------------------------------------------
# Defaults
# --------------------------------------------------------------------
SKIP_SCALING=false

usage() {
    cat <<EOF
Usage: $0 [-s] [-h]

Generate all synthetic IPv6 FIB datasets for SCAMP-Trie evaluation.

Options:
  -s    Skip scaling datasets (cloud2_*, isp2_*, host2_*); base only
  -h    Show this help message

Output:
  Base datasets   → ../data/isp{1..7}_*.fib, ../data/{AWS,Azure,Google}_256k_v6.fib, ../data/host-rd{64,72,80}.fib
  Scaling datasets→ ../data/cloud2_{1..6}.fib, ../data/isp2_{1..6}.fib, ../data/host2_{1..6}.fib
EOF
    exit 0
}

while getopts "sh" opt; do
    case $opt in
        s) SKIP_SCALING=true ;;
        h) usage ;;
        *) usage ;;
    esac
done

# --------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------
DATAGEN_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="${DATAGEN_DIR}/data"
TEST_DATA_DIR="${DATA_DIR}/test-data"
SCENARIOS_DIR="${DATAGEN_DIR}/scenarios"
APP="${SCENARIOS_DIR}/build/scenarios.app"

# Ensure output directories exist
mkdir -p "$TEST_DATA_DIR"

# --------------------------------------------------------------------
# Build the scenarios binary
# --------------------------------------------------------------------
echo "============================================"
echo " Building scenarios generator"
echo "============================================"
(cd "$SCENARIOS_DIR" && make data)
if [[ ! -x "$APP" ]]; then
    echo "Error: binary not found: $APP" 1>&2
    exit 1
fi
echo ""

# --------------------------------------------------------------------
# 1. ISP Edge — Base datasets (7 ISPs, 256K prefixes each)
# --------------------------------------------------------------------
echo "============================================"
echo " [1/6] Generating base ISP Edge datasets"
echo "============================================"
(cd "$SCENARIOS_DIR" && "$APP" -O 2 -N 256000)

# Rename abbr.fib → isp{N}_{abbr}.fib and move to data/
declare -A ISP_MAP=(
    ["kab"]="isp1_kab"
    ["orag"]="isp2_orag"
    ["dtag"]="isp3_dtag"
    ["sky"]="isp4_sky"
    ["free"]="isp5_free"
    ["netclg"]="isp6_netclg"
    ["cmt"]="isp7_cmt"
)

for abbr in "${!ISP_MAP[@]}"; do
    src="${TEST_DATA_DIR}/${abbr}.fib"
    dst="${DATA_DIR}/${ISP_MAP[$abbr]}.fib"
    if [[ -f "$src" ]]; then
        mv "$src" "$dst"
        echo "  ${ISP_MAP[$abbr]}.fib"
    else
        echo "  WARNING: $src not found, skipping" 1>&2
    fi
done
echo ""

# --------------------------------------------------------------------
# 2. Cloud Gateway — Base datasets (AWS/Azure/Google, 256K prefixes each)
# --------------------------------------------------------------------
echo "============================================"
echo " [2/6] Generating base Cloud Gateway datasets"
echo "============================================"
for provider in AWS Azure Google; do
    (cd "$SCENARIOS_DIR" && "$APP" -O 4 -N 256000 -o "../data/${provider}_256k_v6")
    echo "  ${provider}_256k_v6.fib"
done
echo ""

# --------------------------------------------------------------------
# 3. Host — Base datasets (rd64/rd72/rd80, 256K prefixes each)
# --------------------------------------------------------------------
echo "============================================"
echo " [3/6] Generating base Host datasets"
echo "============================================"
(cd "$SCENARIOS_DIR" && "$APP" -O 5)
for param in 64 72 80; do
    src="${TEST_DATA_DIR}/host-rd${param}.fib"
    dst="${DATA_DIR}/host-rd${param}.fib"
    if [[ -f "$src" ]]; then
        mv "$src" "$dst"
        echo "  host-rd${param}.fib"
    else
        echo "  WARNING: $src not found, skipping" 1>&2
    fi
done
echo ""

# --------------------------------------------------------------------
# 4-6. Scaling datasets (optional)
# --------------------------------------------------------------------
if $SKIP_SCALING; then
    echo "============================================"
    echo " Skipping scaling datasets (-s flag set)"
    echo "============================================"
else
    SCALES=(256000 409600 563200 716800 870400 1024000)

    # --- 4. isp2 scaling ---
    echo "============================================"
    echo " [4/6] Generating ISP Edge scaling datasets (isp2_1..isp2_6)"
    echo "============================================"
    ISP2_SOURCE="orag"
    for i in 1 2 3 4 5 6; do
        n="${SCALES[$((i-1))]}"
        (cd "$SCENARIOS_DIR" && "$APP" -O 2 -N "$n")
        src="${TEST_DATA_DIR}/${ISP2_SOURCE}.fib"
        dst="${DATA_DIR}/isp2_${i}.fib"
        if [[ -f "$src" ]]; then
            mv "$src" "$dst"
            echo "  isp2_${i}.fib (N=$n)"
        else
            echo "  WARNING: $src not found, skipping" 1>&2
        fi
    done
    echo ""

    # --- 5. cloud2 scaling ---
    echo "============================================"
    echo " [5/6] Generating Cloud Gateway scaling datasets (cloud2_1..cloud2_6)"
    echo "============================================"
    for i in 1 2 3 4 5 6; do
        n="${SCALES[$((i-1))]}"
        (cd "$SCENARIOS_DIR" && "$APP" -O 4 -N "$n" -o "../data/cloud2_${i}")
        echo "  cloud2_${i}.fib (N=$n)"
    done
    echo ""

    # --- 6. host2 scaling ---
    echo "============================================"
    echo " [6/6] Generating Host scaling datasets (host2_1..host2_6)"
    echo "============================================"
    HOST_PARAM=72
    for i in 1 2 3 4 5 6; do
        n="${SCALES[$((i-1))]}"
        (cd "$SCENARIOS_DIR" && "$APP" -O 5 -N "$n" -o "../data/host2_${i}" -P "$HOST_PARAM")
        echo "  host2_${i}.fib (N=$n)"
    done
    echo ""
fi

# --------------------------------------------------------------------
# Cleanup
# --------------------------------------------------------------------
echo "============================================"
echo " Cleaning up temporary files"
echo "============================================"
rm -rf "$TEST_DATA_DIR"/*
echo "  removed intermediate test-data files"
echo ""

# --------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------
echo "============================================"
echo " Data generation complete!"
echo "============================================"
echo "Output directory: ${DATA_DIR}"
echo ""
echo "Base datasets:"
echo "  ISP Edge:  isp{1..7}_*.fib   (7 files)"
echo "  Cloud:     {AWS,Azure,Google}_256k_v6.fib (3 files)"
echo "  Host:      host-rd{64,72,80}.fib (3 files)"
if ! $SKIP_SCALING; then
    echo "Scaling datasets:"
    echo "  ISP Edge:  isp2_{1..6}.fib   (6 files)"
    echo "  Cloud:     cloud2_{1..6}.fib (6 files)"
    echo "  Host:      host2_{1..6}.fib  (6 files)"
fi
echo ""
echo "Done."
