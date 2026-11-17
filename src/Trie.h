#pragma once
// Prefix tree (trie) for a dictionary of unique words with autocomplete:
// "top-5 shortest words that start with a prefix".
//
// Keys are Unicode code points (see Utf8.h). Words are compared by
//   1) length in letters, 2) Ukrainian alphabetical order (uk::alphabeticLess),
// so among words of equal length the answer is alphabetical and always the same.
//
// Every node keeps a small cache `top`: the kTop best (shortest) words of its
// subtree. Then autocomplete is: walk down the prefix, return the cache of that
// node, i.e. O(|p| log sigma + k) no matter how many words share the prefix.
// The price is paid by updates (see insert/remove). autocompleteBfs() is the
// cache-free alternative (breadth-first search) used for comparison and tests.
//
// Children of a node are a vector sorted by letter rank: binary search,
// O(log sigma) per step, and in-order traversal gives alphabetical order.

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Trie {
public:
    static constexpr int kTop = 5;

    // Counters filled by the *Stats queries, for the benchmark and the report.
    struct QueryStats {
        std::size_t nodesVisited = 0;
    };

    Trie();
    ~Trie();
    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;
    Trie(Trie&&) noexcept;
    Trie& operator=(Trie&&) noexcept;

    // Words are UTF-8; they are normalised with uk::normalizeWord (lower case,
    // apostrophe unification). An invalid word is not inserted.

    // Adds a word. Returns false if the word is invalid or already present.
    // O(L log sigma + L * kTop), L = word length.
    bool insert(const std::string& word);

    // Removes a word. Returns false if it is not in the trie. Nodes that are no
    // longer needed are freed. O(L log sigma + L * sigma * kTop) in the worst case.
    bool remove(const std::string& word);

    // Exact search. O(L log sigma).
    bool contains(const std::string& word) const;

    // At least one word starts with the prefix. O(|p| log sigma).
    bool hasPrefix(const std::string& prefix) const;

    // Top-5 shortest words with the prefix (fewer if there are fewer), shortest
    // first, ties alphabetically. Empty prefix = the whole dictionary.
    // O(|p| log sigma + kTop): reads the cache of the prefix node.
    std::vector<std::string> autocomplete(const std::string& prefix) const;

    // The same answer by breadth-first search from the prefix node, for any k.
    // BFS visits nodes level by level, i.e. by word length, and children in
    // alphabetical order, so the first k terminal nodes met are the answer.
    // O(|p| log sigma + V), V = nodes in the levels up to the k-th found word.
    std::vector<std::string> autocompleteBfs(const std::string& prefix, std::size_t k = kTop,
                                             QueryStats* stats = nullptr) const;

    std::size_t size() const { return words_; }        // number of words
    std::size_t nodeCount() const { return nodes_; }   // including the root
    bool empty() const { return words_ == 0; }
    void clear();

    // All words in alphabetical order (DFS). O(total length).
    std::vector<std::string> allWords() const;

    // Debug check: every node's cache equals the recomputed one and there are no
    // dead nodes (non-terminal leaves). O(n * sigma * kTop). Used by the tests.
    bool checkInvariants() const;

private:
    struct Node;
    using Child = std::pair<char32_t, std::unique_ptr<Node>>;

    struct Node {
        std::vector<Child> children;   // sorted by uk::letterRank(letter)
        bool terminal = false;
        std::u32string word;                   // the whole word, set if terminal
        std::array<const Node*, kTop> top{};   // best words of the subtree (terminal nodes)
        int topSize = 0;
    };

    std::unique_ptr<Node> root_;
    std::size_t words_ = 0;
    std::size_t nodes_ = 1;

    static Node* findChild(const Node* n, char32_t c);
    const Node* findNode(const std::u32string& key) const;
    static bool better(const Node* a, const Node* b);
    static void offerToTop(Node* n, const Node* candidate);
    static void recomputeTop(Node* n);
    static bool topContains(const Node* n, const Node* x);
    static std::vector<std::string> topAsStrings(const Node* n, std::size_t k);
};
