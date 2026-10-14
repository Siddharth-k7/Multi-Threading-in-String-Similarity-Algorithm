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

    for (int i = 0; i < m; i++) good[i] = m;

    int j = 0;
    for (int i = m - 1; i >= 0; i--) {
        if (suffixes[i] == i + 1) {
            for (; j < m - 1 - i; j++) {
                if (good[j] == m) good[j] = m - 1 - i;
            }
        }
    }
    for (int i = 0; i <= m - 2; i++) {
        good[m - 1 - suffixes[i]] = m - 1 - i;
    }
    free(suffixes);
}

/* ---------------- Match buffer ---------------- */

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

/* ---------------- BM scan over window-START range [start, stop) ----------------
 * start: first window START position to examine (owned chunk start)
 * stop : exclusive end of owned window START positions
 * Every occurrence whose start index is in [start, stop) is reported.   */
static long long bm_scan(const char *text, long long n, int m, const char *pattern,
                         const int *badChar, const int *good,
                         MatchBuf *out, long long start, long long stop) {
    long long count = 0;
    if (m < 1) return 0;

    long long hi = stop - 1;
    if (hi > n - m) hi = n - m;              /* window must fit inside text */
    long long s = start;
    if (s > hi) return 0;

    while (s <= hi) {
        int j = m - 1;
        /* compare right-to-left (Boyer-Moore signature) */
        while (j >= 0 && pattern[j] == text[s + j]) j--;

        if (j < 0) {
            /* full match */
            buf_push(out, s);
            count++;
            s += good[0];
        } else {
            int bad_shift = j - badChar[(unsigned char)text[s + j]];
            int shift = (bad_shift > good[j]) ? bad_shift : good[j];
            if (shift < 1) shift = 1;        /* safety: advance at least one */
            s += shift;
        }
    }
    return count;
}

/* ---------------- Parallel worker ---------------- */

typedef struct {
    int thread_id;
    long long chunk_start;   /* first window START position owned by this thread */
    long long chunk_end;     /* exclusive end of owned window START positions */
    long long n;
    int m;
    const char *text;
    const char *pattern;
    const int *badChar;
    const int *good;
    MatchBuf matches;
    int was_created;
} ThreadData;

static void *bm_worker(void *arg) {
    ThreadData *d = (ThreadData *)arg;
    d->matches.buf = NULL;
    d->matches.count = 0;
    d->matches.capacity = 0;
    bm_scan(d->text, d->n, d->m, d->pattern, d->badChar, d->good,
            &d->matches, d->chunk_start, d->chunk_end);
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

    /* Deterministic pattern and text generation (same seeds as the KMP and
     * Rabin-Karp benchmarks => identical datasets across all three). */
    char pattern[257];
    srand(123);
    for (int i = 0; i < m; i++) pattern[i] = (char)('A' + rand() % 4);
    pattern[m] = '\0';

    char *text = (char *)malloc((size_t)n + 1);
    if (text == NULL) {
        fprintf(stderr, "Error: cannot allocate %ld bytes for text\n", (long)n);
        return 1;
    }
    srand(42);
    for (long long i = 0; i < n; i++) text[i] = (char)('A' + rand() % 4);
    long long interval = 1000;
    if (n >= (long long)m) memcpy(text, pattern, (size_t)m);
    for (long long i = interval; i + m <= n; i += interval) {
        memcpy(&text[i], pattern, m);
    }
    text[n] = '\0';

    /* Precompute BM tables once (outside the timed search region) */
    int *badChar = (int *)malloc(BM_ALPHABET * sizeof(int));
    int *good    = (int *)malloc((size_t)m * sizeof(int));
    build_bad_char(pattern, m, badChar);
    build_good_suffix(pattern, m, good);

    /* ---------- Run (serial or parallel) ---------- */
    long long match_count = 0;
    long long *all_matches = NULL;
    double best_time = -1.0;
    int verified = 0;

    if (num_threads_req == 1) {
        /* SERIAL baseline */
        MatchBuf serial;
        serial.buf = NULL; serial.count = 0; serial.capacity = 0;
        for (int r = 0; r < reps; r++) {
            serial.count = 0;
            double t0 = now_ms();
            bm_scan(text, n, m, pattern, badChar, good, &serial, 0, n);
            double t1 = now_ms();
            double dt = t1 - t0;
            if (best_time < 0 || dt < best_time) best_time = dt;
        }
        match_count = serial.count;
        if (verify) all_matches = serial.buf; else free(serial.buf);
    } else {
        /* PARALLEL: partition + create + join + combine inside the timed region */
        long long P = num_threads_req;
        pthread_t *threads = (pthread_t *)malloc((size_t)P * sizeof(pthread_t));
        ThreadData *tds    = (ThreadData *)malloc((size_t)P * sizeof(ThreadData));
        long long *counts  = (long long *)calloc((size_t)P, sizeof(long long));
        if (threads == NULL || tds == NULL || counts == NULL) {
            fprintf(stderr, "Error: thread arrays allocation failed\n");
            free(threads); free(tds); free(counts); free(text);
            free(badChar); free(good);
            return 1;
        }

        for (int r = 0; r < reps; r++) {
            for (long long i = 0; i < P; i++) {
                tds[i].matches.buf = NULL;
                tds[i].matches.count = 0;
                tds[i].matches.capacity = 0;
                tds[i].was_created = 0;
            }
            match_count = 0;
            double t0 = now_ms();

            /* Partition the window-START space [0, n-m+1) into P chunks */
            long long W = n - m + 1; if (W < 1) W = 1;
            long long base = W / P, rem = W % P, cur = 0;
            for (long long i = 0; i < P; i++) {
                long long len = base + (i < rem ? 1 : 0);
                tds[i].thread_id   = (int)i;
                tds[i].chunk_start = cur;
                tds[i].chunk_end   = cur + len;
                tds[i].n           = n;
                tds[i].m           = m;
                tds[i].text        = text;
                tds[i].pattern     = pattern;
                tds[i].badChar     = badChar;
                tds[i].good        = good;
                cur += len;
                tds[i].was_created = (pthread_create(&threads[i], NULL, bm_worker, &tds[i]) == 0);
                if (!tds[i].was_created) {
                    fprintf(stderr, "Error: pthread_create failed for thread %ld\n", (long)i);
                }
            }
            for (long long i = 0; i < P; i++) {
                if (tds[i].was_created) pthread_join(threads[i], NULL);
                counts[i] = tds[i].matches.count;
            }
            long long total = 0;
            for (long long i = 0; i < P; i++) total += counts[i];
            match_count = total;

            /* Collect combined indices on the final repetition for verification */
            if (r == reps - 1 && verify) {
                all_matches = (long long *)malloc((size_t)(total > 0 ? total : 1) * sizeof(long long));
                if (all_matches != NULL) {
                    long long offs = 0;
                    for (long long i = 0; i < P; i++) {
                        for (long long k = 0; k < tds[i].matches.count; k++) {
                            all_matches[offs++] = tds[i].matches.buf[k];
                        }
                    }
                }
            }

            double t1 = now_ms();
            double dt = t1 - t0;
            if (best_time < 0 || dt < best_time) best_time = dt;

            for (long long i = 0; i < P; i++) {
                free(tds[i].matches.buf);
                tds[i].matches.buf = NULL;
            }
        }
        free(threads); free(tds); free(counts);
    }

    /* ---------- Verification against serial ---------- */
    if (verify && num_threads_req != 1 && all_matches != NULL) {
        MatchBuf serial;
        serial.buf = NULL; serial.count = 0; serial.capacity = 0;
        bm_scan(text, n, m, pattern, badChar, good, &serial, 0, n);
        verified = (serial.count == match_count);
        if (verified) {
            for (long long k = 0; k < serial.count; k++) {
                if (serial.buf[k] != all_matches[k]) { verified = 0; break; }
            }
        }
        free(serial.buf);
    }
    if (all_matches) { free(all_matches); all_matches = NULL; }

    printf("bm,%ld,%ld,%d,%.6f,%ld,%d\n",
           (long)num_threads_req, (long)n, m, best_time, (long)match_count, verified);

    free(text); free(badChar); free(good);
    return 0;
}
