#!/usr/bin/env python3
"""Builds data/uk_words.txt: a list of unique Ukrainian words (lemmas).

Source: brown-uk/dict_uk, release asset dict_corp_vis.txt.bz2 (GPL-3.0),
https://github.com/brown-uk/dict_uk/releases
Format: a lemma line "word tags  # comment", then its word forms indented by two spaces.

We keep only lemmas (non-indented lines) that
  - are written in lowercase Ukrainian letters, optionally joined by ' or -,
  - are not proper names, abbreviations, rude/slang, rare, archaic or misspelled words.

Usage: python3 tools/prepare_dictionary.py dict_corp_vis.txt data/uk_words.txt
"""
import re
import sys

LETTERS = "абвгґдеєжзиіїйклмнопрстуфхцчшщьюя"
UK_WORD = re.compile(rf"^[{LETTERS}]+(?:['-][{LETTERS}]+)*$")
SKIP_TAGS = {"prop", "abbr", "bad", "slang", "rare", "arch", "obsc", "alt", "short", "subst"}


def main(src: str, dst: str) -> None:
    seen = set()
    with open(src, encoding="utf-8") as f:
        for line in f:
            if line.startswith(" ") or not line.strip():
                continue
            parts = line.split("#", 1)[0].split()
            if len(parts) < 2:
                continue
            word, tags = parts[0], set(parts[1].split(":"))
            if tags & SKIP_TAGS:
                continue
            word = word.replace("’", "'").replace("ʼ", "'")
            if UK_WORD.match(word):
                seen.add(word)
    words = sorted(seen)
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(words) + "\n")
    print(f"{len(words)} words written to {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
