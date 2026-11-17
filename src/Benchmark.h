#pragma once
// Timing experiments for the report.

#include <iosfwd>
#include <string>
#include <vector>

class Trie;

// Baseline without a trie: scan the whole word list, keep the 5 best matches.
// O(N * |p|) per query.
std::vector<std::string> autocompleteBruteForce(const std::vector<std::u32string>& words,
                                                const std::u32string& prefix);

// Prints the comparison "cache vs BFS vs brute force" for prefix lengths 1..5
// and the cost of insert/remove on the loaded dictionary. The trie is modified
// during the test but restored at the end.
void runBenchmark(Trie& trie, std::ostream& os);
