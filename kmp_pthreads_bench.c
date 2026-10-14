/*
 * KMP String Matching - Serial baseline + Multithreaded (POSIX Pthreads) benchmark
 *
 * Text is partitioned into contiguous chunks; each thread's search region is
 * extended by up to (m - 1) characters so matches that cross a chunk boundary
 * are still found. A thread only RECORDS a match whose start index falls inside
 * its own original chunk (ownership rule), which prevents duplicates.
 *
 * Usage:
 *   ./kmp_bench <num_threads> <text_len_N> [pattern_len_M] [reps] [verify]
 *
 *   num_threads : 1 = serial baseline (no pthreads used), 2,4,8,... up to 1024
 *   text_len_N  : text length N (question string length), up to 1 000 000 000
 *   pattern_len_M : pattern length M (default 5)
 *   reps        : number of repetitions of the timed region (min reported)
 *   verify      : 1 = compare parallel results against serial (default 0)
 *
 * Output (single line, CSV):
 *   kmp,<threads>,<N>,<M>,<time_ms>,<match_count>,<verified>
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

/* ---------------- Utilities ---------------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* Precomputes the Longest Proper Prefix which is also Suffix (LPS) array. */
static void buildLPS(const char *pattern, int m, int *lps) {
    int len = 0;
    lps[0] = 0;
    int i = 1;
    while (i < m) {
        if (pattern[i] == pattern[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
}

/* KMP scan over [start, end) of the text (end is exclusive).
 * Records match start-index into out[] (caller guarantees capacity).
 * Returns the number of matches recorded. */
static int kmp_scan(const char *text, long long start, long long end, int n,
                    const char *pattern, int m, const int *lps,
                    long long *out) {
    int count = 0;
    int j = 0;
    long long i = start;
