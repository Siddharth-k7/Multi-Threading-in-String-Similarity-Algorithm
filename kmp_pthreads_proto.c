#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#define NUM_THREADS 4

// Thread argument structure
typedef struct {
    int thread_id;
    int original_start;             // Start index of thread's non-overlapping chunk
    int original_end;               // End index of thread's non-overlapping chunk (inclusive)
    int search_start;               // Start index of extended search region
    int search_end;                 // End index of extended search region (inclusive)
    const char *text;               // Pointer to full text (shared, read-only)
    const char *pattern;            // Pointer to pattern (shared, read-only)
    int pattern_len;                // Length of pattern (m)
    const int *lps;                 // Precomputed LPS array (shared, read-only)
    int *local_matches;             // Dynamically allocated thread-local match storage
    int match_count;                // Number of matches found by this thread
    int match_capacity;             // Current capacity of local_matches array
} ThreadData;

// Precomputes the Longest Proper Prefix which is also Suffix (LPS) array.
// lps[i] stores the length of the longest matching prefix and suffix for pattern[0..i].
void buildLPS(const char *pattern, int m, int *lps) {
    int len = 0;  // Length of the previous longest prefix-suffix
    lps[0] = 0;   // Base case

    int i = 1;
    while (i < m) {
        if (pattern[i] == pattern[len]) {
            // Characters match: extend the prefix-suffix length
            len++;
            lps[i] = len;
            i++;
        } else {
            // Characters mismatch
            if (len != 0) {
                // Fall back to the previous longest prefix-suffix without advancing i
                len = lps[len - 1];
            } else {
                // No matching prefix-suffix
                lps[i] = 0;
                i++;
            }
        }
    }
}

// Sequential KMP baseline implementation
// Stores match starting indices in a dynamically allocated array (*matches).
// Returns the number of matches found, or -1 on allocation failure.
int KMPsearch(const char *text, int n, const char *pattern, int m, const int *lps, int **matches, int *match_count) {
    *match_count = 0;
    int capacity = 16;
    *matches = (int *)malloc(capacity * sizeof(int));
    if (*matches == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for sequential matches.\n");
        return -1;
    }

    int i = 0;  // Index for text
    int j = 0;  // Index for pattern

    while (i < n) {
        if (pattern[j] == text[i]) {
            i++;
            j++;
        }

        if (j == m) {
            // Full pattern match found at index (i - j)
            if (*match_count >= capacity) {
                int new_cap = capacity * 2;
                int *temp = (int *)realloc(*matches, new_cap * sizeof(int));
                if (temp == NULL) {
                    fprintf(stderr, "Error: Memory reallocation failed for sequential matches.\n");
                    return *match_count;
