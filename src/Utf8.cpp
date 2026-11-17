#include "Utf8.h"

#include <stdexcept>

namespace utf8 {

std::u32string decode(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    std::size_t i = 0;

    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        int extra = 0;
        char32_t cp = 0;

        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            extra = 3;
        } else {
            throw std::invalid_argument("utf8::decode: invalid lead byte");
        }

        if (i + static_cast<std::size_t>(extra) >= s.size() && extra > 0) {
            throw std::invalid_argument("utf8::decode: truncated sequence");
        }

        for (int k = 1; k <= extra; ++k) {
            const unsigned char c = static_cast<unsigned char>(s[i + k]);

            if ((c & 0xC0) != 0x80) {
                throw std::invalid_argument("utf8::decode: bad continuation byte");
            }

            cp = (cp << 6) | (c & 0x3F);
        }

        // Reject overlong forms, surrogates and values above U+10FFFF.
        static const char32_t minValue[] = {0, 0x80, 0x800, 0x10000};

        if (cp < minValue[extra] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            throw std::invalid_argument("utf8::decode: invalid code point");
        }

        out.push_back(cp);
        i += static_cast<std::size_t>(extra) + 1;
    }

    return out;
}

std::string encode(const std::u32string& s) {
    std::string out;
    out.reserve(s.size() * 2);

    for (char32_t cp : s) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return out;
}

}   // namespace utf8

namespace uk {

namespace {

const char32_t kAlphabet[] = U"абвгґдеєжзиіїйклмнопрстуфхцчшщьюя";
constexpr int kAlphabetSize = 33;

bool isUkLetter(char32_t c) {
    // Every Ukrainian letter is in U+0430..U+0491; ranks 2..34 are the alphabet.
    if (c < 0x430 || c > 0x491) {
        return false;
    }

    const int r = letterRank(c);
    return r >= 2 && r < 2 + kAlphabetSize;
}

bool isJoiner(char32_t c) {
    return c == U'\'' || c == U'-';
}

// Lower case + apostrophe unification + validation. allowPrefix permits an empty
// result and a trailing joiner.
std::u32string normalize(const std::string& text, bool allowPrefix) {
    std::u32string s;

    try {
        s = utf8::decode(text);
    } catch (const std::invalid_argument&) {
        return {};
    }

    // Trim spaces, tabs and \r (Windows line endings).
    std::size_t b = 0, e = s.size();

    while (b < e && (s[b] == U' ' || s[b] == U'\t' || s[b] == U'\r' || s[b] == U'\n')) {
        ++b;
    }
    while (e > b &&
           (s[e - 1] == U' ' || s[e - 1] == U'\t' || s[e - 1] == U'\r' || s[e - 1] == U'\n')) {
        --e;
    }

    std::u32string out;
    out.reserve(e - b);

    for (std::size_t i = b; i < e; ++i) {
        char32_t c = toLower(s[i]);

        if (c == 0x2019 || c == 0x02BC) {
            c = U'\'';
        }

        if (isUkLetter(c)) {
            out.push_back(c);
        } else if (isJoiner(c)) {
            // A joiner must follow a letter.
            if (out.empty() || isJoiner(out.back())) {
                return {};
            }

            out.push_back(c);
        } else {
            return {};
        }
    }

    if (!allowPrefix && !out.empty() && isJoiner(out.back())) {
        return {};
    }

    return out;
}

}   // namespace

int letterRank(char32_t c) {
    // Lookup table for U+0400..U+049F (Cyrillic block with all Ukrainian letters):
    // O(1) instead of a search in the alphabet, since this runs in every comparison.
    struct Table {
        int rank[0xA0];
        Table() {
            for (int i = 0; i < 0xA0; ++i) {
                rank[i] = 2 + kAlphabetSize + 0x400 + i;
            }

            for (int i = 0; i < kAlphabetSize; ++i) {
                rank[kAlphabet[i] - 0x400] = 2 + i;
            }
        }
    };

    static const Table table;

    if (c >= 0x400 && c < 0x4A0) {
        return table.rank[c - 0x400];
    }

    if (c == U'\'') {
        return 0;
    }

    if (c == U'-') {
        return 1;
    }

    return 2 + kAlphabetSize + static_cast<int>(c);
}

bool alphabeticLess(const std::u32string& a, const std::u32string& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();

    for (std::size_t i = 0; i < n; ++i) {
        const int ra = letterRank(a[i]);
        const int rb = letterRank(b[i]);

        if (ra != rb) {
            return ra < rb;
        }
    }

    return a.size() < b.size();
}

char32_t toLower(char32_t c) {
    if (c >= U'A' && c <= U'Z') {
        return c + 0x20;
    }

    if (c >= 0x0410 && c <= 0x042F) {
        return c + 0x20;   // А..Я -> а..я
    }

    switch (c) {
        case 0x0490:
            return 0x0491;   // Ґ
        case 0x0404:
            return 0x0454;   // Є
        case 0x0406:
            return 0x0456;   // І
        case 0x0407:
            return 0x0457;   // Ї
        default:
            return c;
    }
}

std::u32string normalizeWord(const std::string& utf8Word) {
    return normalize(utf8Word, false);
}

std::u32string normalizePrefix(const std::string& utf8Prefix) {
    return normalize(utf8Prefix, true);
}

}   // namespace uk
