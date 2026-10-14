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

/* ---------------- Rabin-Karp scan (shared by serial & parallel) ---------------- */

/*
 * Scans window start positions i in [start, scan_stop) with i <= n - m.
 * Every owned occurrence is reported: out[count] = start index of match.
 * Returns the number of matches recorded.
 */
static long long rk_scan(const char *text, long long n, int m, const char *pattern,
                         long long pattern_hash, long long highest_power,
                         long long start, long long scan_stop,
                         MatchBuf *out) {
    long long count = 0;
    long long last_i = n - (long long)m;          /* last valid window start */
    if (scan_stop - 1 > last_i) scan_stop = last_i + 1;
    if (start >= scan_stop) return 0;             /* nothing to do */

    /* Initial window hash for text[start..start+m-1] */
    long long window_hash = 0;
    for (int j = 0; j < m; j++) {
        window_hash = (HASH_BASE * window_hash + (unsigned char)text[start + j]) % HASH_MOD;
    }

    for (long long i = start; i < scan_stop; i++) {
        if (window_hash == pattern_hash) {
            /* Double-check to eliminate spurious hash collisions */
            if (memcmp(&text[i], pattern, (size_t)m) == 0) {
                buf_push(out, i);
                count++;
            }
        }
        if (i < scan_stop - 1) {
            /* Roll the window: remove text[i], append text[i+m] */
            long long lead = ((unsigned char)text[i]) * highest_power % HASH_MOD;
            window_hash = (window_hash - lead + HASH_MOD) % HASH_MOD;
            window_hash = (window_hash * HASH_BASE + (unsigned char)text[i + m]) % HASH_MOD;
        }
    }
    return count;
}

/* ---------------- Parallel worker ---------------- */

typedef struct {
    int thread_id;
    long long chunk_start;     /* first window START position owned by this thread */
    long long chunk_end;       /* exclusive end of owned window START positions */
    long long n;
    int m;
    long long pattern_hash;
    long long highest_power;
    const char *text;
    const char *pattern;
    MatchBuf matches;
    int was_created;
} ThreadData;

static void *rk_worker(void *arg) {
    ThreadData *d = (ThreadData *)arg;
    d->matches.buf = NULL;
    d->matches.count = 0;
    d->matches.capacity = 0;
    rk_scan(d->text, d->n, d->m, d->pattern, d->pattern_hash, d->highest_power,
            d->chunk_start, d->chunk_end, &d->matches);
    return NULL;
}

int main(int argc, char **argv) {
    long long num_threads_req = argc > 1 ? atoll(argv[1]) : 4;
    long long n               = argc > 2 ? atoll(argv[2]) : 1000000;
    int m                     = argc > 3 ? atoi(argv[3]) : 5;
    int reps                  = argc > 4 ? atoi(argv[4]) : 3;
    int verify                = argc > 5 ? atoi(argv[5]) : 0;

    if (num_threads_req < 1 || n < 1 || m < 1 || m > 256 || reps < 1) {
        fprintf(stderr, "Usage: %s <num_threads> <N> [M] [reps] [verify]\n", argv[0]);
        return 1;
    }
