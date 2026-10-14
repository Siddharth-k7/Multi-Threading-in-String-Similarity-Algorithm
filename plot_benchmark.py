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
