#pragma once
// UTF-8 <-> UTF-32 conversion and Ukrainian letter helpers.
//
// The trie works with Unicode code points (char32_t), not bytes: a Cyrillic
// letter takes 2 bytes in UTF-8 and the apostrophe takes 1, so "length of a
// word" must be counted in letters, otherwise "м'яч" (4 letters, 7 bytes) would
// look longer than "мати" (4 letters, 8 bytes) only by accident.

#include <string>

namespace utf8 {

// Decodes UTF-8. Throws std::invalid_argument on malformed input.
std::u32string decode(const std::string& s);

// Encodes code points as UTF-8.
std::string encode(const std::u32string& s);

}   // namespace utf8

namespace uk {

// Position of c in the Ukrainian alphabet order used for sorting:
// ' and - first, then а б в г ґ д е є ж з и і ї й к л м н о п р с т у ф х ц ч ш щ ь ю я,
// then everything else by code point. Plain code point order would be wrong:
// ґ, є, і, ї have codes after я (U+0491, U+0454, U+0456, U+0457).
int letterRank(char32_t c);

// Lexicographic "a < b" using letterRank.
bool alphabeticLess(const std::u32string& a, const std::u32string& b);

// Lower case for Ukrainian and Latin letters; other characters are kept.
char32_t toLower(char32_t c);

// Normalises a word for the dictionary: trims spaces, lower case, typographic
// apostrophes (U+2019, U+02BC) become '. Returns an empty string if the result
// is not a Ukrainian word (letters, optionally joined by single ' or -).
std::u32string normalizeWord(const std::string& utf8Word);

// Same as normalizeWord, but also allows an empty string and a trailing ' or -
// (a prefix typed by the user, e.g. "м'").
std::u32string normalizePrefix(const std::string& utf8Prefix);

}   // namespace uk
