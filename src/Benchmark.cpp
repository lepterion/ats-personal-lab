#include "Benchmark.h"

#include <chrono>
#include <iomanip>
#include <ostream>
#include <random>

#include "Trie.h"
#include "Utf8.h"

namespace {

using Clock = std::chrono::steady_clock;

double nsSince(Clock::time_point t) {
    return std::chrono::duration<double, std::nano>(Clock::now() - t).count();
}

bool betterWord(const std::u32string& a, const std::u32string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return uk::alphabeticLess(a, b);
}

}  // namespace

std::vector<std::string> autocompleteBruteForce(const std::vector<std::u32string>& words,
                                                const std::u32string& prefix) {
    std::vector<const std::u32string*> best;   // sorted, at most 5
    for (const std::u32string& w : words) {
        if (w.size() < prefix.size() || w.compare(0, prefix.size(), prefix) != 0) continue;
        if (best.size() == 5 && !betterWord(w, *best.back())) continue;
        auto pos = best.end();
        while (pos != best.begin() && betterWord(w, **(pos - 1))) --pos;
        best.insert(pos, &w);
        if (best.size() > 5) best.pop_back();
    }
    std::vector<std::string> out;
    for (const auto* w : best) out.push_back(utf8::encode(*w));
    return out;
}

void runBenchmark(Trie& trie, std::ostream& os) {
    const std::vector<std::string> all = trie.allWords();
    if (all.empty()) {
        os << "Словник порожній\n";
        return;
    }
    std::vector<std::u32string> words32;
    words32.reserve(all.size());
    for (const auto& w : all) words32.push_back(utf8::decode(w));

    std::mt19937 rng(2026);
    std::uniform_int_distribution<std::size_t> pick(0, all.size() - 1);
    os << "Слів: " << trie.size() << ", вузлів: " << trie.nodeCount() << "\n\n";

    os << "Автодоповнення, середній час одного запиту\n";
    // Cyrillic letters take 2 bytes and setw counts bytes, so the header is aligned by hand.
    os << " |p|  запитів    кеш, нс    BFS, нс  BFS вузлів  перебір, мкс  збіг\n";
    std::size_t sink = 0;
    for (std::size_t len = 1; len <= 5; ++len) {
        // Prefixes = first len letters of random words (so they exist).
        std::vector<std::string> prefixes;
        while (prefixes.size() < 2000) {
            const std::u32string& w = words32[pick(rng)];
            if (w.size() >= len) prefixes.push_back(utf8::encode(w.substr(0, len)));
        }
        auto t = Clock::now();
        for (const auto& p : prefixes) sink += trie.autocomplete(p).size();
        const double cacheNs = nsSince(t) / prefixes.size();

        std::size_t visited = 0;
        t = Clock::now();
        for (const auto& p : prefixes) {
            Trie::QueryStats st;
            sink += trie.autocompleteBfs(p, Trie::kTop, &st).size();
            visited += st.nodesVisited;
        }
        const double bfsNs = nsSince(t) / prefixes.size();

        const std::size_t bruteCount = 100;
        bool same = true;
        t = Clock::now();
        for (std::size_t i = 0; i < bruteCount; ++i) {
            const auto r = autocompleteBruteForce(words32, utf8::decode(prefixes[i]));
            sink += r.size();
            same = same && r == trie.autocomplete(prefixes[i]) && r == trie.autocompleteBfs(prefixes[i]);
        }
        const double bruteUs = nsSince(t) / bruteCount / 1000.0;

        os << std::fixed << std::setprecision(0) << std::setw(4) << len << std::setw(9)
           << prefixes.size() << std::setw(11) << cacheNs << std::setw(11) << bfsNs
           << std::setw(12) << static_cast<double>(visited) / prefixes.size() << std::setprecision(1)
           << std::setw(14) << bruteUs << "  " << (same ? "так" : "НІ") << "\n"
           << std::flush;
    }

    // Updates: remove 10000 random words, then put them back.
    std::vector<std::string> sample;
    for (int i = 0; i < 10000; ++i) sample.push_back(all[pick(rng)]);
    std::size_t removed = 0, added = 0;
    auto t = Clock::now();
    for (const auto& w : sample) removed += trie.remove(w) ? 1 : 0;
    const double removeNs = nsSince(t) / sample.size();
    t = Clock::now();
    for (const auto& w : sample) added += trie.insert(w) ? 1 : 0;
    const double insertNs = nsSince(t) / sample.size();
    t = Clock::now();
    for (const auto& w : sample) sink += trie.contains(w) ? 1 : 0;
    const double searchNs = nsSince(t) / sample.size();

    os << "\nОперації над " << sample.size() << " випадковими словами (середній час)\n"
       << std::setprecision(0) << "  пошук:     " << searchNs << " нс\n"
       << "  видалення: " << removeNs << " нс (видалено " << removed << ")\n"
       << "  додавання: " << insertNs << " нс (додано " << added << ")\n"
       << "  слів після відновлення: " << trie.size() << "\n";
    os << std::defaultfloat << std::setprecision(6);
    if (sink == 42) os << "";   // keeps the results "used"
}
