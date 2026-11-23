"""Summarize raw pipeline storage and reproduce the paper comparisons."""
import argparse
import csv
import math
from functools import lru_cache
from pathlib import Path
from tempfile import NamedTemporaryFile

# (CSV series, raw output subdirectory, allocation ratio file or None)
DETAIL_ALGO_CONFIG = [
    ("olp",     "olp", None),
    ("biolp",   "biolp", None),
    ("minmax",  "minmax", None),
    ("2-phase", "2-phase", None),
    ("treebitmap", "treebitmap", None),
    ("treebitmap-opt", "treebitmap", "treebitmap"),
    ("poptrie", "pop1", None),
    ("poptrie-opt", "pop1", "pop1"),
    ("scamp-trie-basic", "scamptrie-basic", "scamptrie-basic"),
    ("scamp-trie", "scamptrie-opt", "scamptrie"),
]

DETAIL_ALGORITHM_ORDER = [row[0] for row in DETAIL_ALGO_CONFIG]
DETAIL_ALGO_LOOKUP = {row[0]: row for row in DETAIL_ALGO_CONFIG}


def _read_ratio(path):
    """Read either a single weight row or legacy stage-count + weight rows."""
    if not Path(path).is_file():
        raise FileNotFoundError(f"Required ratio file not found: {path}")
    with open(path, "r", encoding="utf-8") as f:
        rows = [line.split() for line in f if line.strip()]
    try:
        if len(rows) == 1:
            weights = [float(x) for x in rows[0]]
            n = len(weights)
        elif len(rows) == 2 and len(rows[0]) == 1:
            n = int(rows[0][0])
            weights = [float(x) for x in rows[1]]
        else:
            raise ValueError("expected one weight row or legacy two-line format")
    except (ValueError, OverflowError) as exc:
        raise ValueError(f"Invalid ratio file {path}: {exc}") from exc
    if n <= 0 or len(weights) != n or any(not math.isfinite(x) or x <= 0 for x in weights):
        raise ValueError(f"Invalid ratio file {path}: weights must be positive finite values and match stage count")
    total = sum(weights)
    if not math.isfinite(total) or total <= 0:
        raise ValueError(f"Invalid ratio file {path}: invalid weight sum")
    return n, [x / total for x in weights]



REPRESENTATIVES = ("rrc00_v6", "isp1_kab", "AWS_256k_v6", "host-rd64")
PAPER_ORDER = Path(__file__).with_name("paper_fibs.txt").read_text().splitlines()
SUMMARY_COLUMNS = ("Filename", "AvgPerPrefix", "TotalEfficiency", "PipelineUsed",
                   "PipelineCapacity", "FixedUsed", "FixedCapacity", "ElasticUsed",
                   "ElasticCapacity", "TotalCapacity")


@lru_cache(maxsize=None)
def prefix_count(dataset):
    with (Path("data") / (dataset + ".fib")).open("rb") as stream:
        return sum(1 for line in stream if line.strip())


def align_capacity(bits):
    return ((math.ceil(bits) + 1023) // 1024) * 1024


def measure(path, ratios):
    rows = path.read_text(encoding="utf-8").splitlines()
    try:
        n = int(rows[0])
        used = [int(x) for x in rows[1].split()]
        if n <= 0 or n != len(used) or any(x < 0 for x in used):
            raise ValueError("invalid stage usage")
        # Legacy two- and three-line baselines only report pipeline usage.
        if len(rows) < 5 and len(rows) not in (2, 3):
            raise ValueError("incomplete storage counters")
        fixed, fixed_used, elastic = map(int, rows[2:5]) if len(rows) >= 5 else (0, 0, 0)
        if min(fixed, fixed_used, elastic) < 0:
            raise ValueError("negative storage counter")
        if ratios is None:
            capacity = align_capacity(max(used) * n)
            shares = [1 / n] * n
        else:
            if len(ratios) != n:
                raise ValueError(f"{n} stages but {len(ratios)} allocation weights")
            shares = ratios
            capacity = align_capacity(max(u / share for u, share in zip(used, shares)))
    except (IndexError, ValueError) as exc:
        raise ValueError(f"{path}: {exc}") from exc
    elastic_capacity = align_capacity(elastic)
    total = capacity + fixed + elastic_capacity
    occupied = sum(used) + fixed_used + elastic
    count = prefix_count(path.name)
    summary = (path.name, f"{occupied / count if count else 0:.2f}",
               f"{occupied / total if total else 0:.4f}", sum(used), capacity,
               fixed_used, fixed, elastic, elastic_capacity, total)
    return summary, ([round(capacity * share) for share in shares], used)


def summarize(subdir, ratio_name):
    directory = Path("output") / subdir
    files = {entry.name: entry for entry in directory.iterdir() if entry.is_file()}
    if not files:
        raise ValueError(f"No raw output files in {directory}")
    ratios = _read_ratio(Path("ratio") / ratio_name)[1] if ratio_name else None
    names = [name for name in PAPER_ORDER if name in files]
    names.extend(sorted(files.keys() - set(names)))
    return {name: measure(files[name], ratios) for name in names}


def write_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    stream = NamedTemporaryFile(mode="w", dir=path.parent, delete=False,
                                newline="", encoding="utf-8")
    tmp = Path(stream.name)
    try:
        with stream:
            csv.writer(stream).writerows(rows)
        tmp.replace(path)
    finally:
        if tmp.exists():
            tmp.unlink()


def write_summary(result_dir, name, records):
    write_csv(result_dir / (name + ".csv"), [SUMMARY_COLUMNS] + [v[0] for v in records.values()])


def write_comparison(result_dir):
    tables = {}
    for name, subdir, ratio_name in DETAIL_ALGO_CONFIG:
        records = summarize(subdir, ratio_name)
        missing = set(REPRESENTATIVES) - records.keys()
        if missing:
            raise ValueError(f"{subdir}: missing representative FIBs: {', '.join(sorted(missing))}")
        tables[name] = records
    nstages = max(len(records[fib][1][1]) for records in tables.values() for fib in REPRESENTATIVES)
    detail = [["Algorithm"] + [f"Stage{i + 1}" for i in range(nstages)]]
    for fib in REPRESENTATIVES:
        for name, records in tables.items():
            for row in records[fib][1]:
                detail.append([name, *row] + [""] * (nstages - len(row)))
    for name, records in tables.items():
        write_summary(result_dir, name, records)
    write_csv(result_dir / "detail.csv", detail)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("algorithm", nargs="?")
    parser.add_argument("--detail", action="store_true", help="create all ten comparisons and stage details")
    args = parser.parse_args()
    result = Path("result")
    try:
        if args.detail:
            write_comparison(result)
        elif args.algorithm:
            name = args.algorithm
            if name in ("scamptrie", "scamptrie-opt"):
                subdir, ratio = name, "scamptrie"
            elif name == "scamptrie-basic":
                subdir, ratio = name, name
            elif name in ("treebitmap", "pop1"):
                # Preserve the standalone CLI's ratio-aware summaries.
                subdir, ratio = name, name if (Path("ratio") / name).is_file() else None
            elif name in DETAIL_ALGO_LOOKUP:
                _, subdir, ratio = DETAIL_ALGO_LOOKUP[name]
            else:
                subdir, ratio = name, name if (Path("ratio") / name).is_file() else None
            write_summary(result, name, summarize(subdir, ratio))
        else:
            parser.error("specify an algorithm or --detail")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"util.py: {exc}\n")
    print(f"Results saved in {result}")


if __name__ == "__main__":
    main()
