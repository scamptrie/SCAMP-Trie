#!/usr/bin/env bash
# SCAMP-Trie evaluation framework wrapper
# Usage: run.sh <dataset_name>
# Reads data/<dataset_name>.fib, writes output/scamptrie/<dataset_name>
# Output format: 5-line pipeline evaluation format

set -euo pipefail

DATASET_NAME="${1:?Usage: $0 <dataset_name>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

INPUT="$REPO_ROOT/data/${DATASET_NAME}.fib"
OUTPUT_DIR="$REPO_ROOT/output/scamptrie"
WEIGHTS="$REPO_ROOT/weights/weights_default.txt"
BINARY="$REPO_ROOT/src/scamptrie"

if [ ! -f "$INPUT" ]; then
    echo "Error: FIB file not found: $INPUT" >&2
    exit 1
fi

if [ ! -f "$WEIGHTS" ]; then
    echo "Error: weights file not found: $WEIGHTS" >&2
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "Error: SCAMP-Trie binary not found: $BINARY" >&2
    echo "Run 'make scamptrie' first." >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

"$BINARY" --eval \
    -F "$INPUT" \
    -W "$WEIGHTS" \
    --scamp-eval-output \
    --scamp-eval-output-dir "$OUTPUT_DIR"
