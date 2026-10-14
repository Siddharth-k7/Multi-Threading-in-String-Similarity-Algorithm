#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define NUM_THREADS 4

// Structure passed to each worker thread
typedef struct {
    int thread_id;
    int start_index;      // Starting index of this thread's chunk
    int end_index;        // Ending index of this thread's chunk (inclusive)
    const char *text;     // Pointer to the full text
} ThreadData;

// Worker function executed by each thread
void *worker_function(void *arg) {
    // Cast the generic void pointer back to our ThreadData structure
    ThreadData *data = (ThreadData *)arg;

    int chunk_length = data->end_index - data->start_index + 1;

    // "%.*s" prints exactly chunk_length characters starting from text[start_index]
    printf("Thread %d | Start: %2d | End: %2d | Length: %2d | Substring: %.*s\n",
           data->thread_id,
           data->start_index,
           data->end_index,
           chunk_length,
           chunk_length,
           &data->text[data->start_index]);

    return NULL;
}

int main(void) {
    const char *text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int text_length = strlen(text);

    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];

    printf("POSIX Pthreads Text Partitioning Demo\n");
    printf("Text   : \"%s\" (Length: %d)\n", text, text_length);
    printf("Threads: %d\n\n", NUM_THREADS);

    // Divide text as evenly as possible among threads
    int base_chunk_size = text_length / NUM_THREADS;
    int remainder = text_length % NUM_THREADS;

    int current_start = 0;

    // Create worker threads
    for (int i = 0; i < NUM_THREADS; i++) {
        // Distribute remainder characters across the first few threads
        int current_chunk_size = base_chunk_size + (i < remainder ? 1 : 0);

        thread_data[i].thread_id = i;
        thread_data[i].start_index = current_start;
        thread_data[i].end_index = current_start + current_chunk_size - 1;
        thread_data[i].text = text;

        current_start += current_chunk_size;

        if (pthread_create(&threads[i], NULL, worker_function, &thread_data[i]) != 0) {
            perror("Error: pthread_create failed");
            return 1;
        }
    }

    // Wait for all worker threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Error: pthread_join failed");
            return 1;
        }
    }

    printf("\nAll worker threads finished successfully.\n");
    return 0;
}
