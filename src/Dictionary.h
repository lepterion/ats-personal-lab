#pragma once
// Loading a word list into the trie.

#include <cstddef>
#include <iosfwd>
#include <string>

class Trie;

struct LoadStats {
    std::size_t lines = 0;        // non-empty lines read
    std::size_t inserted = 0;     // new words added
    std::size_t duplicates = 0;   // words that were already in the trie
    std::size_t invalid = 0;      // lines that are not a Ukrainian word
    double ms = 0.0;              // time to read and insert everything
};

// One word per line (the first whitespace-separated token is taken, so lists
// like "word 123" work too). UTF-8, LF or CRLF.
LoadStats loadDictionary(Trie& trie, std::istream& is);

// Same from a file. Throws std::runtime_error if the file cannot be opened.
LoadStats loadDictionaryFile(Trie& trie, const std::string& path);
