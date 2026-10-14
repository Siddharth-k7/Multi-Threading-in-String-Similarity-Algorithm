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
