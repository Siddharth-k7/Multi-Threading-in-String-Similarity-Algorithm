#!/usr/bin/env python3
"""
Charts from results.csv (produced by run_benchmark.py):

  fig_time_vs_threads.png        - Number of threads vs. execution time (log-log, one curve per N)
  fig_speedup_vs_threads.png     - Number of threads vs. speedup (ideal linear speedup shown dashed)
  fig_efficiency_vs_threads.png  - Number of threads vs. parallel efficiency (E = S/P, ideal E=1 dashed)

Each figure has one panel per algorithm: KMP | Rabin-Karp | Boyer-Moore.
Speedup  S(P, N) = T_serial(N) / T_parallel(P, N)

Usage:
  python plot_benchmark.py [--csv results.csv] [--outdir .]
"""
import argparse
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

HERE = os.path.dirname(os.path.abspath(__file__))

ALGO_LABEL = {"kmp": "KMP (Pthreads)", "rk": "Rabin-Karp (Pthreads)",
              "bm": "Boyer-Moore (Pthreads)"}


def load(csv_path):
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "algo": r["algo"],
                "threads": int(r["threads"]),
                "n": int(r["n"]),
                "m": int(r["m"]),
                "time_ms": float(r["time_ms"]),
                "matches": int(r["matches"]),
            })
    return rows


def build_series(rows):
    """-> data[algo][n] = {threads: [mapped lists]}
    serial_time[algo][n] = time at 1 thread"""
    times = defaultdict(lambda: defaultdict(dict))
    serial_time = defaultdict(dict)
    for r in rows:
        times[r["algo"]][r["n"]][r["threads"]] = r["time_ms"]
        if r["threads"] == 1:
            serial_time[r["algo"]][r["n"]] = r["time_ms"]
    return times, serial_time


def style_iter():
    """Deterministic color/marker cycle for N-value curves."""
    colors = plt.get_cmap("tab20")
    markers = ["o", "s", "^", "D", "v", "P", "X", "*", "<", ">"]
    i = 0
    while True:
        yield colors(i % 20), markers[i % len(markers)]
        i += 1


def plot_grouped(metric, times, serial_time, outfile, ylabel, logy,
                 add_ideal=False, annotate_cpu=0):
    fig, axes = plt.subplots(1, 3, figsize=(19, 5.6))
    for ax, algo in zip(axes, ("kmp", "rk", "bm")):
        style = style_iter()
        ns = sorted(times[algo].keys())
        for n in ns:
            pts = sorted((p, t) for p, t in times[algo][n].items() if t > 0)
            xs = [p for p, _ in pts]
            if metric == "time":
                ys = [t for _, t in pts]
            elif metric == "speedup":
                ts = serial_time[algo][n]
                ys = [ts / t for _, t in pts]
            else:  # efficiency
                ts = serial_time[algo][n]
                ys = [ts / t / p for p, t in pts]
            color, marker = next(style)
            ax.plot(xs, ys, color=color, marker=marker, ms=4, lw=1.4, label=f"N={n:,}")

        ax.set_xscale("log", base=2)
        if logy:
            ax.set_yscale("log")
        ax.set_xlabel("Number of threads P (log scale)")
        ax.set_ylabel(ylabel)
        ax.set_title(ALGO_LABEL[algo], fontsize=12)
        ax.grid(True, which="both", ls=":", alpha=0.5)
        ax.legend(fontsize=6.5, ncol=2, loc="best")

        if add_ideal and ns:
            # ideal linear speedup / constant efficiency guide lines
            smax = max(p for p, _ in
                       sorted((p, t) for p, t in times[algo][max(ns)].items()))
            if metric == "speedup":
                ax.plot([1, smax], [1, smax], "k--", lw=1.2, alpha=0.7)
            elif metric == "efficiency":
                ax.axhline(1.0, color="k", ls="--", lw=1.2, alpha=0.7)

    if annotate_cpu:
        fig.suptitle(f"{ylabel.split('(')[0].strip()} vs. number of threads "
                     f"(M=5, machine: {annotate_cpu} logical cores CPU-hardware-dependent)",
                     fontsize=11)
    else:
        fig.suptitle(f"{ylabel.split('(')[0].strip()} vs. number of threads (M = 5)", fontsize=12)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(outfile, dpi=150)
    plt.close(fig)
    print(f"saved {outfile}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--csv", default=os.path.join(HERE, "results.csv"))
    ap.add_argument("--outdir", default=HERE)
    ap.add_argument("--cores", type=int, default=0,
                    help="logical CPU cores, shown in titles (optional)")
    args = ap.parse_args()

    rows = load(args.csv)
    times, serial_time = build_series(rows)

    plot_grouped("time", times, serial_time,
                  os.path.join(args.outdir, "fig_time_vs_threads.png"),
                  "Execution time (ms)", logy=True)
    plot_grouped("speedup", times, serial_time,
                  os.path.join(args.outdir, "fig_speedup_vs_threads.png"),
                  "Speedup  S = T1 / TP", logy=False, add_ideal=True,
                  annotate_cpu=args.cores)
    plot_grouped("efficiency", times, serial_time,
                  os.path.join(args.outdir, "fig_efficiency_vs_threads.png"),
                  "Efficiency  E = S / P", logy=False, add_ideal=True)

    # Combined side-by-side summary table printed to console
    print("\nSpeedup summary (S = T_serial / T_parallel):")
    for algo in ("kmp", "rk", "bm"):
        for n in sorted(serial_time[algo]):
            ts = serial_time[algo][n]
            line = [f"N={n:>12,}"]
            for p in (2, 8, 64, 1024):
                t = times[algo][n].get(p)
                line.append(f"P={p:<4d}: {ts / t:5.2f}x" if t else f"P={p:<4d}: -")
            print(f"  {algo:3s} " + "  ".join(line))


if __name__ == "__main__":
    main()
