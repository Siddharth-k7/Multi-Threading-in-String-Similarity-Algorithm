#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <algorithm>
#include <string_view> // prevents temporary string creating and length calculations

constexpr long long BASE = 5;
constexpr long long HASH_MOD = 1000000007; // High prime to minimize the spurious hash collisions

/**
 * Worker thread target routine
 * Executes exact string matching on a dedicated segment of shared memory
 */

void rabin_karp_worker(std::string_view text, std::string_view pattern, 
    size_t start, size_t end, long long highest_power, 
    long long pattern_hash, std::vector<size_t>& local_results) {

        size_t text_len = text.length(); 
        size_t pattern_len = pattern.length();

        size_t effective_end = std::min(end, text_len); // boundary lookahead adjustment

        if ((effective_end - start) < pattern_len) return;

        // 1. Initial state seeding phase
        long long current_window_hash = 0;
        for (size_t i = start; i < start + pattern_len; i++) {
            current_window_hash = (BASE * current_window_hash + text[i]) % HASH_MOD;
        }

        // 2. Sliding window loop across assigned chunk + lookahead
        size_t max_iterations = effective_end - pattern_len;
        for (size_t i = start; i <= max_iterations; ++i) {

            // potential match detected via hash equality
            if (current_window_hash == pattern_hash) {
                if (text.compare(i, pattern_len, pattern) == 0) {
                    local_results.push_back(i); // lock-free storage accumulation within private thread stack
                }
            }

            // Computing the rolling hash for the subsequent text window
            if (i < max_iterations) {
                long long leading_char_component = (text[i] * highest_power) % HASH_MOD;
                current_window_hash = (current_window_hash - leading_char_component + HASH_MOD) % HASH_MOD;

                current_window_hash = (current_window_hash * BASE + text[i + pattern_len]) % HASH_MOD;
            }
        }
    }

    /**
     * Manages partitioning, thread forks, barries and results merge.
     */

     std::vector<size_t> parallel_rabin_karp_search(const std::string& text, const std::string& pattern) {
        size_t n = text.length();
        size_t m = pattern.length();
        if (n < m || m == 0) return {};

        unsigned int num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;

        // Compute the pre-processing constants
        long long pattern_hash = 0;
        long long highest_power = 1;

        for (size_t i = 0; i < m; ++i) {
            pattern_hash = (BASE * pattern_hash + pattern[i]) % HASH_MOD;
            if (i > 0) {
                highest_power = (highest_power * BASE) % HASH_MOD;
            }
        }

        size_t standard_chunk_size = n / num_threads;

        // allocating isolated thread vectors to eliminate race conditions dependencies
        std::vector<std::thread> worker_pool;
        std::vector<std::vector<size_t>> thread_local_buffers(num_threads);
        
        std::string_view text_view(text);
        std::string_view pattern_view(pattern);

        // Memory partitioning
        for (unsigned int i = 0; i < num_threads; ++i) {
            size_t chunk_start = i * standard_chunk_size;
            size_t chunk_end = (i == num_threads - 1) ? n : (i + 1) * standard_chunk_size + (m - 1);

            worker_pool.emplace_back(rabin_karp_worker, text_view, pattern_view,
                                    chunk_start, chunk_end, highest_power, pattern_hash,
                                std::ref(thread_local_buffers[i]));

        }

        for (auto& worker: worker_pool) {
            worker.join();
        }

        // merging
        std::vector<size_t> global_matches;
        for (const auto& local_buffer: thread_local_buffers) {
            global_matches.insert(global_matches.end(), local_buffer.begin(), local_buffer.end());
        }

        std::sort(global_matches.begin(), global_matches.end());
        global_matches.erase(std::unique(global_matches.begin(), global_matches.end()), global_matches.end());

        return global_matches;
}


int main() {
    // Demonstration Genomic Context (Simulating a long chromosome segment)
    std::string dna_sequence = "ATCGATCGATCGATCGATCGTTTGGCCAAATTTGGCCAAATCGATCG";
    std::string gene_target = "ATCG";

    std::vector<size_t> matches = parallel_rabin_karp_search(dna_sequence, gene_target);

    std::cout << "Target DNA Matches located at indices: ";
    for (size_t index : matches) {
        std::cout << index << " ";
    }
    std::cout << "\nTotal matches found: " << matches.size() << std::endl;

    return 0;
}

