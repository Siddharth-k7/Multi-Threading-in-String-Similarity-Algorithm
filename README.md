# Multithreaded Design for String Matching Algorithms

**Project:** Multithreaded Design for String Matching Algorithms  
**My contribution:** Sequential KMP and Multithreaded KMP using POSIX Pthreads.

---

## 1. Multithreaded KMP Design

- **Preprocessing:** Build the LPS array once.
- **Partitioning:** Divide the text into contiguous parts.
- **Thread creation:** Create threads using `pthread_create()`.
- **Parallel KMP searching:** Perform KMP string matching concurrently across threads.
- **Joining threads:** Synchronize and join threads using `pthread_join()`.
- **Combining results:** Combine thread results and compare them with sequential KMP.

---

## 2. Boundary Handling

- Extend each search region by up to `m - 1` extra characters.
- Record matches only when their starting positions belong to the thread's original partition.
- Prevent duplicate matches in overlapping regions.

---

## 3. Thread Safety

- Share the text, pattern, and LPS array for reading.
- Give each thread its own match-result storage.
- Combine results after all threads finish.

---

## 4. Correctness Testing

- **Text:** `ABABDABABCABAB`
- **Pattern:** `ABABC`
- **LPS array:** `[0, 0, 1, 2, 0]`
- **Sequential match index:** `5`
- **Multithreaded match index:** `5`

### Compilation and Execution Commands

```bash
gcc -Wall -Wextra kmp_pthreads_proto.c -o kmp_pthreads_proto -pthread
./kmp_pthreads_proto
```

---

## 5. Sequential Benchmark Results

| Text Size | Matches Found | Execution Time (ms) |
|---:|---:|---:|
| 100,000 | 3 | 0.339100 |
| 1,000,000 | 43 | 3.623600 |
| 10,000,000 | 438 | 32.651000 |

*These are sequential benchmark results only. A separate multithreaded benchmark is required to measure speedup and scalability.*

---

## 6. Conclusion

This project implements sequential KMP and its multithreaded extension using POSIX Pthreads. Boundary handling extends search regions by up to $m - 1$ characters while assigning match ownership to the original partition, successfully preventing duplicate matches in overlapping regions. Correctness testing confirmed identical match indices between sequential and multithreaded executions, and sequential benchmarking established baseline performance.

---

## 7. Benchmark Suite

- `run_benchmark.py` drives text-size scaling runs and writes `results.csv`.
- `run_granularity.py` sweeps chunk sizes into `granularity.csv`.
- `run_throughput.py` sweeps thread counts into `throughput.csv`.
- `plot_benchmark.py`, `plot_granularity.py`, `plot_throughput.py` regenerate all `fig_*.png` charts.

---

## 8. Parallel Algorithms Covered

- Serial baselines: `kmp.c`, `boyer_moore.cpp`, `Rabin-Karp-Algo.cpp`.
- Pthread benchmarks: `kmp_pthreads_bench.c`, `rabin_karp_pthreads_bench.c`, `parallel_bm.cpp`.
- Prototype and demos: `kmp_pthreads_proto.c`, `boundary_demo.c`, `pthread_demo.c`.
- Full analysis with figures: `README_parallel_analysis.md`.
