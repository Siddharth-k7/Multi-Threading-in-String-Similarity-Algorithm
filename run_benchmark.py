#!/usr/bin/env python3
"""
Benchmark driver for the multithreaded string-matching assignment.

Runs BOTH algorithms (KMP and Rabin-Karp):
  - serial baseline            : threads = 1
  - pthreads parallel versions : threads = 2, 4, 8, ... up to 1024
across text sizes N (question string length), and writes all timings into
results.csv for the plotting script.

Usage:
  python run_benchmark.py                          # default sweep (N up to 1e7)
  python run_benchmark.py --threads 2,4,8 --sizes 5,10,100
  python run_benchmark.py --full                   # adds the giant N = 1e8 and 1e9 runs
                                                   # (N = 1e9 needs ~1 GB free RAM and several
                                                   #  minutes; text generation dominates there)

Output:
  results.csv : algo,threads,n,m,time_ms,matches,verified
"""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(HERE, "build")

DEFAULT_THREADS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]
DEFAULT_SIZES = [5, 10, 100, 1_000, 10_000, 100_000, 1_000_000, 10_000_000]
FULL_EXTRA_SIZES = [100_000_000, 1_000_000_000]

SOURCES = {
    "kmp": "kmp_pthreads_bench.c",
    "rk":  "rabin_karp_pthreads_bench.c",
    "bm":  "parallel_bm.cpp",          # Boyer-Moore (serial + Pthreads, C++)
}
EXES = {
    "kmp": os.path.join(BUILD_DIR, "kmp_bench.exe" if os.name == "nt" else "kmp_bench"),
    "rk":  os.path.join(BUILD_DIR, "rk_bench.exe" if os.name == "nt" else "rk_bench"),
    "bm":  os.path.join(BUILD_DIR, "bm_bench.exe" if os.name == "nt" else "bm_bench"),
}


def compile_all():
    os.makedirs(BUILD_DIR, exist_ok=True)
    gcc = shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    gxx = shutil.which("g++") or gcc
    if gcc is None:
        sys.exit("ERROR: no C compiler (gcc/cc/clang) found on PATH.")
    for algo, src in SOURCES.items():
        cc = gxx if src.endswith(".cpp") else gcc
        # Try with -pthread/-lpthread first (Linux/MinGW-w64), then plain.
        commands = [
            [cc, "-O2", "-Wall", "-Wextra", src, "-o", EXES[algo], "-pthread"],
            [cc, "-O2", "-Wall", "-Wextra", src, "-o", EXES[algo]],
        ]
        ok = False
        for cmd in commands:
            res = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace")
            if res.returncode == 0:
                ok = True
                break
        if not ok:
            print(res.stderr, file=sys.stderr)
            sys.exit(f"ERROR: compilation failed for {src}")
        print(f"compiled {src} -> {EXES[algo]}")


def reps_for(n):
    """More repetitions for small inputs whose timings are noisy."""
    if n <= 10_000:
        return 5
    if n <= 1_000_000:
        return 3
    return 2


def run_one(exe, threads, n, m, verify):
    reps = reps_for(n)
    cmd = [exe, str(threads), str(n), str(m), str(reps), "1" if verify else "0"]
    res = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0 or not res.stdout.strip():
        print(f"  WARNING: run failed ({' '.join(cmd)}): {res.stderr.strip()[:200]}")
        return None
    # expected: algo,threads,n,m,time_ms,matches,verified
    parts = res.stdout.strip().split(",")
    if len(parts) != 7:
        print(f"  WARNING: unexpected output: {res.stdout.strip()[:200]}")
        return None
    algo, th, tn, tm, t_ms, matches, verified = parts
    return {
        "algo": algo,
        "threads": int(th),
        "n": int(tn),
        "m": int(tm),
        "time_ms": float(t_ms),
        "matches": int(matches),
        "verified": int(verified),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--threads", default=",".join(map(str, DEFAULT_THREADS)),
                    help="comma-separated thread counts (1 = serial baseline)")
    ap.add_argument("--sizes", default=",".join(map(str, DEFAULT_SIZES)),
                    help="comma-separated text sizes N")
    ap.add_argument("--full", action="store_true",
                    help="also run the giant sizes N=1e8 and N=1e9")
    ap.add_argument("--m", type=int, default=5, help="pattern length M (default 5)")
    ap.add_argument("--out", default=os.path.join(HERE, "results.csv"))
    ap.add_argument("--verify-limit", type=float, default=1e7,
                    help="verify correctness against serial for N <= this (default 1e7)")
    args = ap.parse_args()

    threads_list = [int(x) for x in args.threads.split(",") if x.strip()]
    sizes_list = [int(x) for x in args.sizes.split(",") if x.strip()]
    if args.full and sizes_list[-1] < FULL_EXTRA_SIZES[-1]:
        sizes_list += [s for s in FULL_EXTRA_SIZES if s > sizes_list[-1]]

    if 1 not in threads_list:
        threads_list = [1] + threads_list

    compile_all()

    rows = []
    total = len(threads_list) * len(sizes_list) * len(SOURCES)
    done = 0
    t_start = time.time()
    for n in sizes_list:
        verify = n <= args.verify_limit
        print(f"--- N = {n} (verify={verify}) ---", flush=True)
        for algo in ("kmp", "rk", "bm"):
            for p in threads_list:
                row = run_one(EXES[algo], p, n, args.m, verify)
                done += 1
                if row is None:
                    continue
                rows.append(row)
                print(f"  [{done}/{total}] {algo:3s} P={p:5d} "
                      f"time={row['time_ms']:.3f} ms matches={row['matches']} "
                      f"verified={row['verified']} "
                      f"(elapsed {time.time()-t_start:.0f}s)", flush=True)

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["algo", "threads", "n", "m",
                                          "time_ms", "matches", "verified"])
        w.writeheader()
        w.writerows(rows)

    # verification applies only to parallel runs; serial baseline rows are the reference itself
    bad = [r for r in rows if r["threads"] != 1 and r["verified"] == 0]
    print(f"\nWrote {len(rows)} rows to {args.out}")
    if bad:
        bad_n = sorted({(r['algo'], r['n']) for r in bad})
        print(f"WARNING: {len(bad)} runs FAILED verification: {bad_n}")
    else:
        print("All verification-enabled runs PASSED (parallel == serial results).")


if __name__ == "__main__":
    main()
