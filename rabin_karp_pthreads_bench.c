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

    /* Deterministic pattern and text generation (same seed => identical datasets) */
    char pattern[257];
    srand(123);
    for (int i = 0; i < m; i++) pattern[i] = (char)('A' + rand() % 4);
    pattern[m] = '\0';

    char *text = (char *)malloc((size_t)n + 1);
    if (text == NULL) {
        fprintf(stderr, "Error: cannot allocate %lld bytes for text\n", n);
        return 1;
    }
    srand(42);
    for (long long i = 0; i < n; i++) text[i] = (char)('A' + rand() % 4);
    /* Embed the pattern at fixed intervals so matches exist and grow with N.
     * For tiny N where no full interval fits, embed once at position 0. */
    long long interval = 1000;
    if (n >= (long long)m) memcpy(text, pattern, (size_t)m);
    for (long long i = interval; i + m <= n; i += interval) {
        memcpy(&text[i], pattern, m);
    }
    text[n] = '\0';

    /* Precompute pattern hash and BASE^(m-1) mod HASH_MOD */
    long long pattern_hash = 0, highest_power = 1;
    for (int i = 0; i < m; i++) {
        pattern_hash = (HASH_BASE * pattern_hash + (unsigned char)pattern[i]) % HASH_MOD;
        if (i > 0) highest_power = (highest_power * HASH_BASE) % HASH_MOD;
    }

    /* ---------- Run (serial or parallel) ---------- */
    long long match_count = 0;
    long long *all_matches = NULL;   /* sorted match indices (verification only) */
    double best_time = -1.0;
    int verified = 0;

    if (num_threads_req == 1) {
        /* SERIAL baseline */
        MatchBuf serial;
        serial.buf = NULL; serial.count = 0; serial.capacity = 0;

        for (int r = 0; r < reps; r++) {
            serial.count = 0;
            double t0 = now_ms();
            rk_scan(text, n, m, pattern, pattern_hash, highest_power, 0, n, &serial);
            double t1 = now_ms();
            double dt = t1 - t0;
            if (best_time < 0 || dt < best_time) best_time = dt;
        }
        match_count = serial.count;
        if (verify) {
            all_matches = serial.buf;
        } else {
            free(serial.buf);
        }
    } else {
        /* PARALLEL: partition + create + join + combine inside the timed region */
        long long P = num_threads_req;
        pthread_t *threads = (pthread_t *)malloc((size_t)P * sizeof(pthread_t));
        ThreadData *tds    = (ThreadData *)malloc((size_t)P * sizeof(ThreadData));
        long long *counts  = (long long *)calloc((size_t)P, sizeof(long long));
        if (threads == NULL || tds == NULL || counts == NULL) {
            fprintf(stderr, "Error: thread arrays allocation failed\n");
            free(threads); free(tds); free(counts); free(text);
            return 1;
        }

        for (int r = 0; r < reps; r++) {
            for (long long i = 0; i < P; i++) {
                tds[i].matches.buf = NULL;
                tds[i].matches.count = 0;
                tds[i].matches.capacity = 0;
            }
            match_count = 0;
            double t0 = now_ms();

            /* Partition the window-START space [0, n-m+1) into P contiguous chunks */
            long long W = n - m + 1; if (W < 1) W = 1;
            long long base = W / P, rem = W % P, cur = 0;
            for (long long i = 0; i < P; i++) {
                long long len = base + (i < rem ? 1 : 0);
                tds[i].thread_id     = (int)i;
                tds[i].chunk_start   = cur;
                tds[i].chunk_end     = cur + len;
                tds[i].n             = n;
                tds[i].m             = m;
                tds[i].pattern_hash  = pattern_hash;
                tds[i].highest_power = highest_power;
                tds[i].text          = text;
                tds[i].pattern       = pattern;
                cur += len;
                tds[i].was_created = (pthread_create(&threads[i], NULL, rk_worker, &tds[i]) == 0);
                if (!tds[i].was_created) {
                    fprintf(stderr, "Error: pthread_create failed for thread %lld\n", i);
                }
            }
            for (long long i = 0; i < P; i++) {
                if (tds[i].was_created) {
                    pthread_join(threads[i], NULL);
                }
                counts[i] = tds[i].matches.count;
            }
            /* Combine: chunks are contiguous ascending ranges => concatenation is sorted */
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
        rk_scan(text, n, m, pattern, pattern_hash, highest_power, 0, n, &serial);
        verified = (serial.count == match_count);
        if (verified) {
            for (long long k = 0; k < serial.count; k++) {
                if (serial.buf[k] != all_matches[k]) { verified = 0; break; }
            }
        }
        free(serial.buf);
    }
    if (all_matches) { free(all_matches); all_matches = NULL; }

    printf("rk,%lld,%lld,%d,%.6f,%lld,%d\n",
           num_threads_req, n, m, best_time, match_count, verified);

    free(text);
    return 0;
}
