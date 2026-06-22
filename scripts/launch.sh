#!/bin/bash

# Algorithm invocation commands
# 格式：ALGO_COMMANDS[算法名]="调用命令（使用 \$dataset 作为数据集参数）"

declare -A ALGO_COMMANDS
ALGO_COMMANDS[random]="./baselines/random/random \"\$dataset\""
ALGO_COMMANDS[olp]="./baselines/olp/olp \"\$dataset\""
ALGO_COMMANDS[biolp]="./baselines/biolp/biolp \"\$dataset\""
ALGO_COMMANDS[treebitmap]="./baselines/treebitmap/treebitmap \"\$dataset\""
ALGO_COMMANDS[pop1]="./baselines/pop1/pop1 \"\$dataset\""
ALGO_COMMANDS[numpy]="python3 baselines/numpy/numpy.py \"\$dataset\""
ALGO_COMMANDS[minmax]="python3 baselines/minmax/minmax.py \"\$dataset\""
ALGO_COMMANDS[2-phase]="python3 baselines/2-phase/2-phase.py \"\$dataset\""
ALGO_COMMANDS[cpe]="python3 baselines/cpe/cpe.py \"\$dataset\""
ALGO_COMMANDS[scamptrie]="bash baselines/scamptrie/run.sh \"\$dataset\""
# ！！！！此处添加算法调用命令！！！！

# ================ 在上述添加算法调用命令即可 ================
# ===================== 无需修改以下内容 ====================

call_algo() {
    local algo=$1
    local dataset=$2
    
    if [ -n "${ALGO_COMMANDS[$algo]}" ]; then
        eval "${ALGO_COMMANDS[$algo]}"
    else
        echo "Error: Unknown algorithm '$algo'"
        echo "Please add it to ALGO_COMMANDS in launch.sh"
        return 1
    fi
}

ALGO=""
DATASET=""
JOBS=1

while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--algo)
            ALGO="$2"
            shift 2
            ;;
        -d|--dataset)
            DATASET="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [ "$JOBS" -lt 1 ]; then
                echo "Error: -j must be a positive integer"
                exit 1
            fi
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -a, --algo <algo>       Specify algorithm to run (e.g., random)"
            echo "  -d, --dataset <dataset>  Specify dataset to run (e.g., sample1)"
            echo "  -j, --jobs <num>        Maximum number of parallel jobs (default: 1)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                      # Run all algorithms on all datasets"
            echo "  $0 -a random            # Run random algorithm on all datasets"
            echo "  $0 -d sample1           # Run all algorithms on sample1 dataset"
            echo "  $0 -a random -d sample1 # Run random algorithm on sample1 dataset"
            echo "  $0 -j 4                 # Run with up to 4 parallel jobs"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use -h or --help for help"
            exit 1
            ;;
    esac
done

# Get all algorithms
get_all_algos() {
    local algos=()
    if [ -d "baselines" ]; then
        for dir in baselines/*/; do
            if [ -d "$dir" ]; then
                local algo_name=$(basename "$dir")
                algos+=("$algo_name")
            fi
        done
    fi
    echo "${algos[@]}"
}

# Get all datasets
get_all_datasets() {
    local datasets=()
    if [ -d "data" ]; then
        for file in data/*.fib; do
            if [ -f "$file" ]; then
                local dataset_name=$(basename "$file" .fib)
                datasets+=("$dataset_name")
            fi
        done
    fi
    echo "${datasets[@]}"
}

if [ -z "$ALGO" ]; then
    ALGOS=($(get_all_algos))
    if [ ${#ALGOS[@]} -eq 0 ]; then
        echo "Error: No algorithms found"
        exit 1
    fi
    echo "Running all algorithms: ${ALGOS[*]}"
else
    if [ ! -d "baselines/$ALGO" ]; then
        echo "Error: Algorithm '$ALGO' does not exist"
        exit 1
    fi
    ALGOS=("$ALGO")
    echo "Running algorithm: $ALGO"
fi

if [ -z "$DATASET" ]; then
    DATASETS=($(get_all_datasets))
    if [ ${#DATASETS[@]} -eq 0 ]; then
        echo "Error: No datasets found"
        exit 1
    fi
    echo "Running all datasets: ${DATASETS[*]}"
else
    if [ ! -f "data/$DATASET.fib" ]; then
        echo "Error: Dataset '$DATASET' does not exist"
        exit 1
    fi
    DATASETS=("$DATASET")
    echo "Running dataset: $DATASET"
fi

echo ""

# Function to run a single task (algo + dataset)
# This function is called in parallel, so it needs to be self-contained
run_task() {
    local algo=$1
    local dataset_name=$2
    local cmd_template=$3
    local task_id="$algo - $dataset_name"
    
    # Set dataset variable to the dataset name (not full path)
    # Algorithms expect dataset name and will construct the path themselves
    # The cmd_template contains "$dataset" which will be expanded by eval
    local dataset="$dataset_name"
    
    # Ensure output directory exists
    mkdir -p "output/$algo"

    # Execute command and capture output
    # Use eval to expand $dataset in the command template
    local output
    output=$(eval "$cmd_template" 2>&1)
    local result=$?
    
    if [ "$result" -eq 0 ]; then
        printf "[%s] ✓ Success\n" "$task_id"
        return 0
    else
        printf "[%s] ✗ Failed\n" "$task_id"
        # Print error output if command failed (limit length to avoid clutter)
        if [ -n "$output" ]; then
            local error_msg="${output:0:200}"
            if [ ${#output} -gt 200 ]; then
                error_msg="${error_msg}..."
            fi
            printf "[%s] Error: %s\n" "$task_id" "$error_msg" >&2
        fi
        return 1
    fi
}

# Run tasks with parallel execution at dataset level
algo_index=0
for algo in "${ALGOS[@]}"; do
    if [ $algo_index -gt 0 ]; then
        echo "=========================================="
        echo ""
    fi
    
    echo "Algorithm: $algo"
    
    # Get command template for this algorithm
    cmd_template="${ALGO_COMMANDS[$algo]}"
    if [ -z "$cmd_template" ]; then
        echo "Error: Unknown algorithm '$algo'"
        echo "Please add it to ALGO_COMMANDS in launch.sh"
        algo_index=$((algo_index + 1))
        continue
    fi
    
    # Execute tasks in parallel with job limit
    pids=()
    dataset_index=0
    
    while [ $dataset_index -lt ${#DATASETS[@]} ] || [ ${#pids[@]} -gt 0 ]; do
        # Start new jobs if we have capacity
        while [ ${#pids[@]} -lt $JOBS ] && [ $dataset_index -lt ${#DATASETS[@]} ]; do
            dataset="${DATASETS[$dataset_index]}"
            run_task "$algo" "$dataset" "$cmd_template" &
            pids+=($!)
            dataset_index=$((dataset_index + 1))
        done
        
        # Wait for at least one job to complete and clean up
        if [ ${#pids[@]} -gt 0 ]; then
            # Wait for the first PID (simple and reliable)
            wait ${pids[0]} 2>/dev/null || true
            # Remove all completed PIDs by checking which ones are still running
            new_pids=()
            for pid in "${pids[@]}"; do
                if kill -0 "$pid" 2>/dev/null; then
                    new_pids+=($pid)
                fi
            done
            pids=("${new_pids[@]}")
        fi
    done
    
    algo_index=$((algo_index + 1))
done

echo ""
echo "All evaluations completed!"
