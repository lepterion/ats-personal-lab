#include "Trie.h"

#include <algorithm>
#include <deque>

#include "Utf8.h"

Trie::Trie() : root_(std::make_unique<Node>()) {}
Trie::~Trie() = default;
Trie::Trie(Trie&&) noexcept = default;
Trie& Trie::operator=(Trie&&) noexcept = default;

void Trie::clear() {
    root_ = std::make_unique<Node>();
    wordTable_.clear();
    freeIds_.clear();
    words_ = 0;
    nodes_ = 1;
}

// ---------- helpers ----------

Trie::Node* Trie::findChild(const Node* n, char32_t c) {
    const int rank = uk::letterRank(c);
    auto it = std::lower_bound(n->children.begin(), n->children.end(), rank,
                               [](const Child& ch, int r) { return uk::letterRank(ch.first) < r; });
    if (it != n->children.end() && it->first == c) {
        return it->second.get();
    }

    return nullptr;
}

const Trie::Node* Trie::findNode(const std::u32string& key) const {
    const Node* n = root_.get();

    for (char32_t c : key) {
        n = findChild(n, c);

        if (n == nullptr) {
            return nullptr;
        }
    }

    return n;
}

Trie::WordId Trie::allocateWord(const std::u32string& key) {
    if (!freeIds_.empty()) {
        const WordId id = freeIds_.back();
        freeIds_.pop_back();
        wordTable_[id] = key;

        return id;
    }

    wordTable_.push_back(key);

    return static_cast<WordId>(wordTable_.size() - 1);
}

void Trie::releaseWord(WordId id) {
    wordTable_[id].clear();
    wordTable_[id].shrink_to_fit();
    freeIds_.push_back(id);
}

// a is a better (earlier) autocomplete answer than b: shorter, or same length
// and alphabetically smaller.
bool Trie::better(WordId a, WordId b) const {
    const std::u32string& wa = wordTable_[a];
    const std::u32string& wb = wordTable_[b];

    if (wa.size() != wb.size()) {
        return wa.size() < wb.size();
    }

    return uk::alphabeticLess(wa, wb);
}

// Inserts candidate into the sorted cache of n if it is among the kTop best. O(kTop).
void Trie::offerToTop(Node* n, WordId candidate) const {
    if (n->topSize == kTop && !better(candidate, n->top[kTop - 1])) {
        return;
    }

    int pos = n->topSize < kTop ? n->topSize : kTop - 1;

    // Shift worse entries one step right (the last one falls out if the cache is full).
    while (pos > 0 && better(candidate, n->top[pos - 1])) {
        n->top[pos] = n->top[pos - 1];
        --pos;
    }

    n->top[pos] = candidate;

    if (n->topSize < kTop) {
        ++n->topSize;
    }
}

// Rebuilds the cache of n from its own word and the caches of its children.
// Correct because the kTop best words of a subtree are among the node's own word
// and the kTop best words of each child subtree. O(sigma * kTop).
void Trie::recomputeTop(Node* n) const {
    n->topSize = 0;

    if (n->terminal()) {
        offerToTop(n, n->word);
    }

    for (const Child& ch : n->children) {
        for (int i = 0; i < ch.second->topSize; ++i) {
            offerToTop(n, ch.second->top[i]);
        }
    }
}

bool Trie::topContains(const Node* n, WordId id) {
    for (int i = 0; i < n->topSize; ++i) {
        if (n->top[i] == id) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> Trie::topAsStrings(const Node* n, std::size_t k) const {
    std::vector<std::string> out;

    for (int i = 0; i < n->topSize && out.size() < k; ++i) {
        out.push_back(utf8::encode(wordTable_[n->top[i]]));
    }

    return out;
}

// ---------- operations ----------

bool Trie::insert(const std::string& word) {
    const std::u32string key = uk::normalizeWord(word);

    if (key.empty()) {
        return false;
    }

    // Walk down, creating missing nodes; remember the path for the cache update.
    std::vector<Node*> path;
    path.reserve(key.size() + 1);
    Node* n = root_.get();
    path.push_back(n);

    for (char32_t c : key) {
        Node* next = findChild(n, c);

        if (next == nullptr) {
            const int rank = uk::letterRank(c);
            auto it = std::lower_bound(
                n->children.begin(), n->children.end(), rank,
                [](const Child& ch, int r) { return uk::letterRank(ch.first) < r; });
            it = n->children.insert(it, Child(c, std::make_unique<Node>()));
            next = it->second.get();
            ++nodes_;
        }

        n = next;
        path.push_back(n);
    }

    if (n->terminal()) {
        return false;   // already in the dictionary
    }

    n->word = allocateWord(key);
    ++words_;

    // The new word belongs to the subtree of every node on the path.
    for (Node* p : path) {
        offerToTop(p, n->word);
    }

    return true;
}

bool Trie::remove(const std::string& word) {
    const std::u32string key = uk::normalizeWord(word);

    if (key.empty()) {
        return false;
    }

    std::vector<Node*> path;
    path.reserve(key.size() + 1);
    Node* n = root_.get();
    path.push_back(n);

    for (char32_t c : key) {
        n = findChild(n, c);

        if (n == nullptr) {
            return false;
        }

        path.push_back(n);
    }

    if (!n->terminal()) {
        return false;
    }

    // The id stays valid until the caches are repaired, then it is freed.
    const WordId removed = n->word;
    n->word = kNoWord;
    --words_;

    // Bottom-up: free nodes that have become useless (no word, no children),
    // and repair the caches that contained the removed word.
    bool repairRoot = true;

    for (std::size_t i = path.size() - 1; i > 0; --i) {
        Node* cur = path[i];

        if (!cur->terminal() && cur->children.empty()) {
            Node* parent = path[i - 1];
            const char32_t c = key[i - 1];
            parent->children.erase(std::find_if(parent->children.begin(), parent->children.end(),
                                                [c](const Child& ch) { return ch.first == c; }));
            --nodes_;
            continue;
        }

        // If the word is not in this subtree's top, it is not in any ancestor's
        // top either (an ancestor's best words come from its children's best).
        if (!topContains(cur, removed)) {
            repairRoot = false;
            break;
        }

        recomputeTop(cur);
    }

    if (repairRoot && topContains(root_.get(), removed)) {
        recomputeTop(root_.get());
    }

    releaseWord(removed);

    return true;
}

bool Trie::contains(const std::string& word) const {
    const std::u32string key = uk::normalizeWord(word);

    if (key.empty()) {
        return false;
    }

    const Node* n = findNode(key);

    return n != nullptr && n->terminal();
}

bool Trie::hasPrefix(const std::string& prefix) const {
    const std::u32string key = uk::normalizePrefix(prefix);

    if (key.empty() && !prefix.empty()) {
        // A non-empty string that normalises to nothing is not a valid prefix,
        // unless it is only spaces.
        if (prefix.find_first_not_of(" \t\r\n") != std::string::npos) {
            return false;
        }
    }

    const Node* n = findNode(key);
    return n != nullptr && n->topSize > 0;
}

std::vector<std::string> Trie::autocomplete(const std::string& prefix) const {
    const std::u32string key = uk::normalizePrefix(prefix);

    if (key.empty() && prefix.find_first_not_of(" \t\r\n") != std::string::npos) {
        return {};
    }

    const Node* n = findNode(key);

    if (n == nullptr) {
        return {};
    }

    return topAsStrings(n, kTop);
}

std::vector<std::string> Trie::autocompleteBfs(const std::string& prefix, std::size_t k,
                                               QueryStats* stats) const {
    std::vector<std::string> out;
    const std::u32string key = uk::normalizePrefix(prefix);

    if (key.empty() && prefix.find_first_not_of(" \t\r\n") != std::string::npos) {
        return out;
    }

    const Node* start = findNode(key);

    if (start == nullptr || k == 0) {
        return out;
    }

    std::size_t visited = 0;
    std::deque<const Node*> queue{start};

    while (!queue.empty() && out.size() < k) {
        const Node* n = queue.front();
        queue.pop_front();
        ++visited;

        if (n->terminal()) {
            out.push_back(utf8::encode(wordTable_[n->word]));
        }

        for (const Child& ch : n->children) {
            queue.push_back(ch.second.get());
        }
    }

    if (stats != nullptr) {
        stats->nodesVisited = visited;
    }

    return out;
}

std::vector<std::string> Trie::allWords() const {
    std::vector<std::string> out;
    out.reserve(words_);
    std::vector<const Node*> stack{root_.get()};

    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();

        if (n->terminal()) {
            out.push_back(utf8::encode(wordTable_[n->word]));
        }

        // Push in reverse so the alphabetically first child is processed first.
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
            stack.push_back(it->second.get());
        }
    }

    return out;
}

bool Trie::checkInvariants() const {
    std::size_t nodes = 0;
    std::size_t words = 0;
    bool ok = true;

    // Post-order: children are checked before their parent.
    struct Frame {
        const Node* n;
        bool expanded;
    };

    std::vector<Frame> stack{{root_.get(), false}};

    while (!stack.empty() && ok) {
        Frame f = stack.back();
        stack.pop_back();

        if (!f.expanded) {
            stack.push_back({f.n, true});

            for (const Child& ch : f.n->children) {
                stack.push_back({ch.second.get(), false});
            }

            continue;
        }

        const Node* n = f.n;
        ++nodes;

        if (n->terminal()) {
            ++words;

            if (wordTable_[n->word].empty()) {
                ok = false;   // a terminal node points to a freed id
            }
        }

        if (n != root_.get() && !n->terminal() && n->children.empty()) {
            ok = false;   // dead node
        }

        for (std::size_t i = 1; i < n->children.size(); ++i) {
            if (uk::letterRank(n->children[i - 1].first) >= uk::letterRank(n->children[i].first)) {
                ok = false;
            }
        }

        // Recompute the cache into a temporary node, without touching n.
        Node tmp;

        if (n->terminal()) {
            offerToTop(&tmp, n->word);
        }

        for (const Child& ch : n->children) {
            for (int i = 0; i < ch.second->topSize; ++i) {
                offerToTop(&tmp, ch.second->top[i]);
            }
        }

        if (tmp.topSize != n->topSize) {
            ok = false;
        }

        for (int i = 0; ok && i < n->topSize; ++i) {
            if (tmp.top[i] != n->top[i]) {
                ok = false;
            }
        }
    }

    return ok && nodes == nodes_ && words == words_ &&
           wordTable_.size() == words_ + freeIds_.size();
}

std::size_t Trie::memoryUsage() const {
    std::size_t bytes = sizeof(*this);
    std::vector<const Node*> stack{root_.get()};

    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        bytes += sizeof(Node) + n->children.capacity() * sizeof(Child);

        for (const Child& ch : n->children) {
            stack.push_back(ch.second.get());
        }
    }

    bytes += wordTable_.capacity() * sizeof(std::u32string) + freeIds_.capacity() * sizeof(WordId);

    // A short word fits into the string object itself (small string optimisation).
    const std::u32string probe;

    for (const std::u32string& w : wordTable_) {
        if (w.capacity() > probe.capacity()) {
            bytes += (w.capacity() + 1) * sizeof(char32_t);
        }
    }

    return bytes;
}
