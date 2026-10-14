#!/usr/bin/env python3
"""
Throughput sweep for the three string-matching algorithms.

Measures searchable bandwidth:  throughput (GB/s) = N bytes / T(ms) / 1e6.
For each pattern length (short M = 4, long M = 32) the thread ladder
1 (sequential), 4, 8, 16, 24, 48 is run across input sizes for KMP,
Rabin-Karp and Boyer-Moore, writing throughput.csv for plot_throughput.py.

Usage:
  python run_throughput.py                        # default sizes below
  python run_throughput.py --sizes 100000,1000000 --threads 1,8
  python run_throughput.py --full                 # add N = 1e9 (needs ~1 GB RAM)

Output:
  throughput.csv : algo,m,n,threads,time_ms,mbytes_per_s
"""
import argparse
import csv
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(HERE, "build")

EXES = {
    "kmp": os.path.join(BUILD_DIR, "kmp_bench.exe" if os.name == "nt" else "kmp_bench"),
    "rk":  os.path.join(BUILD_DIR, "rk_bench.exe" if os.name == "nt" else "rk_bench"),
    "bm":  os.path.join(BUILD_DIR, "bm_bench.exe" if os.name == "nt" else "bm_bench"),
}

DEFAULT_SIZES = [1_000, 10_000, 100_000, 1_000_000, 10_000_000, 100_000_000]
FULL_EXTRA = [1_000_000_000]
# Points-per-decade refinement for smoother curves on the log axis
DEFAULT_SIZES += [3_000, 30_000, 300_000, 3_000_000, 30_000_000]
DEFAULT_THREADS = [1, 4, 8, 16, 24, 48]
DEFAULT_M = [4, 32]


def reps_for(n):
    if n < 100_000:
        return 5
    if n <= 10_000_000:
        return 3
    return 2


def run_one(exe, threads, n, m):
    cmd = [exe, str(threads), str(n), str(m), str(reps_for(n)), "0"]
    res = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0 or not res.stdout.strip():
        print(f"WARNING: run failed ({' '.join(cmd)}): {res.stderr.strip()[:200]}")
        return None
    parts = res.stdout.strip().split(",")
    algo, th, tn, tm, t_ms, _matches, _v = parts
    n_bytes = float(tn)                     # 1 char = 1 byte in the generator
    ms = float(t_ms)
    if ms <= 0:
        return None
    return {
        "algo": algo,
        "m": int(tm),
        "n": int(tn),
        "threads": int(th),
        "time_ms": ms,
        # GB/s = bytes / (ns): ms * 1e6 = ns; bytes / (ms*1e6) = GB/s
        "mbytes_per_s": n_bytes / (ms * 1e6) * 1000.0,
    }


def ensure_built():
    missing = [tag for tag, exe in EXES.items() if not os.path.exists(exe)]
    if missing:
        sys.exit(f"ERROR: missing executables {missing} - run `python run_benchmark.py` first.")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sizes", default=",".join(map(str, DEFAULT_SIZES)),
                    help="comma-separated input sizes N (bytes = chars)")
    ap.add_argument("--threads", default=",".join(map(str, DEFAULT_THREADS)),
                    help="comma-separated thread counts (1 = sequential)")
    ap.add_argument("--ms", default=",".join(map(str, DEFAULT_M)),
                    help="comma-separated pattern lengths (default 4,32)")
    ap.add_argument("--full", action="store_true",
                    help="also run N = 1,000,000,000 (needs ~1 GB RAM)")
    ap.add_argument("--out", default=os.path.join(HERE, "throughput.csv"))
    args = ap.parse_args()

    sizes = sorted({int(s) for s in args.sizes.split(",") if s.strip()})
    threads = sorted({int(t) for t in args.threads.split(",") if t.strip()})
    ms_list = [int(m) for m in args.ms.split(",") if m.strip()]
    if args.full and 1_000_000_000 not in sizes:
        sizes.append(1_000_000_000)
    if 1 not in threads:
        threads = [1] + threads

    ensure_built()

    rows = []
    for m in ms_list:
        print(f"=== pattern length M = {m} ===", flush=True)
        for algo in ("kmp", "rk", "bm"):
            for n in sizes:
                for p in threads:
                    row = run_one(EXES[algo], p, n, m)
                    if row is None:
                        continue
                    rows.append(row)
                    print(f"  {algo:3s} M={m:2d} N={n:>12,} P={p:3d} "
                          f"T={row['time_ms']:9.3f} ms  "
                          f"thpt={row['mbytes_per_s']:6.2f} MB/s", flush=True)

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["algo", "m", "n", "threads",
                                          "time_ms", "mbytes_per_s"])
        w.writeheader()
        w.writerows(rows)
    print(f"\nWrote {len(rows)} rows to {args.out}")


if __name__ == "__main__":
    main()
