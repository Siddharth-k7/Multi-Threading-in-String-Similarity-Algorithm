# Parallel String Matching — KMP, Rabin-Karp & Boyer-Moore (Pthreads) Benchmark

This document covers the **parallel implementations and scalability analysis** for
all three string-grep approaches used in the project:

* `kmp.c` — serial KMP baseline
* `Rabin-Karp-Algo.cpp` — serial Rabin-Karp baseline
* `boyer_moore.cpp` + `boyer_moore.h` — serial Boyer-Moore library
  (bad-character + good-suffix heuristics; the header was missing and has been
  reconstructed so the library builds)
* `kmp_pthreads_bench.c` / `rabin_karp_pthreads_bench.c` / `parallel_bm.cpp` —
  the **benchmark executables** that run the *same* algorithm serially
  (`num_threads = 1`) or in parallel (2 … 1024 Pthreads)

Parity guarantees between the three benchmark programs: identical deterministic
text generator (seed 42 + pattern embedded every 1,000 chars and at position 0),
identical pattern generator (seed 123), identical CLI, identical CSV output, and
identical timing scopes (parallel runs time partition + create + join + combine;
preprocessing is excluded everywhere).

---

## 1. Files

| File | Purpose |
|---|---|
| `kmp_pthreads_bench.c` | KMP: serial + Pthreads-parallel benchmark (CLI: threads, N, M) |
| `rabin_karp_pthreads_bench.c` | Rabin-Karp: serial + Pthreads-parallel benchmark |
| `boyer_moore.h` | Reconstructed header of the BM library (namespace `bm`) |
| `boyer_moore.cpp` | Serial Boyer-Moore library (baseline) |
| `parallel_bm.cpp` | **Boyer-Moore: serial + Pthreads-parallel benchmark executable** (self-contained `const char*` port of the `bm::` logic so it builds on toolchains without `std::string_view`; threads, N, M via CLI) |
| `pthread_compat.h` | Real `<pthread.h>` on Linux/MSYS2; Win32-backed fallback for MinGW.org compilers without winpthreads |
| `run_benchmark.py` | Compiles all three programs and sweeps threads × N into `results.csv` |
| `plot_benchmark.py` | Matplotlib charts: time, speedup, efficiency vs. threads |
| `results.csv` | Raw timings; last run = 264 rows = **3 algorithms** × 11 thread counts × 8 text sizes |
| `fig_time_vs_threads.png` | Number of threads vs. execution time |
| `fig_speedup_vs_threads.png` | Number of threads vs. speedup (+ ideal line) |
| `fig_efficiency_vs_threads.png` | Number of threads vs. efficiency (+ E = 1 line) |

## 2. Design (all algorithms, incl. Boyer-Moore)

* **Partitioning** — the text is split by *window-start position* into `P`
  contiguous chunks; thread *i* owns every match whose **start index** lies in
  its chunk `[c_i, c_{i+1})`.
* **Boyer-Moore specifics (parallel_bm.cpp)** — the chunk is scanned with the
  classic right-to-left window comparison, and window slides use
  `max(bad-character shift, good-suffix shift)`. Because every slide is a
  *pure jump* computed from the pattern tables plus the mismatched char, a
  chunk's scan never depends on another thread's window state; still, a window
  may *exchange* extra characters beyond its chunk, so every occurrence whose
  START belongs to the chunk is caught even when the pattern textually crosses
  the boundary.
* **Thread safety** — text, pattern, LPS array / pattern hash / BM tables are
  shared read-only; each thread collects matches in a thread-local buffer
  (no locks). Ownership makes the concatenated result sorted and
  duplicate-free.
* **Combining** — after `pthread_join`, per-thread counts are summed (and, on
  the verifying pass, indices concatenated — already sorted).
* **Verification** — every parallel run is checked against the serial result
  for the same input (same count **and** same sorted indices).

## 3. How to build & run

```bash
# single experiment  (Linux / MSYS2 with real pthreads)
gcc -O2 -Wall -pthread kmp_pthreads_bench.c -o kmp_bench -lpthread
gcc -O2 -Wall -pthread rabin_karp_pthreads_bench.c -o rk_bench -lpthread
g++ -O2 -Wall -pthread parallel_bm.cpp -o bm_bench -lpthread   # Boyer-Moore (C++)
# ^ plain `cc src -o out` also works: pthread_compat.h supplies Win32 threads

# <prog> <threads 1|2..1024> <N> <M=5> <reps=3> <verify 0|1>
./kmp_bench 1  10000000 5 3 0     # serial baseline
./parallel-bm 16 10000000 5 3 1   # 16 threads, results verified vs serial
./rk_bench  16 10000000 5 3 1
```

### Full sweep + charts (what produced `results.csv` and the PNGs)

```bash
python run_benchmark.py            # threads 1..1024 × N = 5..10,000,000 for ALL THREE algorithms
python run_benchmark.py --full     # additionally N = 100,000,000 and 1,000,000,000
                                   # (N = 1e9 needs ~1 GB RAM and several minutes)
python run_benchmark.py --threads 2,4,8 --sizes 5,10,100   # custom sweep
python plot_benchmark.py --cores 8 # builds the three PNG charts
```

Repetition count is adaptive (5 for tiny N, 2 for huge N); the best (minimum)
repetition is reported to filter scheduler noise.

## 4. Measured results (this machine: Intel Tiger Lake, 8 logical cores, M = 5)
