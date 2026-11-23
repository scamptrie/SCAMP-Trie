#!/usr/bin/env python3
"""Plot storage utilization and stage usage from the CSVs produced by util.py."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator, PercentFormatter

from util import DETAIL_ALGORITHM_ORDER

REPRESENTATIVES = ("rrc00_v6", "isp1_kab", "AWS_256k_v6", "host-rd64")
PAPER_FIBS = Path(__file__).with_name("paper_fibs.txt").read_text().splitlines()
PAPER_LABELS = ([f"ixp{i}" for i in range(1, 24)] + [f"isp{i}" for i in range(1, 8)]
                + ["cg1", "cg2", "cg3", "host1", "host2", "host3"])
LABELS = dict(zip(PAPER_FIBS, PAPER_LABELS))
NAMES = ("OLP", "BiOLP", "MinMax", "2-phase", "Tree Bitmap", "Tree Bitmap-Opt",
         "Poptrie", "Poptrie-Opt", "SCAMP-Trie-Basic", "SCAMP-Trie")
PROFILE_SERIES = ("biolp", "2-phase", "poptrie", "scamp-trie")
DISPLAY = dict(zip(DETAIL_ALGORITHM_ORDER, NAMES))


def read_utilization(directory, fibs):
    series = {}
    for algo in DETAIL_ALGORITHM_ORDER:
        path = directory / f"{algo}.csv"
        with path.open(encoding="utf-8-sig", newline="") as stream:
            rows = csv.DictReader(stream, skipinitialspace=True)
            rows.fieldnames = [name.strip() for name in rows.fieldnames or []]
            values = {row["Filename"].strip(): float(row["TotalEfficiency"]) for row in rows}
        for fib in fibs:
            if fib not in values or not 0 <= values[fib] <= 1:
                raise ValueError(f"{path}: missing or invalid utilization for {fib}")
        series[algo] = [values[fib] for fib in fibs]
    return series


def read_profiles(path):
    # detail.csv stores paired allocated/used rows in the order of util.py.
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = csv.reader(stream)
        header = next(rows, [])
        if header[:2] != ["Algorithm", "Stage1"]:
            raise ValueError(f"{path}: invalid stage header")
        profiles = {}
        for fib in REPRESENTATIVES:
            for algo in DETAIL_ALGORITHM_ORDER:
                allocated, used = next(rows, []), next(rows, [])
                if allocated[:1] != [algo] or used[:1] != [algo]:
                    raise ValueError(f"{path}: expected allocated and used rows for {fib}/{algo}")
                values = [int(cell) for cell in used[1:] if cell.strip()]
                if not values or any(value < 0 for value in values):
                    raise ValueError(f"{path}: invalid stage usage for {fib}/{algo}")
                profiles[fib, algo] = values
        if next(rows, None) is not None:
            raise ValueError(f"{path}: unexpected extra detail rows")
    return profiles


def render_utilization(series, fibs, path):
    fig, ax = plt.subplots(figsize=(max(8, len(fibs) * 0.47), 4.1), constrained_layout=True)
    step = 0.88 / len(series)
    for i, (algo, values) in enumerate(series.items()):
        xs = [j - 0.44 + (i + 0.5) * step for j in range(len(fibs))]
        ax.bar(xs, values, width=step, label=DISPLAY[algo])
    ax.set_xticks(range(len(fibs)))
    ax.set_xticklabels([LABELS[f] for f in fibs], rotation=35 if len(fibs) > 4 else 0)
    ax.set(ylabel="Memory utilization", ylim=(0, 1.03))
    ax.yaxis.set_major_formatter(PercentFormatter(1))
    ax.set_axisbelow(True)
    ax.grid(axis="y", alpha=.22)
    ax.legend(loc="lower center", bbox_to_anchor=(.5, 1), ncol=5, fontsize=8, frameon=False)
    fig.savefig(path)
    plt.close(fig)


def render_profiles(profiles, path):
    fig, axes = plt.subplots(2, 2, figsize=(9, 5), constrained_layout=True)
    for ax, fib in zip(axes.flat, REPRESENTATIVES):
        for algo in PROFILE_SERIES:
            used = profiles[fib, algo]
            ax.plot(range(len(used)), [bits / 1e6 for bits in used], label=DISPLAY[algo])
        ax.invert_xaxis()  # The paper displays S0 on the right.
        ax.set(title=LABELS[fib], xlabel="Stage index", ylabel="Memory used (Mbit)")
        ax.xaxis.set_major_locator(MaxNLocator(integer=True, nbins=7))
        ax.set_ylim(bottom=0)
        ax.grid(alpha=.22)
    axes[0, 0].legend(fontsize=8, frameon=False)
    fig.savefig(path)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-dir", type=Path, default=Path("result"))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--full", action="store_true", help="use all 36 paper FIBs for the utilization plot")
    args = parser.parse_args()
    fibs = PAPER_FIBS if args.full else REPRESENTATIVES
    output = args.output_dir or args.result_dir / "figures"
    try:
        series = read_utilization(args.result_dir, fibs)
        profiles = read_profiles(args.result_dir / "detail.csv")
        output.mkdir(parents=True, exist_ok=True)
        render_utilization(series, fibs, output / "fig11_utilization.svg")
        render_profiles(profiles, output / "fig12_stages.svg")
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f"plot_storage: {exc}\n")
    print(f"Figures saved to {output}")


if __name__ == "__main__":
    main()
