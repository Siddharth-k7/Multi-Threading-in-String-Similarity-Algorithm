#!/usr/bin/env python3
"""
Granularity comparison chart for all three algorithms.

Reads granularity.csv (from run_granularity.py) and draws
fig_granularity.png: one row per text size N, three columns per row:

  col 1: execution time vs grain size G = N/P (log x; U-shape)
  col 2: speedup vs grain size (dashed = the ideal-speedup curve S = P = N/G)
  col 3: parallel efficiency vs grain size (E = 1.0 reference dashed)

Finer grain (small G, left) = overhead-dominated; coarser grain (large G,
right) = little parallelism. The sweet spot G* is where time bottoms out /
efficiency starts to fall.

Usage:
  python plot_granularity.py [--csv granularity.csv] [--out fig_granularity.png]
"""
import argparse
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))

COLORS = {"kmp": "tab:blue", "rk": "tab:orange", "bm": "tab:green"}
LABELS = {"kmp": "KMP", "rk": "Rabin-Karp", "bm": "Boyer-Moore"}
ALGOS = ("kmp", "rk", "bm")


def load(csv_path):
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "algo": r["algo"],
                "n": int(r["n"]),
                "threads": int(r["threads"]),
                "grain": max(int(r["grain"]), 1),
                "time_ms": float(r["time_ms"]),
            })
    return rows


def series(rows):
    """data[n][algo][threads] = time_ms"""
    data = defaultdict(lambda: defaultdict(dict))
    for r in rows:
        data[r["n"]][r["algo"]][r["threads"]] = r["time_ms"]
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--csv", default=os.path.join(HERE, "granularity.csv"))
    ap.add_argument("--out", default=os.path.join(HERE, "fig_granularity.png"))
    args = ap.parse_args()

    rows = load(args.csv)
    data = series(rows)
    n_values = sorted(data.keys())
    if not n_values:
        raise SystemExit("granularity.csv is empty")

    ncols = 3
    nrows = len(n_values)
    fig, axes = plt.subplots(nrows, ncols, figsize=(17, 4.8 * nrows),
                             squeeze=False)

    for row_i, n in enumerate(n_values):
        ax_time, ax_speed, ax_eff = axes[row_i]
        serial = {a: data[n][a][1] for a in ALGOS if 1 in data[n][a]}

        xs = None
        for a in ALGOS:
            pts = sorted(data[n][a].items())          # [(threads, time), ...]
            if not pts:
                continue
            xs = [max(1, n // p) for p, _ in pts]     # grain (chars per thread)
            ts = [t for _, t in pts]
            t1 = serial.get(a)
            if t1 is None or t1 <= 0:
                continue

            ax_time.plot(xs, ts, "-o", ms=4, lw=1.6, color=COLORS[a],
                         label=LABELS[a])
            ax_time.axhline(t1, color=COLORS[a], ls=":", lw=1.0, alpha=0.7,
                            label=f"{LABELS[a]} serial")
            ax_speed.plot(xs, [t1 / t for t in ts], "-o", ms=4, lw=1.6,
                          color=COLORS[a], label=LABELS[a])
            ax_eff.plot(xs, [t1 / t / p for p, t in pts], "-o", ms=4, lw=1.6,
                        color=COLORS[a], label=LABELS[a])

        if xs:
            # Ideal speedup achievable at each grain level: S = P = N/G
            ideal = [n / max(g, 1) for g in xs]
            ax_speed.plot(xs, ideal, "k--", lw=1.2, alpha=0.6,
                          label="ideal S = N/G")
        ax_eff.axhline(1.0, color="k", ls="--", lw=1.2, alpha=0.6)

        for ax, ylab, title in (
            (ax_time, "Execution time (ms)",
             "time (overhead ↑ on fine grain, parallelism ↓ on coarse grain)"),
            (ax_speed, "Speedup  S = T1 / TP", "speedup"),
            (ax_eff, "Efficiency  E = S / P", "efficiency"),
        ):
            ax.set_xscale("log", base=2)
            ax.set_xlabel("Grain size  G = N/P  (chars per thread, log)")
            ax.set_ylabel(ylab)
            ax.set_title(f"N = {n:,} — {title}", fontsize=11)
            ax.grid(True, which="both", ls=":", alpha=0.5)
            ax.legend(fontsize=8)

    fig.suptitle("Granularity comparison — chars per thread vs time / speedup / "
                 "efficiency (dotted horizontal lines = each algorithm's serial time)",
                 fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
