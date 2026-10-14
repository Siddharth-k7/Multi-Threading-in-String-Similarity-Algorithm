#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
// Fallback high-resolution timer for Windows systems
static inline int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    tp->tv_sec = count.QuadPart / freq.QuadPart;
    tp->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
    return 0;
}
#endif
#endif

// Precomputes the Longest Proper Prefix which is also Suffix (LPS) array.
// lps[i] stores the length of the longest matching prefix and suffix for pattern[0..i].
void buildLPS(const char *pattern, int m, int *lps) {
    int len = 0;  // Length of the previous longest prefix-suffix
    lps[0] = 0;   // Base case: a single character has no proper prefix

    int i = 1;
    while (i < m) {
        if (pattern[i] == pattern[len]) {
            // Characters match: extend the current prefix-suffix
            len++;
            lps[i] = len;
            i++;
        } else {
            // Characters mismatch
            if (len != 0) {
                // Fall back to the previous known prefix-suffix without advancing i
                len = lps[len - 1];
            } else {
                // No prefix-suffix matches for this position
                lps[i] = 0;
                i++;
            }
        }
    }
}

// Searches for pattern in text using the KMP algorithm and measures search time.
// Returns the total number of matches found.
int KMPsearch(const char *text, int n, const char *pattern, int m, const int *lps, double *exec_time_ms) {
    int match_count = 0;
    int i = 0;  // Index for text
    int j = 0;  // Index for pattern

    struct timespec start_time, end_time;

    // Measure only the pure search operation
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (i < n) {
        if (pattern[j] == text[i]) {
            i++;
            j++;
        }

        if (j == m) {
            // Full pattern matched
            match_count++;
            // Use LPS to find the next possible match (handles overlapping occurrences)
            j = lps[j - 1];
        } else if (i < n && pattern[j] != text[i]) {
            // Mismatch after j matches
            if (j != 0) {
                // Shift pattern using LPS without moving text pointer i
                j = lps[j - 1];
            } else {
                // No characters matched yet, move to the next text character
                i++;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    *exec_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                    (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    return match_count;
}

// Generates pseudo-random text ('A'-'D') and embeds the pattern at fixed intervals.
// Uses a fixed seed (42) to ensure identical datasets across runs.
void generate_text(char *text, int size, const char *pattern, int pattern_len, int interval) {
    srand(42);

    for (int i = 0; i < size; i++) {
        text[i] = 'A' + (rand() % 4);
    }

    // Embed the pattern at predictable intervals
    for (int i = interval; i + pattern_len <= size; i += interval) {
        memcpy(&text[i], pattern, pattern_len);
    }

    text[size] = '\0';
}

int main(void) {
    const char *pattern = "ABABCABAB";
    int m = strlen(pattern);

    // Test dataset sizes: 100K, 1M, 10M characters
    int test_sizes[] = {100000, 1000000, 10000000};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    int pattern_interval = 25000;

    // Allocate and precompute LPS array
    int *lps = (int *)malloc(m * sizeof(int));
    if (lps == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for LPS array.\n");
        return 1;
    }
    buildLPS(pattern, m, lps);

    printf("Sequential KMP String Matching Baseline\n");
    printf("Pattern   : %s (Length: %d)\n", pattern, m);
    printf("LPS Array : [ ");
    for (int k = 0; k < m; k++) {
        printf("%d ", lps[k]);
    }
    printf("]\n\n");

    printf("%-15s %-15s %-20s\n", "Text Size", "Matches Found", "Execution Time (ms)");
    printf("%-15s %-15s %-20s\n", "---------", "-------------", "-------------------");

    for (int t = 0; t < num_tests; t++) {
        int n = test_sizes[t];

        char *text = (char *)malloc((size_t)(n + 1) * sizeof(char));
        if (text == NULL) {
            fprintf(stderr, "Error: Memory allocation failed for text size %d\n", n);
            continue;
        }

        generate_text(text, n, pattern, m, pattern_interval);

        double exec_time_ms = 0.0;
        int matches = KMPsearch(text, n, pattern, m, lps, &exec_time_ms);

        printf("%-15d %-15d %-20.6f\n", n, matches, exec_time_ms);

        free(text);
    }

    free(lps);
    return 0;
}
