# IXP (Backbone) Data Generation Guide

This guide explains how to download and process real BGP routing data from [RIPE RIS](https://ris.ripe.net/) to generate IPv6 FIB datasets for the IXP (backbone) network scenario. Unlike the other three scenarios (ISP Edge, Cloud Gateway, Host), IXP data **cannot be generated synthetically** — it must be derived from real BGP snapshots.

> **If you only need the IXP FIB files for evaluation**, pre-generated datasets are already available in [`../data/`](../data/) (files named `rrc*_v6.fib`). This guide is only necessary if you want to reproduce the data from scratch or collect snapshots from different time points.

---

## Overview

The SCAMP-Trie paper uses IPv6 BGP routing snapshots from **23 RIPE RIS Route Collectors (RRCs)** deployed at major Internet Exchange Points worldwide. Nine of them are used for the main experimental evaluation, chosen for geographical diversity. Additionally, 10 years of historical snapshots from **rrc00** (2016–2025, plus the current year) are used for scalability analysis.

### Processing Workflow

```
RIPE RIS ──download──> .gz raw files ──bgpdump──> .fib files ──benchmark──> .tr files ──shuf──> shuffled .tr files
```

This guide covers all steps in this pipeline.

---

## Prerequisites

### 1. Linux Environment

The process is designed for **Linux** (Ubuntu recommended). The core tool (`bgpdump`) is a C program that compiles on Linux.

### 2. Install Dependencies

```bash
sudo apt-get update
sudo apt-get install libbz2-dev
sudo apt-get install libcurl4-openssl-dev
sudo apt-get install zlib1g-dev
sudo apt-get install build-essential
```

### 3. Download and Compile bgpdump

The standard tool for parsing BGP dump files is **bgpdump** (version 1.6.0), maintained by RIPE NCC.

```bash
# Download bgpdump source
wget https://www.ris.ripe.net/source/bgpdump/bgpdump-1.6.0.tar.gz
tar -xzf bgpdump-1.6.0.tar.gz
cd bgpdump-1.6.0

# Build
./configure
make

# Verify the binary
./bgpdump -h
```

The compiled binary `bgpdump` will be used in the parsing steps below.

---

## Step 1: Prepare Working Directories

Create two directories to organize the raw downloaded data and the parsed output:

```bash
mkdir -p raw       # For downloaded .gz BGP raw data files
mkdir -p data      # For the generated .fib output files
```

---

## Step 2: Download BGP Raw Data from RIPE RIS

RIPE RIS provides BGP routing snapshots (called "bview" files) at regular intervals. Files are available at:

```
https://data.ris.ripe.net/rrcXX/
```

Where `rrcXX` is the collector identifier (e.g., `rrc00`, `rrc01`, ..., `rrc26`).

### Download a Single bview Snapshot

Each bview file is named with the timestamp of the snapshot:

```
bview.YYYYMMDD.HHmm.gz
```

For example, to download the **08:00 UTC, January 25, 2025** snapshot from rrc00:

```bash
wget https://data.ris.ripe.net/rrc00/2025.01/bview.20250125.0800.gz -P raw/
```

### Download Snapshots for All 23 RRCs

The paper uses snapshots from rrc00, rrc01, rrc03–rrc07, rrc10–rrc16, rrc18–rrc26. To download all of them for a specific date, use a script:

```bash
#!/bin/bash
# download_rrc.sh — Download bview snapshots for all RRCs at a given date/time

DATE="2025.01"                  # Year.Month
TIMESTAMP="20250125.0800"       # YYYYMMDD.HHmm (UTC)

RRC_LIST=(
    rrc00 rrc01 rrc03 rrc04 rrc05 rrc06 rrc07
    rrc10 rrc11 rrc12 rrc13 rrc14 rrc15 rrc16
    rrc18 rrc19 rrc20 rrc21 rrc22 rrc23 rrc24 rrc25 rrc26
)

for rrc in "${RRC_LIST[@]}"; do
    URL="https://data.ris.ripe.net/${rrc}/${DATE}/bview.${TIMESTAMP}.gz"
    echo "Downloading ${rrc}..."
    wget -q "$URL" -P raw/ && echo "  OK: ${rrc}" || echo "  FAILED: ${rrc}"
done
```

> **Tip:** Choose a timestamp when BGP tables are relatively stable. The paper uses **08:00 UTC, July 1st** for the main datasets. January 25th is used in some examples.

### Historical Data (for Scalability Analysis)

For year-over-year analysis, download the same time-of-day snapshot from each year:

```bash
# Example: rrc00 at 08:00 UTC, July 1st, from 2016 to 2026
for year in 2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026; do
    wget "https://data.ris.ripe.net/rrc00/${year}.07/bview.${year}0701.0800.gz" -P raw/
done
```

---

## Step 3: Parse BGP Raw Data to Generate FIB Files

Use `bgpdump` to parse each downloaded `.gz` file and extract IPv6 prefixes into `.fib` format.

### Understanding bgpdump Output

The `-m` (machine-readable) flag produces one line per prefix:

```
TIME|TYPE|FROM_PEER|TO_PEER|PREFIX|AS_PATH|ORIGIN|NEXT_HOP|...
```

For generating a FIB, we only need the **best-path IPv6 prefixes** — the prefixes that would actually be installed in a router's forwarding table.

### Parse a Single bview File

The following pipeline extracts unique IPv6 prefixes from a bview file:

```bash
bgpdump -m raw/bview.20250125.0800.gz \
  | grep "A::" \
  | grep -v "::/0" \
  | awk -F'|' '{print $5}' \
  | awk -F'/' '{printf "%s/%s\n", $1, $2}' \
  | sort -u \
  > data/rrc00_v6.fib
```

**Explanation:**
1. `bgpdump -m` — Parse the bview file in machine-readable format
2. `grep "A::"` — Filter for IPv6 announcements (RRC dump marks IPv6 with `A::`)
3. `grep -v "::/0"` — Remove the default route
4. `awk -F'|' '{print $5}'` — Extract the prefix field (5th pipe-delimited field)
5. `awk -F'/' '{printf ...}'` — Reformat to `<prefix>/<len>` format
6. `sort -u` — Sort and deduplicate

### Convert to SCAMP-Trie .fib Format

The SCAMP-Trie evaluation framework expects a specific `.fib` format: `<IPv6_HighHex>.<IPv6_LowHex>/<Len>`. Convert the output with a Python helper script:

```python
#!/usr/bin/env python3
# convert_to_fib_format.py — Convert bgpdump prefix output to SCAMP-Trie .fib format

import sys
import ipaddress

def convert_line(line):
    """Convert '2001:db8::1/64' to '20010DB800000000.0000000000000001/64'"""
    line = line.strip()
    if '/' not in line:
        return None
    prefix, length = line.split('/')
    try:
        addr = ipaddress.IPv6Address(prefix)
        full = int(addr)
        hi = full >> 64
        lo = full & ((1 << 64) - 1)
        return f"{hi:016X}.{lo:016X}/{length}"
    except ValueError:
        return None

if __name__ == "__main__":
    for line in sys.stdin:
        result = convert_line(line)
        if result:
            print(result)

# Usage: python3 convert_to_fib_format.py < data/rrc00_v6.txt > data/rrc00_v6.fib
```

### Batch Processing Script

To process all downloaded bview files at once:

```bash
#!/bin/bash
# parse_all.sh — Parse all bview files in raw/ and generate .fib files

for gzfile in raw/bview.*.gz; do
    # Extract RRC name and date from filename
    # Example: raw/bview.20250125.0800.gz → rrc_INFO would need to be tracked separately
    # Simpler: rename downloaded files to include rrc name before processing
    
    base=$(basename "$gzfile" .gz)
    output="data/${base}.fib"
    
    echo "Parsing $gzfile → $output ..."
    
    bgpdump -m "$gzfile" \
      | grep "A::" \
      | grep -v "::/0" \
      | awk -F'|' '{print $5}' \
      | sort -u \
      > "${output}.tmp"
    
    python3 convert_to_fib_format.py < "${output}.tmp" > "$output"
    rm "${output}.tmp"
    
    count=$(wc -l < "$output")
    echo "  Done: $count prefixes"
done
```

> **Note:** Since bview filenames do not include the RRC identifier, organize your downloads so each file is associated with its RRC. One approach is to download into per-RRC subdirectories (`raw/rrc00/`, `raw/rrc01/`, ...) and adjust the script accordingly.

---

## Step 4: Rename Output Files

Rename the generated FIB files to the standard naming convention used by the evaluation framework:

```bash
# Per-RRC snapshots (current)
mv data/rrc00.fib data/rrc00_v6.fib
mv data/rrc01.fib data/rrc01_v6.fib
# ... (repeat for all RRCs)

# Year-over-year snapshots
mv data/rrc00_2016.fib data/rrc00_2016_v6.fib
mv data/rrc00_2017.fib data/rrc00_2017_v6.fib
# ... (repeat for all years)
```

Standard naming format: `rrc{NN}_v6.fib` or `rrc{NN}_{YYYY}_v6.fib`

---

## Step 5 (Optional): Generate Trace (.tr) Files

After obtaining `.fib` files, generate corresponding `.tr` (trace) files for lookup evaluation. The SCAMP-Trie benchmark framework generates `.tr` files automatically when a FIB file is loaded:

```bash
# In the scamptrie/ directory:
./build/benchmark.app -F ./data/rrc00_v6.fib -a <algorithm_name>
```

Running any algorithm on a `.fib` file will produce a `.tr` file in the same directory. The `.tr` file contains lookup requests (randomly sampled from the FIB prefixes) used for consistent evaluation across algorithms.

---

## Step 6 (Optional): Shuffle Trace Files

For experiments requiring randomized trace order, shuffle the `.tr` files using `shuf`:

```bash
mkdir -p shuffled

for trfile in data/*.tr; do
    base=$(basename "$trfile")
    shuf "$trfile" > "shuffled/${base}"
    echo "Shuffled: ${base}"
done
```

> **Important:** Do not redirect output to the same file as input — this can corrupt or truncate the data. Always use a different output path.

---

## Complete Workflow Summary

| Step | Action | Command/Tool |
|------|--------|-------------|
| 1 | Install dependencies | `apt-get install libbz2-dev libcurl4-openssl-dev` |
| 2 | Build bgpdump 1.6.0 | `wget ... && ./configure && make` |
| 3 | Create directories | `mkdir raw data` |
| 4 | Download BGP raw data | `wget https://data.ris.ripe.net/rrcXX/...` |
| 5 | Parse raw data to .fib | `bgpdump -m ... \| awk ... \| sort -u` |
| 6 | Convert to SCAMP-Trie format | Python script (see Step 3) |
| 7 | Rename output files | Standard naming: `rrc{NN}_v6.fib` |
| 8 | Generate .tr files | Benchmark framework with `-F` flag |
| 9 | Shuffle .tr files | `shuf input.tr > output.tr` |

---

## Quick Reference: Expected Output

After completing all steps, you should have:

```
data/
├── rrc00_v6.fib          # RRC00 (Amsterdam) — ~110K IPv6 prefixes
├── rrc01_v6.fib          # RRC01 (London)     — ~108K IPv6 prefixes
├── rrc03_v6.fib          # RRC03 (Amsterdam)  — ~108K IPv6 prefixes
├── rrc04_v6.fib          # RRC04 (Geneva)     — ~110K IPv6 prefixes
├── rrc05_v6.fib          # RRC05 (Vienna)     — ~108K IPv6 prefixes
├── rrc06_v6.fib          # RRC06 (Otemachi)   — ~110K IPv6 prefixes
├── rrc07_v6.fib          # RRC07 (Stockholm)  — ~110K IPv6 prefixes
├── rrc10_v6.fib          # RRC10 (Milan)      — ~106K IPv6 prefixes
├── rrc11_v6.fib          # RRC11 (New York)   — ~112K IPv6 prefixes
├── rrc12_v6.fib          # RRC12 (Frankfurt)  — ~106K IPv6 prefixes
├── rrc13_v6.fib          # RRC13 (Moscow)     — ~104K IPv6 prefixes
├── rrc14_v6.fib          # RRC14 (Palo Alto)  — ~106K IPv6 prefixes
├── rrc15_v6.fib          # RRC15 (Sao Paulo)  — ~117K IPv6 prefixes
├── rrc16_v6.fib          # RRC16 (Miami)      — ~103K IPv6 prefixes
├── rrc18_v6.fib          # RRC18 (Barcelona)  — ~100K IPv6 prefixes
├── rrc19_v6.fib          # RRC19 (Johannesburg) — ~111K IPv6 prefixes
├── rrc20_v6.fib          # RRC20 (Zurich)     — ~108K IPv6 prefixes
├── rrc21_v6.fib          # RRC21 (Paris)      — ~107K IPv6 prefixes
├── rrc22_v6.fib          # RRC22 (Bucharest)  — ~103K IPv6 prefixes
├── rrc23_v6.fib          # RRC23 (Singapore)  — ~108K IPv6 prefixes
├── rrc24_v6.fib          # RRC24 (Montevideo) — ~112K IPv6 prefixes
├── rrc25_v6.fib          # RRC25 (Nairobi)    — ~117K IPv6 prefixes
├── rrc26_v6.fib          # RRC26 (Dubai)      — ~112K IPv6 prefixes
│
├── rrc01_2021.fib .. rrc01_2026.fib   # Year-over-year (2021–2026)
├── rrc03_2021.fib .. rrc03_2026.fib   # Year-over-year (2021–2026)
└── rrc04_2021.fib .. rrc04_2026.fib   # Year-over-year (2021–2026)
```

Prefix counts are approximate and will vary depending on the snapshot date.

---

## References

- [RIPE RIS Route Collectors](https://ris.ripe.net/docs/route-collectors/)
- [RIPE RIS Raw Data Access](https://data.ris.ripe.net/)
- [bgpdump (RIPE NCC)](https://www.ris.ripe.net/source/bgpdump/)
- [SCAMP-Trie Paper (SIGCOMM 2026)](../SCAMP_Trie_SIGCOMM_2026.pdf)
