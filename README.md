# SCAMP-Trie: Structure-Guided Compression, Allocation, and Mapping for Storage-Efficient IPv6 Lookup Pipelines

SCAMP-Trie is a trie-structure-guided approach that jointly performs compression, allocation, and mapping for trie-based IPv6 lookup pipelines. This repository contains the reference implementation and evaluation framework accompanying our **SIGCOMM 2026** paper:

> *"Trie-Structure-Guided Compression, Allocation, and Mapping for Storage-Efficient IPv6 Lookup Pipelines"*

## Key Features

- **InvDist-based node mapping**: Assigns nodes to pipeline stages based on their inverse distance to leaf nodes, achieving high memory utilization
- **Warning-line dynamic mapping**: Flexible partial node remapping that adapts to memory allocation ratios
- **Simulated annealing weight search**: Metaheuristic optimization for per-stage memory allocation
- **Co-designed node structures**: Five node types (C1/H1, C2/H2, C3) with NI compression and half-size storage
- **11-stage lookup pipeline**: Compact IPv6 lookup architecture achieving 768 Gbps on FPGA

## Directory Structure

```
scamptrie/
├── src/                    # SCAMP-Trie core C++ implementation
│   ├── main.cpp            # CLI: --gen (annealing) | --eval | --scamp-eval-output
│   ├── scamptrie_base.*    # Core types, constants, node structures
│   ├── scamptrie_dynamic.* # Dynamic trie insertion and lookup
│   ├── moo/                # Multi-objective optimization (simulated annealing)
│   └── utils/              # RAM pool, BMP manager, hash table, memory init
│
├── baselines/              # Comparison algorithms
│   ├── scamptrie/          # SCAMP-Trie evaluation wrapper (run.sh)
│   ├── olp/                # Optimal Leaf Push [Jiang & Prasanna, HOTI 2007]
│   ├── biolp/              # Bidirectional OLP [Le et al., FCCM 2008]
│   ├── minmax/             # MinMax DP partitioning [Basu & Narlikar, TON 2005]
│   ├── 2-phase/            # Two-Phase optimization [Kim & Sahni, TC 2007]
│   ├── treebitmap/         # Tree Bitmap [Eatherton et al., CCR 2004]
│   ├── pop1/               # Poptrie implementation [Asai & Ohara, SIGCOMM 2015]
│   ├── poptrie/            # Original Poptrie (research/evaluation license only)
│   ├── cpe/                # Configurable Partitioning Engine
│   ├── numpy/              # Simple trie partitioning (sample)
│   └── random/             # Baseline random (sample)
│
├── data/                   # IPv6 FIB datasets (36+ FIBs, 4 network scenarios)
├── scripts/                # Evaluation and analysis tools
│   ├── launch.sh           # Master benchmark runner
│   ├── util.py             # Storage utilization and stage comparison CSVs
│   ├── plot_storage.py     # Plot utilization and stage usage from those CSVs
│   ├── gen_ratio.py        # Storage allocation ratio optimizer
│   ├── calc_ratio.py       # Closed-form ratio calculator
│   ├── calc_num_prefixes.py # Prefix capacity comparison
│   └── calc_prediction_base.py # Prediction baseline analysis
│
├── weights/                # Pre-computed SCAMP-Trie weights
├── ratio/                  # Storage allocation ratios
├── docs/                   # Supplementary documentation
│   └── figures/origin/SCAMP-Trie.opju  # Original plotting data and editable graphs from the paper
├── datagen/                # Synthetic data generators
├── legacy/                 # Historical predecessor version
├── output/                 # Algorithm outputs (generated at runtime)
└── result/                 # Analysis results (generated at runtime)
```

## Prerequisites

- **C++ compiler**: g++ 7+ with C++14 support
- **OpenMP**: Required for simulated annealing (parallel evaluation)
- **Python 3**: Required for analysis scripts and Python-based baselines
- **GNU Make**: Build orchestration
- **Bash 4+**: Shell scripts for evaluation
- **Matplotlib**: Only required for the figures (e.g. `python3 -m pip install matplotlib` in a Python environment)

## Quick Start

```bash
# 1. Build everything (SCAMP-Trie + all C++ baselines)
make all

# 2. Run a quick evaluation on the sample dataset
bash scripts/launch.sh -a scamptrie -d sample1

# 3. Check the 5-line output
cat output/scamptrie/sample1

# 4. Compute storage utilization
python3 scripts/util.py scamptrie

# 5. [Optional] Quick annealing test (100 rounds, ~30 seconds)
make -C src annealing-test
```

> **Windows/MinGW users**: Use `mingw32-make` instead of `make`. Ensure a 64-bit MinGW-w64 compiler is in PATH (e.g., MSYS2 `mingw64/bin`). An example: `export PATH="/c/msys64/mingw64/bin:$PATH" && CXX=g++ mingw32-make all`.

## Build

```bash
# Build SCAMP-Trie only
make scamptrie

# Build all C++ baselines
make baselines

# Build everything
make all

# Clean build artifacts
make clean
```

### Build Variants

```bash
# Build with debug symbols
make -C src debug

# Build for profiling (gprof)
make -C src profile

# Build the executable shared by both mapping modes
make -C src scamptrie-basic
bash scripts/launch.sh -a scamptrie-basic -d rrc00_v6
```

### LPM Correctness Verification

SCAMP-Trie includes a built-in correctness check that verifies lookup results against a brute-force LPM reference:

```bash
# Verify SCAMP-Trie on a single FIB
./src/scamptrie --eval -F data/sample1.fib -W weights/weights_default.txt --verify

# Verify across all 4 representative scenarios
for fib in rrc00_v6.fib isp1_kab.fib AWS_256k_v6.fib host-rd64.fib; do
    ./src/scamptrie --eval -F "data/$fib" -W weights/weights_default.txt --verify
done
```

The `--verify` flag performs a linear scan over all FIB prefixes, computing the ground-truth LPM result for each prefix's network address and comparing it against SCAMP-Trie's lookup. A pass/fail summary is printed at the end.

## Running Evaluations

### Single Algorithm on a Single Dataset

```bash
bash scripts/launch.sh -a scamptrie -d sample1
bash scripts/launch.sh -a olp -d rrc00_v6
```

### All Algorithms on All Datasets

```bash
make eval
# or: bash scripts/launch.sh
# With parallel execution (4 jobs):
bash scripts/launch.sh -j 4
```

### Output Format

Each algorithm produces a 5-line output file at `output/<algo>/<dataset>`:

```
11                              # Number of pipeline stages
7386624 7224320 ... 12416      # Per-stage memory usage (bits)
532480                          # Fixed storage capacity (bits)
9750                            # Fixed storage used (bits)
473408                          # Elastic storage used (bits)
```

## Analyzing Results

### Storage Utilization

```bash
# Compute per-algorithm utilization
python3 scripts/util.py scamptrie
python3 scripts/util.py olp
python3 scripts/util.py treebitmap

# Results are saved to result/<algo>.csv
```

### Recomputing Allocation Ratios

The artifact includes the ratios used in the paper. To derive the Basic allocation independently, first generate its mapping on the four training FIBs, then run the closed-form calculator. Writing the result to a separate directory lets you compare it with the shipped ratio.

```bash
bash scripts/launch.sh --paper -a scamptrie-basic -j 2
python3 scripts/calc_ratio.py scamptrie-basic --ratio-dir result/derived-ratio
diff -w ratio/scamptrie-basic result/derived-ratio/scamptrie-basic
```

The same calculator accepts `treebitmap` and `pop1` once their four raw outputs exist. Its default applies the artifact's 2% minimum-stage allocation procedure. The older `gen_ratio.py` provides an alternative search and can produce different ratios.

## Generating Optimized Weights (Simulated Annealing)

SCAMP-Trie uses simulated annealing to find optimal per-stage memory allocation weights:

```bash
# Quick test (100 rounds, single dataset)
make quick-test

# Full annealing (300 rounds, 4 training FIBs)
# Clear objects compiled with the 100-round smoke-test setting first.
make -C src clean && make weights
# or: make -C src annealing-scamptrie

# 100 rounds on the training set with timing
make -C src annealing-fset1-100
```

Annealing parameters can be adjusted via `ANNEAL_MAX_ROUND`:

```bash
make -C src ANNEAL_MAX_ROUND=500 annealing-scamptrie
```

## Reproducing Paper Results

The paper evaluates SCAMP-Trie against 9 baselines on 36 IPv6 FIBs across 4 network scenarios (IXP, ISP edge, cloud gateway, host).

### Four representative FIBs

From the repository root on Linux, run:

```bash
make -j4 all &&
bash scripts/launch.sh --paper -j 2 &&
python3 scripts/util.py --detail
```

This runs eight implementations on four FIBs. The analysis produces ten series because Tree Bitmap and Poptrie each have both uniform and optimized allocations. The paper's Poptrie series uses the `pop1` implementation. The original C implementation can be exercised separately with `bash scripts/launch.sh -a poptrie -d sample1`. The launcher writes per-task logs to `result/logs/<algorithm>/<FIB>.log` and returns a nonzero status on failure.

| FIB | Optimized total utilization | Basic total utilization |
|---|---:|---:|
| `rrc00_v6` | 95.55% | 51.58% |
| `isp1_kab` | 89.45% | 71.59% |
| `AWS_256k_v6` | 86.75% | 54.13% |
| `host-rd64` | 87.31% | 51.47% |

Read `TotalEfficiency` in `result/scamp-trie.csv` and `result/scamp-trie-basic.csv` as a fraction. The eight baseline comparison CSVs are in the same directory. Run `--detail` last before plotting. It writes separate uniform and optimized Tree Bitmap series, while the legacy standalone `util.py treebitmap` command writes its ratio-aware result to `treebitmap.csv`. In `result/detail.csv` the four FIB blocks follow the order above. Within each block, each algorithm has two rows, allocated bits followed by used bits. Stage1 in the CSV corresponds to pipeline stage S0.

- **SCAMP-Trie** evaluates insertion with the published vector in `ratio/scamptrie`. The launcher writes five-line raw outputs under `output/scamptrie-opt/`. The analysis normalizes the same vector to calculate total capacity from per-stage usage and includes fixed and elastic storage. To evaluate a single FIB, use `bash scripts/launch.sh -a scamptrie-opt -d rrc00_v6` followed by `python3 scripts/util.py scamptrie-opt`. The Quick Start alias `scamptrie` uses the same weights and writes to its own output directory.
- **SCAMP-Trie-Basic (§3.2)** maps each node to the lowest legal stage, with enough logical capacity at each stage for the entire node pool. This removes capacity pressure during mapping while keeping the node representation, hash and free-child handling shared with SCAMP-Trie. The naive allocation is computed **after mapping** from peak relative stage demand across the four training FIBs. The five-line files in `output/scamptrie-basic/` supply actual usage, and `ratio/scamptrie-basic` supplies the allocation for reporting. To evaluate a single FIB, use `bash scripts/launch.sh -a scamptrie-basic -d rrc00_v6` followed by `python3 scripts/util.py scamptrie-basic`.

### Paper figures

The [SCAMP-Trie Origin project](docs/figures/origin/SCAMP-Trie.opju) contains
the original data used to plot the figures in the paper and the corresponding
editable Origin graphs. The authors prepared these figures with
**Origin 2021 SR0 for Windows (64-bit, build 9.8.0.200)**.
Download the project and open it in Origin to inspect the data or edit the graphs.

After the four-FIB sequence, run `python3 scripts/plot_storage.py`. It writes a four-FIB preview of the memory utilization comparison and the four stage-usage panels for BiOLP, 2-phase, Poptrie and SCAMP-Trie. The two SVGs are in `result/figures/`.

To plot the utilization comparison across all 36 paper FIBs, run:

```bash
bash scripts/launch.sh --paper-full -j 2 &&
python3 scripts/util.py --detail &&
python3 scripts/plot_storage.py --full
```

The full utilization plot includes all 23 IXP, 7 ISP, 3 cloud and 3 host FIBs listed in `scripts/paper_fibs.txt`. Additional scaling datasets in `data/` are excluded. Both workflows use the shipped weights. Full annealing is only needed to perform a new weight search and `make weights` overwrites `ratio/scamptrie`.

### Key Metrics

The paper evaluates three storage efficiency metrics under the TPLM model:

- **Memory utilization (η_util)**: Fraction of allocated memory actually used
- **Per-prefix cost (η_pp)**: Average bits per FIB prefix
- **Storage capacity (N_max)**: Maximum supportable FIB size under fixed memory budget

## Datasets

The `data/` directory contains 36+ IPv6 FIBs across four scenarios:

| Scenario | Count | Description |
|----------|-------|-------------|
| IXP (Backbone) | 23 | RIPE NCC RRC BGP snapshots |
| ISP Edge | 7 | Synthesized from major ISP subnet delegations |
| Cloud Gateway | 3 | Synthesized with Azure-style /56→/64 allocation |
| Host | 3 | Synthesized with random /128 host address assignment |

Additional prediction/scaling datasets are included in `data/` (prefixed with `rrc*_202*`, `isp2_*`, `cloud2_*`, `host2_*`).

### Reproducing the Datasets

- **ISP Edge, Cloud Gateway, Host** (synthetic): One-click generation via [`datagen/gen_all.sh`](datagen/gen_all.sh)
- **IXP / Backbone** (real BGP data): Step-by-step guide at [`datagen/IXP_DATA.md`](datagen/IXP_DATA.md)

See [`datagen/README.md`](datagen/README.md) for detailed documentation on all data generation procedures.

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

**Note**: The `baselines/poptrie/` directory contains the original Poptrie implementation by Hirochika Asai and Yasuhiro Ohara, which is provided under a research/evaluation-only license. It is included here for reproducibility purposes. For any use beyond research/evaluation, please refer to the original Poptrie distribution.

## Citation

If you use SCAMP-Trie in your research, please cite:

```bibtex
@inproceedings{scamptrie2026,
  title     = {Trie-Structure-Guided Compression, Allocation, and Mapping
               for Storage-Efficient IPv6 Lookup Pipelines},
  author    = {Donghong Jiang and Zhenhao Yuan and Yanbiao Li and Shi Meng and Yuxuan Chen and Taiji Chen and Xian Yu and Gaogang Xie},
  booktitle = {Proceedings of the ACM SIGCOMM 2026 Conference},
  year      = {2026},
  publisher = {ACM}
}
```

See [CITATION.cff](CITATION.cff) for citation metadata.

## Documentation

- [docs/ANNEALING_REPORT.md](docs/ANNEALING_REPORT.md) — Simulated annealing algorithm details
- [docs/INSERT_PSEUDOCODE.md](docs/INSERT_PSEUDOCODE.md) — Incremental prefix insertion pseudocode
- `legacy/algo-tamptrie/` — Historical predecessor implementation

## Contact

For questions about the code or paper, please open an issue on this repository.
