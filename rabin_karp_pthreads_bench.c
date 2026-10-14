/*
 * Rabin-Karp String Matching - Serial baseline + Multithreaded (POSIX Pthreads) benchmark
 *
 * C/Pthreads port of the C++ baseline in Rabin-Karp-Algo.cpp:
 *  - HASH_BASE = 5, HASH_MOD = 1e9+7, rolling hash with direct comparison
 *    double-check on hash collisions (no spurious matches reported).
 *  - Text is partitioned into contiguous chunks. Thread i scans the windows
 *    whose START index lies inside its own chunk [c0, c1); a window starting
 *    at i needs text[i..i+m-1], so all owned occurrences are found even when
 *    they straddle a chunk boundary. Ownership makes the per-thread result
 *    lists disjoint -> no duplicates, no locks, and the concatenation is
 *    already globally sorted.
 *
 * Usage:
 *   ./rk_bench <num_threads> <text_len_N> [pattern_len_M] [reps] [verify]
 *
 *   num_threads   : 1 = serial baseline (no pthreads), 2,4,8,... up to 1024
 *   text_len_N    : text length N (the question string length)
 *   pattern_len_M : pattern length M (default 5)
 *   reps          : repetitions of the timed region (best/min time reported)
 *   verify        : 1 = compare parallel results against serial (default 0)
 *
 * Output (single line, CSV):
 *   rk,<threads>,<N>,<M>,<time_ms>,<match_count>,<verified>
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pthread_compat.h"   /* POSIX pthreads; Win32-backed fallback for MinGW.org */
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
/* High-resolution timer fallback for Windows (QueryPerformanceCounter) */
static int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    /* double-based math avoids rounding to a negative sub-microsecond nsec */
    double secs = (double)count.QuadPart / (double)freq.QuadPart;
    tp->tv_sec  = (time_t)secs;
    tp->tv_nsec = (long)(((double)secs - (double)(time_t)secs) * 1e9);
    return 0;
}
#endif
#endif

#define HASH_BASE 5LL
#define HASH_MOD 1000000007LL

/* ---------------- Utilities ---------------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* Growing per-thread match buffer */
typedef struct {
    long long *buf;
    long long count;
    long long capacity;
} MatchBuf;

static void buf_push(MatchBuf *b, long long v) {
    if (b->count >= b->capacity) {
        b->capacity = (b->capacity == 0) ? 64 : b->capacity * 2;
        b->buf = (long long *)realloc(b->buf, (size_t)b->capacity * sizeof(long long));
    }
    b->buf[b->count++] = v;
}

