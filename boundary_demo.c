#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define NUM_THREADS 2

// Thread data structure to pass search parameters to each worker thread
typedef struct {
    int thread_id;
    int search_start;     // Starting index in text
    int search_end;       // Ending index in text (inclusive)
    const char *text;
    const char *pattern;
    int pattern_len;
    int matches_found;
} ThreadData;

// Simple substring search worker (used for demonstration purposes)
void *search_worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    data->matches_found = 0;

    int region_len = data->search_end - data->search_start + 1;

    // Search strictly within this thread's assigned region
    for (int i = data->search_start; i <= data->search_end - data->pattern_len + 1; i++) {
        if (strncmp(&data->text[i], data->pattern, data->pattern_len) == 0) {
            printf("  [Thread %d] Found match starting at index: %d\n",
                   data->thread_id, i);
            data->matches_found++;
        }
    }

    if (data->matches_found == 0) {
        printf("  [Thread %d] No match found in region [%d..%d] (length %d: \"%.*s\")\n",
               data->thread_id, data->search_start, data->search_end,
               region_len, region_len, &data->text[data->search_start]);
    }

    return NULL;
}

int main(void) {
    // Demonstration text and pattern:
    // Text: "XXHELLOYY" (Length 9)
    // Pattern: "HELLO" (Length 5)
    // With 2 threads:
    // Thread 0 gets indices 0..4 ("XXHEL")
    // Thread 1 gets indices 5..8 ("LOYY")
    // "HELLO" spans indices 2..6, crossing the boundary between Thread 0 and Thread 1.
    const char *text = "XXHELLOYY";
    const char *pattern = "HELLO";

    int n = strlen(text);
    int m = strlen(pattern);

    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];

    printf("Demonstrating the Boundary Problem in Parallel Search\n");
    printf("Text    : \"%s\" (Length: %d)\n", text, n);
    printf("Pattern : \"%s\" (Length: %d, Overlap needed: m - 1 = %d)\n\n", pattern, m, m - 1);

    // Calculate base chunk boundaries
    int base_chunk_size = n / NUM_THREADS;
    int remainder = n % NUM_THREADS;

    int base_start[NUM_THREADS];
    int base_end[NUM_THREADS];
    int current_start = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        int chunk_len = base_chunk_size + (i < remainder ? 1 : 0);
        base_start[i] = current_start;
        base_end[i] = current_start + chunk_len - 1;
        current_start += chunk_len;
    }

    // --- PART 1: Searching WITHOUT Overlap (Naive Partitioning) ---
    printf("Part 1: Searching WITHOUT overlap (naive chunks)\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        int len = base_end[i] - base_start[i] + 1;
        printf("Thread %d chunk: [%d..%d] -> \"%.*s\"\n",
               i, base_start[i], base_end[i], len, &text[base_start[i]]);
    }
    printf("\nRunning threads without overlap...\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].search_start = base_start[i];
        thread_data[i].search_end = base_end[i];
        thread_data[i].text = text;
        thread_data[i].pattern = pattern;
        thread_data[i].pattern_len = m;

        if (pthread_create(&threads[i], NULL, search_worker, &thread_data[i]) != 0) {
            perror("Error: pthread_create failed in Part 1");
            return 1;
        }
    }

    int total_naive_matches = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Error: pthread_join failed in Part 1");
            return 1;
        }
        total_naive_matches += thread_data[i].matches_found;
    }

    printf("Result WITHOUT overlap: %d matches found.\n", total_naive_matches);
    printf("Notice: Pattern \"%s\" was missed because it crossed the chunk boundary.\n\n", pattern);

    // --- PART 2: Searching WITH (m - 1) Overlap ---
    printf("Part 2: Searching WITH (m - 1 = %d) character overlap\n", m - 1);

    for (int i = 0; i < NUM_THREADS; i++) {
        int ext_start = base_start[i];
        int ext_end = base_end[i];

        // Non-final threads extend their search region by (m - 1) characters
        if (i < NUM_THREADS - 1) {
            ext_end = base_end[i] + (m - 1);
            if (ext_end >= n) {
                ext_end = n - 1;
            }
        }

        int ext_len = ext_end - ext_start + 1;
        printf("Thread %d extended region: [%d..%d] -> \"%.*s\"\n",
               i, ext_start, ext_end, ext_len, &text[ext_start]);

        thread_data[i].thread_id = i;
        thread_data[i].search_start = ext_start;
        thread_data[i].search_end = ext_end;
        thread_data[i].text = text;
        thread_data[i].pattern = pattern;
        thread_data[i].pattern_len = m;

        if (pthread_create(&threads[i], NULL, search_worker, &thread_data[i]) != 0) {
            perror("Error: pthread_create failed in Part 2");
            return 1;
        }
    }

    printf("\nRunning threads with overlap...\n");
    int total_overlap_matches = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Error: pthread_join failed in Part 2");
            return 1;
        }
        total_overlap_matches += thread_data[i].matches_found;
    }

    printf("Result WITH overlap: %d match(es) found.\n", total_overlap_matches);
    printf("Success: Thread 0 found \"%s\" at index 2 by inspecting the overlap.\n", pattern);

    return 0;
}
