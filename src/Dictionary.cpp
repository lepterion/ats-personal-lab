#include "Dictionary.h"

#include <chrono>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>

#include "Trie.h"
#include "Utf8.h"

LoadStats loadDictionary(Trie& trie, std::istream& is) {
    LoadStats st;
    const auto start = std::chrono::steady_clock::now();

    std::string line;
    bool first = true;

    while (std::getline(is, line)) {
        if (first) {
            // Skip a UTF-8 byte order mark (Notepad adds one).
            if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                line.erase(0, 3);
            }

            first = false;
        }

        std::istringstream ls(line);
        std::string token;

        if (!(ls >> token)) {
            continue;
        }

        ++st.lines;

        if (uk::normalizeWord(token).empty()) {
            ++st.invalid;
        } else if (trie.insert(token)) {
            ++st.inserted;
        } else {
            ++st.duplicates;
        }
    }

    st.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    return st;
}

LoadStats loadDictionaryFile(Trie& trie, const std::string& path) {
    std::ifstream in(path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }

    return loadDictionary(trie, in);
}
