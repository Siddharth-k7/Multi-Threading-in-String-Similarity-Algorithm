/*
 * Boyer-Moore String Matching - Serial baseline + Multithreaded (POSIX Pthreads benchmark
 *
 * Parallel counterpart of the serial library in boyer_moore.cpp:
 *  - Same bad-character + good-suffix heuristics (const char* port of the
 *    string_view library so it builds on toolchains without <string_view>).
 *  - Text partitioned over window-START positions into P contiguous chunks;
 *    a window starting at s needs text[s..s+m-1], so every occurrence whose
 *    START index lies inside the thread's chunk is found even when the
 *    pattern textually crosses a chunk boundary.
 *  - Ownership makes per-thread result lists disjoint -> no duplicates, no
 *    locks, and the concatenation is already globally sorted.
 *
 * Usage:
 *   ./bm_bench <num_threads> <text_len_N> [pattern_len_M] [reps] [verify]
 *
 *   num_threads   : 1 = serial baseline (no threads), 2,4,8,... up to 1024
 *   text_len_N    : text length N (the question string length)
 *   pattern_len_M : pattern length M (default 5)
 *   reps          : repetitions of the timed region (best/min time reported)
 *   verify        : 1 = compare parallel results against serial (default 0)
 *
 * Output (single line, CSV):
 *   bm,<threads>,<N>,<M>,<time_ms>,<match_count>,<verified>
 */
#define _POSIX_C_SOURCE 199309L
#define __USE_MINGW_ANSI_STDIO 1   /* %lld support on MinGW C++ mode (old msvcrt printf) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pthread_compat.h"   /* POSIX pthreads; Win32-backed fallback for MinGW.org */

#ifdef _WIN32
#include <windows.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
static int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    double secs = (double)count.QuadPart / (double)freq.QuadPart;
    tp->tv_sec  = (time_t)secs;
    tp->tv_nsec = (long)(((double)secs - (double)(time_t)secs) * 1e9);
    return 0;
}
#endif
#endif

#define BM_ALPHABET 256

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* ---------------- Heuristic tables (port of boyer_moore.cpp) ---------------- */

static void build_bad_char(const char *pattern, int m, int *badChar) {
    for (int i = 0; i < BM_ALPHABET; i++) badChar[i] = -1;
    for (int i = 0; i < m; i++) {
        badChar[(unsigned char)pattern[i]] = i;
    }
}

static void build_good_suffix(const char *pattern, int m, int *good) {
    int *suffixes = (int *)malloc((size_t)m * sizeof(int));
    int f = 0, g = m - 1;

    suffixes[m - 1] = m;
    for (int i = m - 2; i >= 0; i--) {
        if (i > g && suffixes[i + m - 1 - f] < i - g) {
            suffixes[i] = suffixes[i + m - 1 - f];
        } else {
            if (i < g) g = i;
            f = i;
            while (g >= 0 && pattern[g] == pattern[g + m - 1 - f]) {
                g--;
            }
            suffixes[i] = f - g;
        }
    }
