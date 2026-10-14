#!/usr/bin/env python3
"""
Throughput vs input size — figure in the style of the reference chart:

one row per pattern length (short M=4, long M=32), one COLUMN per algorithm
(KMP | Rabin-Karp | Boyer-Moore). Lines = sequential (black) + 4/8/16/24/48
threads. x = input size (bytes, log), y = throughput (GB/s).

Reads throughput.csv (from run_throughput.py).

Usage:
  python plot_throughput.py [--csv throughput.csv] [--out fig_throughput.png]
"""
import argparse
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))

THREAD_COLOR = {
    1: "black",
    4: "tab:blue",
    8: "tab:orange",
    16: "tab:green",
    24: "tab:red",
    48: "orchid",
}
ALGO_LABEL = {"kmp": "KMP", "rk": "Rabin-Karp", "bm": "Boyer-Moore"}
ALGOS = ("kmp", "rk", "bm")


def load(csv_path):
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "algo": r["algo"],
                "m": int(r["m"]),
                "n": int(r["n"]),
                "threads": int(r["threads"]),
                "mbytes_per_s": float(r["mbytes_per_s"]),   # MB/s (converted to GB/s at plot time)
            })
    return rows


def series(rows):
    """data[m][algo][threads] = {n: mbytes_per_s}"""
    data = defaultdict(lambda: defaultdict(lambda: defaultdict(dict)))
    for r in rows:
        data[r["m"]][r["algo"]][r["threads"]][r["n"]] = r["mbytes_per_s"]
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--csv", default=os.path.join(HERE, "throughput.csv"))
    ap.add_argument("--out", default=os.path.join(HERE, "fig_throughput.png"))
    args = ap.parse_args()

    rows = load(args.csv)
    data = series(rows)
    ms_list = sorted(data.keys())
    if not ms_list:
        raise SystemExit("throughput.csv has no rows")

    ncols = len(ALGOS)
    nrows = len(ms_list)
    fig, axes = plt.subplots(nrows, ncols, figsize=(6.0 * ncols, 4.6 * nrows),
                             squeeze=False, sharey="row")

    for i, m in enumerate(ms_list):
        for j, algo in enumerate(ALGOS):
            ax = axes[i][j]
            d = data[m][algo]
            # union of all N logged for this panel, sequential line first
            thread_levels = sorted(d.keys())
            ns = sorted({n for t in thread_levels for n in d[t]})
            for t in thread_levels:
                pts = [(n, d[t][n]) for n in ns if n in d[t]]
                if not pts:
                    continue
                xs, ys = zip(*pts)
                ys = [y / 1000.0 for y in ys]   # CSV stores MB/s -> plot in GB/s
                color = THREAD_COLOR.get(t, None)
                lw = 2.0 if t == 1 else 1.6
                label = ("sequential" if t == 1 else f"{t} threads")
                ax.plot(xs, ys, "-o", ms=4, lw=lw, color=color, label=label)

            ax.set_xscale("log")
            ax.set_xlabel("input size (bytes)")
            if j == 0:
                ax.set_ylabel("throughput (GB/s)")
            short_long = "short" if m <= 8 else "long"
            ax.set_title(f"{ALGO_LABEL[algo]} — {short_long} pattern "
                         f"({m} chars)", fontsize=11)
            ax.grid(True, which="both", ls=":", alpha=0.5)
            if i == 0 and j == ncols - 1:
                ax.legend(fontsize=9, loc="upper left")

    fig.suptitle("Throughput vs input size — all three algorithms "
                 "(1 char = 1 byte; best-of-repetitions timing)", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
