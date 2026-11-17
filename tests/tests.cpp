// Unit tests (no external framework). Run: trie_tests [name filter]
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Benchmark.h"
#include "Dictionary.h"
#include "Trie.h"
#include "Utf8.h"

namespace {

struct TestCase {
    const char* name;
    void (*fn)();
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define TEST(name)                                     \
    static void name();                                \
    static Registrar name##_reg(#name, &name);         \
    static void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::ostringstream m_;                                                     \
            m_ << "line " << __LINE__ << ": CHECK(" #cond ")";                         \
            throw Failure(m_.str());                                                   \
        }                                                                              \
    } while (0)

using Words = std::vector<std::string>;

Trie makeTrie(const Words& words) {
    Trie t;
    for (const auto& w : words) t.insert(w);
    return t;
}

}  // namespace

// ---------- UTF-8 and normalisation ----------

TEST(utf8_roundtrip) {
    const std::string s = "м'ясо ґанок Їжак € 😀 abc";
    CHECK(utf8::encode(utf8::decode(s)) == s);
    CHECK(utf8::decode("ґ") == std::u32string(1, 0x491));
    CHECK(utf8::decode("м'яч").size() == 4);   // 4 letters, 7 bytes
}

TEST(utf8_rejects_malformed) {
    bool thrown = false;
    try { utf8::decode("\xD0"); } catch (const std::invalid_argument&) { thrown = true; }
    CHECK(thrown);
    thrown = false;
    try { utf8::decode("\xC0\xAF"); } catch (const std::invalid_argument&) { thrown = true; }   // overlong
    CHECK(thrown);
}

TEST(normalize_word) {
    CHECK(uk::normalizeWord("  ПРИВІТ\r") == utf8::decode("привіт"));
    CHECK(uk::normalizeWord("Ґанок") == utf8::decode("ґанок"));
    CHECK(uk::normalizeWord("ЄВРОПА") == utf8::decode("європа"));
    CHECK(uk::normalizeWord("Їжак") == utf8::decode("їжак"));
    CHECK(uk::normalizeWord("м’ясо") == utf8::decode("м'ясо"));   // typographic apostrophe
    CHECK(uk::normalizeWord("мʼясо") == utf8::decode("м'ясо"));
    CHECK(uk::normalizeWord("будь-який") == utf8::decode("будь-який"));
    CHECK(uk::normalizeWord("").empty());
    CHECK(uk::normalizeWord("два слова").empty());
    CHECK(uk::normalizeWord("hello").empty());
    CHECK(uk::normalizeWord("ыы").empty());
    CHECK(uk::normalizeWord("-так").empty());
    CHECK(uk::normalizeWord("так-").empty());
    CHECK(uk::normalizeWord("а--б").empty());
    CHECK(uk::normalizePrefix("м'") == utf8::decode("м'"));
    CHECK(uk::normalizePrefix("").empty());
}

TEST(alphabet_order) {
    // Ukrainian order: г < ґ < д, е < є < ж, и < і < ї < й.
    const char* ordered[] = {"г", "ґ", "д", "е", "є", "ж", "и", "і", "ї", "й", "я"};
    for (int i = 0; i + 1 < 11; ++i)
        CHECK(uk::alphabeticLess(utf8::decode(ordered[i]), utf8::decode(ordered[i + 1])));
    CHECK(uk::alphabeticLess(utf8::decode("кіт"), utf8::decode("кітель")));
    CHECK(!uk::alphabeticLess(utf8::decode("кіт"), utf8::decode("кіт")));
}

// ---------- basic operations ----------

TEST(trie_insert_contains) {
    Trie t;
    CHECK(t.empty());
    CHECK(t.insert("кіт"));
    CHECK(t.insert("кит"));
    CHECK(t.insert("кітель"));
    CHECK(!t.insert("кіт"));   // duplicate
    CHECK(!t.insert("КІТ"));   // duplicate after normalisation
    CHECK(!t.insert(""));
    CHECK(!t.insert("cat"));
    CHECK(t.size() == 3);
    CHECK(t.contains("кіт") && t.contains("Кіт") && t.contains("кит") && t.contains("кітель"));
    CHECK(!t.contains("кі"));       // a prefix, not a word
    CHECK(!t.contains("кітелі"));
    CHECK(t.hasPrefix("кі") && t.hasPrefix("") && !t.hasPrefix("ко"));
    CHECK(t.checkInvariants());
}

TEST(trie_node_count) {
    Trie t;
    CHECK(t.nodeCount() == 1);
    t.insert("про");     // п, р, о
    t.insert("проба");   // + б, а
    t.insert("прут");    // + у, т
    CHECK(t.nodeCount() == 1 + 3 + 2 + 2);
}

TEST(trie_remove) {
    Trie t = makeTrie({"про", "проба", "пробі", "прут"});
    const std::size_t nodes = t.nodeCount();
    CHECK(!t.remove("пр"));        // only a prefix
    CHECK(!t.remove("промінь"));   // absent
    CHECK(t.remove("про"));        // a word inside other words: no nodes freed
    CHECK(!t.contains("про") && t.contains("проба") && t.contains("пробі"));
    CHECK(t.nodeCount() == nodes);
    CHECK(t.remove("проба"));      // frees one node ("а")
    CHECK(t.nodeCount() == nodes - 1);
    CHECK(!t.remove("проба"));
    CHECK(t.size() == 2);
    CHECK(t.checkInvariants());
    CHECK(t.remove("пробі") && t.remove("прут"));
    CHECK(t.empty() && t.nodeCount() == 1);   // everything freed
    CHECK(t.autocomplete("").empty());
    CHECK(t.checkInvariants());
}

TEST(trie_all_words_alphabetical) {
    Trie t = makeTrie({"яр", "ґава", "гава", "їжак", "іти", "иній", "є", "е"});
    const Words expected = {"гава", "ґава", "е", "є", "иній", "іти", "їжак", "яр"};
    CHECK(t.allWords() == expected);
}

// ---------- autocomplete ----------

TEST(autocomplete_example_from_task) {
    Trie t = makeTrie({"проспект", "про", "проза", "простий", "проба", "пробіг", "програма",
                       "прокурор", "просо", "пройти", "пр", "мама"});
    const Words expected = {"про", "проба", "проза", "просо", "пробіг"};
    CHECK(t.autocomplete("про") == expected);
    CHECK(t.autocompleteBfs("про") == expected);
    CHECK(t.autocomplete("ПРО") == expected);   // prefix is normalised too
}

TEST(autocomplete_shorter_first_then_alphabet) {
    // Same length goes alphabetically: а < є < і < я. Code point order would give
    // га, гя, гє, гі (я = U+044F, є = U+0454, і = U+0456), which is wrong.
    Trie t = makeTrie({"гяк", "гя", "гі", "гє", "га", "г", "ґа"});
    const Words expected = {"г", "га", "гє", "гі", "гя"};
    CHECK(t.autocomplete("г") == expected);
    CHECK(t.autocompleteBfs("г") == expected);
    // ґ is a separate letter (after г), so ґа is a separate branch.
    CHECK(t.autocomplete("ґ") == Words{"ґа"});
}

TEST(autocomplete_fewer_than_five_and_missing) {
    Trie t = makeTrie({"дім", "дімок", "дах"});
    CHECK((t.autocomplete("ді") == Words{"дім", "дімок"}));
    CHECK(t.autocomplete("дім") == (Words{"дім", "дімок"}));   // the prefix itself is a word
    CHECK(t.autocomplete("ко").empty());
    CHECK(t.autocomplete("hello").empty());
    CHECK(t.autocompleteBfs("ко").empty());
    CHECK((t.autocomplete("") == Words{"дах", "дім", "дімок"}));   // empty prefix: whole dictionary
}

TEST(autocomplete_apostrophe) {
    Trie t = makeTrie({"м'ясо", "м'яч", "м'яз", "мама", "м’ята"});
    CHECK((t.autocomplete("м'я") == Words{"м'яз", "м'яч", "м'ясо", "м'ята"}));
    CHECK((t.autocomplete("м’") == Words{"м'яз", "м'яч", "м'ясо", "м'ята"}));
}

TEST(autocomplete_updates_after_insert_and_remove) {
    Trie t = makeTrie({"проба", "проза", "просо", "прокат", "пролог", "промінь"});
    CHECK((t.autocomplete("про") == Words{"проба", "проза", "просо", "прокат", "пролог"}));
    t.insert("про");
    CHECK((t.autocomplete("про") == Words{"про", "проба", "проза", "просо", "прокат"}));
    t.remove("проба");
    t.remove("про");
    // A word that had dropped out of the cache must come back.
    CHECK((t.autocomplete("про") == Words{"проза", "просо", "прокат", "пролог", "промінь"}));
    CHECK(t.autocomplete("про") == t.autocompleteBfs("про"));
    CHECK(t.checkInvariants());
}

TEST(autocomplete_bfs_any_k) {
    Trie t = makeTrie({"а", "аб", "ав", "абв", "абг", "ага", "абвг"});
    CHECK((t.autocompleteBfs("а", 7) == Words{"а", "аб", "ав", "абв", "абг", "ага", "абвг"}));
    CHECK((t.autocompleteBfs("а", 2) == Words{"а", "аб"}));
    CHECK(t.autocompleteBfs("а", 0).empty());
    Trie::QueryStats st;
    t.autocompleteBfs("а", 1, &st);
    CHECK(st.nodesVisited == 1);   // stops as soon as k words are found
}

// ---------- randomized cross-check ----------

TEST(randomized_against_brute_force) {
    // Random insert/remove on a small alphabet (many shared prefixes, many ties),
    // after each step: cache == BFS == brute force, and invariants hold.
    const std::u32string letters = U"абвгґіїя";
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> len(1, 6), letter(0, 7), coin(0, 2);
    Trie t;
    std::set<std::string> model;
    for (int step = 0; step < 3000; ++step) {
        std::u32string w;
        const int n = len(rng);
        for (int i = 0; i < n; ++i) w.push_back(letters[letter(rng)]);
        const std::string s = utf8::encode(w);
        if (coin(rng) == 0) {
            CHECK(t.remove(s) == (model.erase(s) == 1));
        } else {
            CHECK(t.insert(s) == model.insert(s).second);
        }
        CHECK(t.size() == model.size());
        if (step % 50 == 0) CHECK(t.checkInvariants());
        std::vector<std::u32string> all;
        for (const auto& m : model) all.push_back(utf8::decode(m));
        for (const char* p : {"", "а", "аб", "ґ", "і", "ія", "ааа"}) {
            const auto brute = autocompleteBruteForce(all, utf8::decode(p));
            CHECK(t.autocomplete(p) == brute);
            CHECK(t.autocompleteBfs(p) == brute);
        }
    }
    CHECK(t.checkInvariants());
    for (const auto& m : model) CHECK(t.remove(m));
    CHECK(t.empty() && t.nodeCount() == 1 && t.checkInvariants());
}

// ---------- dictionary loading ----------

TEST(load_dictionary_stream) {
    std::istringstream in("\xEF\xBB\xBFкіт\r\nКІТ\n\nмама 123\nhello\nм’ята\n");
    Trie t;
    const LoadStats st = loadDictionary(t, in);
    CHECK(st.lines == 5);
    CHECK(st.inserted == 3);
    CHECK(st.duplicates == 1);
    CHECK(st.invalid == 1);
    CHECK(t.contains("кіт") && t.contains("мама") && t.contains("м'ята"));
}

TEST(load_dictionary_missing_file) {
    Trie t;
    bool thrown = false;
    try { loadDictionaryFile(t, "no/such/file.txt"); } catch (const std::runtime_error&) { thrown = true; }
    CHECK(thrown);
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& tc : registry()) {
        if (filter != nullptr && std::strstr(tc.name, filter) == nullptr) continue;
        try {
            tc.fn();
            ++passed;
            std::cout << "[PASS] " << tc.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << tc.name << ": " << e.what() << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
