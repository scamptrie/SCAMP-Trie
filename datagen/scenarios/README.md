# Scenarios Generator

C++ program that generates synthetic IPv6 FIB datasets for ISP Edge, Cloud Gateway, and Host network scenarios.

## Build

```bash
make data        # Release build (optimized, -O3)
make debug       # Debug build (with -g3 -DDEBUG)
make clean       # Remove build artifacts
```

The binary is produced at `build/scenarios.app`.

## Usage

```
scenarios.app -[M:O:N:o:P:h]

Options:
  -M <str>   Mode by name: isp-edge | cloud-gateway | host
  -O <num>   Mode by number (see below)
  -N <num>   Total prefix count / scale (default: 256000)
  -o <path>  Output file path without .fib extension
  -P <num>   Host randomization parameter (e.g., 64, 72, 80; default: 72)
  -h         Print help and exit
```

## Modes

| Number | Name | Description | Status |
|--------|------|-------------|--------|
| 1 | backbone | IXP backbone (real BGP data) | *Not implemented* |
| 2 | isp-edge | ISP edge router FIB | ✅ |
| 3 | campus | Campus network FIB | *Not implemented* |
| 4 | cloud-gateway | Cloud gateway FIB | ✅ |
| 5 | host | Host machine FIB | ✅ |
| 6 | others | Other scenarios | *Not implemented* |

> **Note:** Mode 1 (backbone/IXP) relies on real BGP snapshots from RIPE RIS, not synthetic generation. See [../IXP_DATA.md](../IXP_DATA.md) for instructions.

## Examples

### ISP Edge

Generate FIBs for 7 ISPs with 256K prefixes each:
```bash
./build/scenarios.app -O 2 -N 256000
```
Output files are written to `../data/test-data/{kab,orag,dtag,sky,free,netclg,cmt}.fib`.

### Cloud Gateway

Generate a single cloud gateway FIB with a specified prefix count:
```bash
./build/scenarios.app -O 4 -N 256000 -o ../data/AWS_256k_v6
```
Generates `../data/AWS_256k_v6.fib`.

Without `-o`, generates 4 fixed-size FIBs to the test-data directory:
```bash
./build/scenarios.app -O 4
```

### Host

Generate a single host FIB with param=72:
```bash
./build/scenarios.app -O 5 -N 262144 -o ../data/host2_1 -P 72
```

Without `-o`, generates 3 base host FIBs (param=64, 72, 80) at 256K each:
```bash
./build/scenarios.app -O 5
```

## Source Files

```
scenarios/
├── main.cpp          # Entry point: parses CLI args, dispatches to generator
├── makefile          # Build rules (g++ -std=c++14, pthread)
└── src/
    ├── scenarios.hpp     # Mode enum, gnrt_fib() declaration
    ├── scenarios.cpp     # Mode dispatch logic
    ├── cmd_argc.hpp      # Command-line argument parser (getopt)
    ├── common.hpp        # Logging macros, file I/O helpers
    ├── fib.hpp           # IPv6 prefix data structures, random generation
    ├── isp_edge.hpp      # ISP definitions (7 ISPs), prefix length distributions
    ├── isp_edge.cpp      # ISP edge FIB generation logic
    ├── cloud_gateway.hpp # Cloud gateway constants (/56 VPC, /64 subnet)
    ├── cloud_gateway.cpp # Azure-style cloud FIB generation (80/20 distribution)
    ├── host.hpp          # Host scenario constants
    └── host.cpp          # Random /128 host FIB generation
```

## Dependencies

- C++14 compiler (g++ 7+)
- pthread
- Standard library only (no external libraries)
