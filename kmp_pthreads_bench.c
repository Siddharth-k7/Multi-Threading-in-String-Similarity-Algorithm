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
    while (i < end && i < n) {
        if (pattern[j] == text[i]) {
            i++;
            j++;
        }
        if (j == m) {
            long long match_start = i - j;
            out[count++] = match_start;
            j = lps[j - 1];
        } else if (i < end && i < n && pattern[j] != text[i]) {
            if (j != 0) {
                j = lps[j - 1];
            } else {
                i++;
            }
        }
    }
    return count;
}

/* Growing per-thread match buffer */
typedef struct {
    long long *buf;
    int count;
    int capacity;
} MatchBuf;

static void buf_push(MatchBuf *b, long long v) {
    if (b->count >= b->capacity) {
        b->capacity = (b->capacity == 0) ? 64 : b->capacity * 2;
        b->buf = (long long *)realloc(b->buf, (size_t)b->capacity * sizeof(long long));
    }
    b->buf[b->count++] = v;
}

/* ---------------- Serial baseline (threads = 1) ---------------- */

/* ---------------- Parallel worker ---------------- */

typedef struct {
    int thread_id;
    long long chunk_start;        /* start of ORIGINAL (non-overlapping) chunk, exclusive end given below */
    long long chunk_end;          /* exclusive end of original chunk; matches must start < this */
    long long search_end;         /* exclusive end of EXTENDED search region */
    long long n;                  /* total text length */
    const char *text;
    const char *pattern;
    int m;
    const int *lps;
    MatchBuf matches;
    int was_created;              /* set to 1 if pthread_create succeeded */
} ThreadData;

static void *kmp_worker(void *arg) {
    ThreadData *d = (ThreadData *)arg;
    d->matches.buf = NULL;
    d->matches.count = 0;
    d->matches.capacity = 0;

    /* Local scratch for matches in this region before ownership filtering */
    long long scratch_cap = 64;
    long long *scratch = (long long *)malloc((size_t)scratch_cap * sizeof(long long));
    if (scratch == NULL) {
        fprintf(stderr, "Error: allocation failed in thread %d\n", d->thread_id);
        return NULL;
    }

    /* Scan extended region only if it can contain a full pattern */
    if (d->search_end - d->chunk_start >= d->m) {
        long long search_end_min = d->search_end < d->n ? d->search_end : d->n;
        /* scan needs to look at most m-1 chars past the exclusive scan end */
        long long scan_end = search_end_min + d->m - 1;
        if (scan_end > d->n) scan_end = d->n;

        /* Over-allocate scratch generously: expected matches are few but be safe */
        long long expected = (scan_end - d->chunk_start) / d->m + 2;
        if (expected > scratch_cap) {
            scratch = (long long *)realloc(scratch, (size_t)expected * sizeof(long long));
            scratch_cap = expected;
            if (scratch == NULL) { fprintf(stderr, "Error: realloc failed in thread %d\n", d->thread_id); return NULL; }
        }

        int cnt = kmp_scan(d->text, d->chunk_start, scan_end, d->n,
                           d->pattern, d->m, d->lps, scratch);
