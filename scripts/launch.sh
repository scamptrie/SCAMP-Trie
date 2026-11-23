#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."

paper_algorithms=(olp biolp minmax 2-phase treebitmap pop1 scamptrie-basic scamptrie-opt)
algorithms=("${paper_algorithms[@]}" random numpy cpe)
jobs=1
algorithm=
dataset=
paper_set=

usage() {
    echo "Usage: bash scripts/launch.sh [-a ALGORITHM] [-d FIB | --paper | --paper-full] [-j JOBS]"
    echo "  --paper       Run the eight producers for the ten paper series on four representative FIBs"
    echo "  --paper-full  Run those producers on the 36 paper FIBs"
    echo "  -a            Select a producer: ${algorithms[*]}, scamptrie (optimized alias), or poptrie (original)"
}

while (($#)); do
    case "$1" in
        -a|--algo|-d|--dataset|-j|--jobs)
            if (($# < 2)) || [[ -z "$2" || "$2" == -* ]]; then usage >&2; exit 2; fi
            case "$1" in
                -a|--algo) algorithm=$2 ;;
                -d|--dataset) dataset=$2 ;;
                -j|--jobs) jobs=$2 ;;
            esac
            shift 2 ;;
        --paper|--paper-full) paper_set=$1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then echo "Invalid job count: $jobs" >&2; exit 2; fi
if [[ -n "$paper_set" && -n "$dataset" ]]; then usage >&2; exit 2; fi
if [[ -n "$algorithm" ]]; then
    if ! [[ "$algorithm" =~ ^[a-z0-9-]+$ ]]; then echo "Invalid algorithm: $algorithm" >&2; exit 2; fi
    case " ${algorithms[*]} poptrie scamptrie " in
        *" $algorithm "*) ;;
        *) echo "Unknown algorithm: $algorithm" >&2; exit 2 ;;
    esac
    algorithms=("$algorithm")
elif [[ -n "$paper_set" ]]; then
    algorithms=("${paper_algorithms[@]}")
fi

if [[ "$paper_set" == --paper ]]; then
    datasets=(rrc00_v6 isp1_kab AWS_256k_v6 host-rd64)
elif [[ "$paper_set" == --paper-full ]]; then
    mapfile -t datasets < scripts/paper_fibs.txt
elif [[ -n "$dataset" ]]; then
    datasets=("$dataset")
else
    shopt -s nullglob
    datasets=()
    for file in data/*.fib; do stem=${file##*/}; datasets+=("${stem%.fib}"); done
fi
if ((${#datasets[@]} == 0)); then echo "No FIBs selected" >&2; exit 1; fi
for fib in "${datasets[@]}"; do
    if ! [[ "$fib" != */* && -f "data/$fib.fib" ]]; then
        echo "Missing or invalid FIB: $fib" >&2; exit 1
    fi
done

run_algorithm() {
    local algo=$1 fib=$2
    case "$algo" in
        scamptrie|scamptrie-opt|scamptrie-basic) bash baselines/scamptrie/run.sh "$fib" "$algo" ;;
        numpy|minmax|2-phase|cpe) python3 "baselines/$algo/$algo.py" "$fib" ;;
        *) "./baselines/$algo/$algo" "$fib" ;;
    esac
}

run_task() {
    local algo=$1 fib=$2 log="result/logs/$1/$2.log"
    mkdir -p "output/$algo" "result/logs/$algo" || return 1
    local output="output/$algo/$fib" previous=""
    # Some upstream baselines can return zero after failing to write. Only
    # accept a file from this invocation, and keep the last good result on error.
    if [[ -f "$output" ]]; then
        previous=$(mktemp "result/logs/$algo/.previous.XXXXXXXX") || return 1
        mv -- "$output" "$previous" || return 1
    fi
    if run_algorithm "$algo" "$fib" >"$log" 2>&1 && [[ -s "$output" ]]; then
        if [[ -n "$previous" ]]; then rm -f -- "$previous"; fi
        echo "[$algo/$fib] OK"
    else
        if [[ -n "$previous" ]]; then mv -- "$previous" "$output"; else rm -f -- "$output"; fi
        echo "[$algo/$fib] failed (see $log)" >&2
        tail -n 4 "$log" >&2
        return 1
    fi
}

status=0
for algo in "${algorithms[@]}"; do
    pids=()
    for fib in "${datasets[@]}"; do
        run_task "$algo" "$fib" &
        pids+=("$!")
        if ((${#pids[@]} >= jobs)); then
            wait "${pids[0]}" || status=1
            pids=("${pids[@]:1}")
        fi
    done
    for pid in "${pids[@]}"; do wait "$pid" || status=1; done
done
exit "$status"
