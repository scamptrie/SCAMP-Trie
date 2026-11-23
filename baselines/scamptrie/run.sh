#!/usr/bin/env bash
# SCAMP-Trie evaluation framework wrapper
# Usage: run.sh <dataset_name> [scamptrie|scamptrie-opt|scamptrie-basic]
set -euo pipefail

DATASET_NAME="${1:?Usage: $0 <dataset_name>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

VARIANT="${2:-scamptrie}"
cd "$REPO_ROOT"
case "$VARIANT" in
    scamptrie|scamptrie-opt) mapping=(-W ratio/scamptrie) ;;
    scamptrie-basic) mapping=(--basic) ;;
    *) echo "Unknown SCAMP-Trie variant: $VARIANT" >&2; exit 2 ;;
esac
mkdir -p "output/$VARIANT"
exec ./src/scamptrie --eval -F "data/${DATASET_NAME}.fib" "${mapping[@]}" \
    --scamp-eval-output --scamp-eval-output-dir "output/$VARIANT"
