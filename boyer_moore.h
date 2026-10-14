#ifndef BOYER_MOORE_H
#define BOYER_MOORE_H

/*
 * boyer_moore.h - interface of the Boyer-Moore string matching library
 * implemented in boyer_moore.cpp (bad-character + good-suffix heuristics).
 *
 * The character alphabet assumed by the bad-character table is the full
 * 8-bit range, so any byte text/pattern is handled.
 */

#include <string>
#include <vector>

namespace bm {

constexpr int ALPHABET_SIZE = 256;

/* badChar[c] = index of the LAST occurrence of byte c in the pattern, or
 * -1 if the pattern does not contain it. */
std::vector<int> buildBadCharTable(std::string_view pattern);

/* goodSuffix[j] = how far the pattern window may slide when a mismatch
 * happens with j pattern characters already matched ((m-1)..0). */
std::vector<int> buildGoodSuffixTable(std::string_view pattern);

/* Runs the BM scan over window-START positions in [start_idx, end_idx).
 * A window starting at s needs text[s .. s+m-1], so every occurrence whose
 * START lies inside the range is found even across chunk boundaries.
 * Returns the list of match start indices (ascending). */
std::vector<size_t> searchChunk(std::string_view text,
                                std::string_view pattern,
                                const std::vector<int>& badChar,
                                const std::vector<int>& goodSuffix,
                                size_t start_idx,
                                size_t end_idx);

/* Whole-text sequential search (serial baseline). */
std::vector<size_t> searchSequential(std::string_view text,
                                     std::string_view pattern);

} // namespace bm

#endif // BOYER_MOORE_H
