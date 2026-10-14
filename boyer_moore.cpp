#include "boyer_moore.h"
#include <algorithm>

namespace bm {

std::vector<int> buildBadCharTable(std::string_view pattern) {
    std::vector<int> badChar(ALPHABET_SIZE, -1);
    // Initialize all occurrences as -1
    // Then fill the actual value of last occurrence of a character
    for (int i = 0; i < static_cast<int>(pattern.length()); ++i) {
        badChar[static_cast<unsigned char>(pattern[i])] = i;
    }
    return badChar;
}

std::vector<int> buildGoodSuffixTable(std::string_view pattern) {
    int m = pattern.length();
    std::vector<int> goodSuffix(m, m);
    std::vector<int> suffixes(m, 0);

    // Compute suffix lengths (similar to Z-algorithm but right-to-left)
    int f = 0, g = m - 1;
    suffixes[m - 1] = m;
    for (int i = m - 2; i >= 0; --i) {
        if (i > g && suffixes[i + m - 1 - f] < i - g) {
            suffixes[i] = suffixes[i + m - 1 - f];
        } else {
            if (i < g) g = i;
            f = i;
            while (g >= 0 && pattern[g] == pattern[g + m - 1 - f]) {
                --g;
            }
            suffixes[i] = f - g;
        }
    }

    // Populate Good Suffix table using the suffixes array
    int j = 0;
    for (int i = m - 1; i >= 0; --i) {
        if (suffixes[i] == i + 1) {
            for (; j < m - 1 - i; ++j) {
                if (goodSuffix[j] == m) {
                    goodSuffix[j] = m - 1 - i;
                }
            }
        }
    }
    for (int i = 0; i <= m - 2; ++i) {
        goodSuffix[m - 1 - suffixes[i]] = m - 1 - i;
    }

    return goodSuffix;
}

std::vector<size_t> searchChunk(
    std::string_view text,
    std::string_view pattern,
    const std::vector<int>& badChar,
    const std::vector<int>& goodSuffix,
    size_t start_idx,
    size_t end_idx)
{
    std::vector<size_t> occurrences;
    size_t m = pattern.length();

    if (end_idx > text.length()) end_idx = text.length();
    if (m == 0 || start_idx >= end_idx || m > end_idx - start_idx) {
        return occurrences;
    }

    size_t s = start_idx; // s is shift of the pattern with respect to text
    while (s <= end_idx - m) {
        int j = static_cast<int>(m) - 1;

        // Keep reducing index j of pattern while characters match
        while (j >= 0 && pattern[j] == text[s + j]) {
            j--;
        }

        if (j < 0) {
            // Pattern found
            occurrences.push_back(s);
            // Shift the pattern so that the next character aligns with the last occurrence
            s += goodSuffix[0];
        } else {
            // Pattern doesn't match: calculate max shift between Bad Character and Good Suffix heuristics
            int badCharShift = j - badChar[static_cast<unsigned char>(text[s + j])];
            int goodSuffixShift = goodSuffix[j];
            s += std::max(badCharShift, goodSuffixShift);
        }
    }

    return occurrences;
}

std::vector<size_t> searchSequential(std::string_view text, std::string_view pattern) {
    if (pattern.empty() || pattern.length() > text.length()) {
        return {};
    }

    std::vector<int> badChar = buildBadCharTable(pattern);
    std::vector<int> goodSuffix = buildGoodSuffixTable(pattern);

    return searchChunk(text, pattern, badChar, goodSuffix, 0, text.length());
}

} 
