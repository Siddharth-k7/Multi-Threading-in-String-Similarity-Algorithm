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
                }
                *matches = temp;
                capacity = new_cap;
            }
            (*matches)[(*match_count)++] = i - j;
            // Use LPS to find the next possible match
            j = lps[j - 1];
        } else if (i < n && pattern[j] != text[i]) {
            // Mismatch: shift pattern using LPS without backtracking text pointer i
            if (j != 0) {
                j = lps[j - 1];
            } else {
                i++;
            }
        }
    }

    return *match_count;
}

// Compatibility wrapper for sequential KMP
int sequentialKMP(const char *text, int n, const char *pattern, int m, const int *lps, int **matches, int *match_count) {
    return KMPsearch(text, n, pattern, m, lps, matches, match_count);
}

// Worker thread function: performs KMP search on the thread's assigned search region
void *threadKMPsearch(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    data->match_count = 0;

    int m = data->pattern_len;
    int i = data->search_start;  // Text index starts at the beginning of the search region
    int j = 0;                   // Pattern index

    while (i <= data->search_end) {
        if (data->pattern[j] == data->text[i]) {
            i++;
            j++;
        }

        if (j == m) {
            int match_index = i - j;  // Global starting index in full text

            // Match Ownership Rule:
            // Because adjacent search regions overlap by up to (m - 1) characters,
            // two threads could both see the same occurrence of the pattern.
            // To prevent duplicate recordings, a thread only records a match
            // if its starting index falls inside this thread's original partition.
            if (match_index >= data->original_start && match_index <= data->original_end) {
                if (data->match_count >= data->match_capacity) {
                    int new_cap = (data->match_capacity == 0) ? 16 : (data->match_capacity * 2);
                    int *temp = (int *)realloc(data->local_matches, new_cap * sizeof(int));
                    if (temp != NULL) {
                        data->local_matches = temp;
                        data->match_capacity = new_cap;
                        data->local_matches[data->match_count++] = match_index;
                    } else {
                        fprintf(stderr, "Error: Memory reallocation failed in thread %d.\n", data->thread_id);
                    }
                } else {
                    data->local_matches[data->match_count++] = match_index;
                }
            }

            j = data->lps[j - 1];
        } else if (i <= data->search_end && data->pattern[j] != data->text[i]) {
            if (j != 0) {
                j = data->lps[j - 1];
            } else {
                i++;
            }
        }
    }

    return NULL;
}

// Runs both multithreaded and sequential KMP on an input and verifies correctness
void run_kmp_test(const char *test_title, const char *text, const char *pattern) {
    if (text == NULL || pattern == NULL) {
        fprintf(stderr, "Error: Text or pattern is NULL.\n");
        return;
    }

    int n = (int)strlen(text);
    int m = (int)strlen(pattern);

    printf("=================================================================\n");
    printf("Test: %s\n", test_title);
    printf("Text    : \"%s\" (Length: %d)\n", text, n);
    printf("Pattern : \"%s\" (Length: %d)\n", pattern, m);

    // If pattern is empty, nothing to search
    if (m == 0) {
        printf("Pattern is empty. Skipping.\n\n");
        return;
    }

    // Build LPS array once for the pattern
    int *lps = (int *)malloc((size_t)m * sizeof(int));
    if (lps == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for LPS array.\n");
        return;
    }
    buildLPS(pattern, m, lps);

    printf("LPS     : [ ");
    for (int k = 0; k < m; k++) {
        printf("%d ", lps[k]);
    }
    printf("]\n\n");

    // Adjust active threads if text is shorter than NUM_THREADS
    int active_threads = NUM_THREADS;
    if (n > 0 && n < active_threads) {
        active_threads = n;
    }
    if (n == 0) {
        active_threads = 1;
    }

    // Partition text among worker threads into contiguous, non-overlapping chunks
    int base_chunk_size = (active_threads > 0) ? (n / active_threads) : 0;
    int remainder = (active_threads > 0) ? (n % active_threads) : 0;

    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];

    int current_start = 0;

    printf("--- Thread Partitions and Overlaps ---\n");
    for (int i = 0; i < active_threads; i++) {
        int orig_len = base_chunk_size + (i < remainder ? 1 : 0);
        int orig_start = current_start;
        int orig_end = (orig_len > 0) ? (orig_start + orig_len - 1) : (orig_start - 1);
        current_start += orig_len;

        // Search region: extend by up to (m - 1) extra characters for non-final threads
        int search_start = orig_start;
        int search_end = orig_end;
        if (orig_len > 0 && i < active_threads - 1) {
            search_end = orig_end + (m - 1);
            if (search_end >= n) {
                search_end = (n > 0) ? (n - 1) : 0;
            }
        }

        thread_data[i].thread_id = i;
        thread_data[i].original_start = orig_start;
        thread_data[i].original_end = orig_end;
        thread_data[i].search_start = search_start;
        thread_data[i].search_end = search_end;
        thread_data[i].text = text;
        thread_data[i].pattern = pattern;
        thread_data[i].pattern_len = m;
        thread_data[i].lps = lps;
        thread_data[i].match_count = 0;
        thread_data[i].match_capacity = 16;
        thread_data[i].local_matches = (int *)malloc((size_t)thread_data[i].match_capacity * sizeof(int));
        if (thread_data[i].local_matches == NULL) {
