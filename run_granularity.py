#!/usr/bin/env python3
"""
Granularity sweep for the three string-matching algorithms.

"Grain size" G = N / P = number of text characters per thread. Too fine a
grain and thread-management overhead dominates; too coarse a grain and there
is no parallelism. This script samples the spectrum by holding N fixed and
running a FINE ladder of thread counts (P), for KMP, Rabin-Karp and
Boyer-Moore, writing granularity.csv for plot_granularity.py.

Usage:
  python run_granularity.py                       # N = 1e6 and 1e7, P ladder 1..1024
  python run_granularity.py --sizes 10000000 --table 1,2,4,8,16,32,64,128,256,1024

Output:
  granularity.csv : algo,n,threads,grain,time_ms
"""
import argparse
import csv
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(HERE, "build")

# Start from build/kmp_bench etc. produced by run_benchmark.py
EXES = {
    "kmp": os.path.join(BUILD_DIR, "kmp_bench.exe" if os.name == "nt" else "kmp_bench"),
    "rk":  os.path.join(BUILD_DIR, "rk_bench.exe" if os.name == "nt" else "rk_bench"),
    "bm":  os.path.join(BUILD_DIR, "bm_bench.exe" if os.name == "nt" else "bm_bench"),
}

# Fine ladder to resolve the overhead-dominated and saturation regions
DEFAULT_P_LADDER = [1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 128, 256, 512, 1024]
DEFAULT_SIZES = [1_000_000, 10_000_000]


def reps_for(n):
    if n <= 1_000_000:
        return 4
    return 3


def run_one(exe, threads, n):
    verify = "0"
    cmd = [exe, str(threads), str(n), "5", str(reps_for(n)), verify]
    res = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0 or not res.stdout.strip():
        print(f"WARNING: run failed ({' '.join(cmd)}): {res.stderr.strip()[:200]}")
        return None
    parts = res.stdout.strip().split(",")
    algo, th, tn, _m, t_ms, matches, _v = parts
    return {
        "algo": algo,
        "n": int(tn),
        "threads": int(th),
        "grain": int(tn) // max(int(th), 1),
        "time_ms": float(t_ms),
        "matches": int(matches),
    }


def ensure_built():
    for tag, exe in EXES.items():
        if not os.path.exists(exe):
            sys.exit(f"ERROR: {exe} missing – run `python run_benchmark.py` first "
                     f"(it compiles all three binaries). ({tag})")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sizes", default=",".join(map(str, DEFAULT_SIZES)),
                    help="fixed text sizes N to sample (comma-separated)")
    ap.add_argument("--table", default=",".join(map(str, DEFAULT_P_LADDER)),
                    help="thread-count ladder P (grain = N/P)")
    ap.add_argument("--out", default=os.path.join(HERE, "granularity.csv"))
    args = ap.parse_args()

    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
    ladder = sorted({int(p) for p in args.table.split(",") if p.strip()})
    if 1 not in ladder:
        ladder = [1] + ladder      # serial baseline is mandatory

    ensure_built()

    rows = []
    for n in sizes:
        print(f"--- fixed N = {n} ---", flush=True)
        for algo in ("kmp", "rk", "bm"):
            serial_time = None
            for p in ladder:
                row = run_one(EXES[algo], p, n)
                if row is None:
                    continue
                rows.append(row)
                if p == 1:
                    serial_time = row["time_ms"]
                sp = (serial_time / row["time_ms"]) if serial_time and row["time_ms"] > 0 else float("nan")
                print(f"  {algo:3s} P={p:5d} grain={row['grain']:>10,} chars "
                      f"time={row['time_ms']:.3f} ms  S={sp:5.2f}x", flush=True)

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["algo", "n", "threads", "grain",
                                          "time_ms", "matches"])
        w.writeheader()
        w.writerows(rows)
    print(f"\nWrote {len(rows)} rows to {args.out}")


if __name__ == "__main__":
    main()
