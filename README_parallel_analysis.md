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

All parallel runs across all three algorithms verified **identical** to serial
(same match count and indices).

### Execution time (ms) — latest sweep

| N | serial | P=2 | P=8 | P=16 | P=32 | P=128 | P=1024 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 10,000,000 KMP | 56.83 | — | 17.15 | 15.87 | 17.92 | — | ~64 |
| 10,000,000 RK | 173.7 | — | 46.68 | 45.66 | 51.73 | — | ~139 |
| 10,000,000 BM | 31.57 | 19.68 | 11.42 | **8.71** | 14.37 | 17.49 | 62.77 |
| 1,000,000 BM | 2.407 | 1.876 | 1.362 | — | 1.778 | 6.673 | 60.10 |
| 100,000 BM | 0.226 | 0.321 | 0.698 | — | 1.764 | 8.073 | 60.60 |

### Speedup S(P) = T_serial / T_parallel (efficiency E = S / P) at N = 10M

| Algorithm | P=2 | P=8 | P=16 (best) | P=1024 |
|---|---:|---:|---:|---:|
| KMP | — | 3.31 (E .41) | **3.58** (E .22) | 0.89 (E .001) |
| Rabin-Karp | — | 3.72 (E .47) | **3.80** (E .24) | 1.25 (E .001) |
| Boyer-Moore | 1.60 | 2.76 (E .35) | **3.62** (E .23) | 0.50 (E .0005) |

---

## 5. Discussion Questions (three-algorithm edition)

### 1. How does increasing the number of Pthreads affect execution time?

For a **small fixed workload** the time *increases* with thread count. At
N = 100,000 Boyer-Moore takes 0.226 ms serially but **60.6 ms with 1,024
threads** (≈ 270× slower!), same as KMP/R-K: creating/joining 1,024 Win32
threads costs ~55–60 ms, hundreds of times more than the search itself.

For a **large fixed workload** the time first *decreases* as real cores work in
parallel (BM at N = 10M: 31.6 ms → **8.7 ms** around P = 16), then it
**plateaus and rises again** (at P = 1024 Boyer-Moore is *slower*: 62.8 ms vs
31.6 ms serial). KMP reaches its floor near ≈ 15.9 ms and Rabin-Karp near
≈ 45.7 ms; Boyer-Moore's scan jumps
irregularly, which makes some threads do more work (heuristic shifts are
data-dependent), so BM shows a bumpier curve: 8.7 @16, 9.1 @64, 14.4 @32,
17.5 @128 in one sweep — a classic load-imbalance signature.

So: three regimes — overhead-dominated (small N), beneficial (large N, P ≤
several cores), and oversubscribed / imbalance-limited (P >> cores).

### 2. Does the speedup increase linearly with the number of threads? Explain.

No — speedup is only near-linear up to roughly the number of **physical
cores**. At N = 10M on this 8-logical-core machine:

* KMP: S ≈ 3.3 at P = 8, S ≈ 3.6 at P = 16, then the curve saturates around
  3.6–3.8× for all three algorithms (RK: 3.80, BM: 3.62) and later decays
  (KMP 0.89×, BM 0.50× at P = 1024).
* Reasons:
  * **Amdahl's law** — the inherently serial slice (pattern preprocessing,
    partition setup, joins, merge, and — conceptually — any per-instance
    startup) caps speedup below P.
  * **Physical vs. logical cores** — beyond 4 physical cores the remaining
    logical threads share execution units; adding threads adds no throughput.
  * **Oversubscription** — with P ≫ cores the OS time-slices; the same total
    work now includes context switches and scheduler overhead (Boyer-Moore
    windows jump, and threads repeatedly lose their thermal/boost budget).
  * **Memory bandwidth** — all threads stream the same text; for RK's
    2 modular ops per character the scaling saturates a bit *earlier*
    (S = 3.80 plateau) than for the branchy scans.

### 3. What factors limit the achievable speedup?

* **Serial fraction** (Amdahl): preprocessing (LPS / hash / BM tables — BM's
  good-suffix build is the most expensive of the three), partition/arg setup,
  joins and the final merge.
* **Thread-management overhead**: `pthread_create`/`pthread_join` +
  per-thread match buffers — visible as the ~55–60 ms floor at P = 1024 that
  all three algorithms hit regardless of N.
* **Memory bandwidth and cache**: text dominates; the DRAM bandwidth fills up
  quickly with every thread streaming the same text array.
* **Load imbalance**, strongest for **Boyer-Moore**: heuristic shifts are
  data-dependent, so chunk finishing times vary (P = 16 vs. 32 inversion in
  the BM table above). KMP/RK character-by-character scans balance perfectly.
* **Physical vs. logical cores**, CPU boost/thermal budgets, and Windows
  scheduler behavior at large P.
* **False sharing** is deliberately avoided (thread-local buffers, read-only
  shared inputs); without that design choice it would appear too.

### 4. Why can efficiency decrease as the number of threads increases?

Efficiency is E = S / P — the fraction of each added thread's capability that
becomes useful throughput. As P grows:

* per-thread work shrinks (N/P characters each) while **fixed overhead stays
