// Console program: a Ukrainian dictionary in a trie with autocomplete.
//
// Usage: trie_app [dictionary.txt]
// Without an argument it looks for data/uk_words.txt in the current directory
// and one or two levels up (so it works from the project root and from build/).
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "Benchmark.h"
#include "Dictionary.h"
#include "Trie.h"
#include "Utf8.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double usSince(Clock::time_point t) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t).count();
}

std::string findDefaultDictionary() {
    for (const char* p : {"data/uk_words.txt", "../data/uk_words.txt", "../../data/uk_words.txt"}) {
        std::ifstream f(p);

        if (f) {
            return p;
        }
    }

    return {};
}

// Reads one line; false on end of input.
bool readLine(const char* prompt, std::string& out) {
    std::cout << prompt;

    if (!std::getline(std::cin, out)) {
        return false;
    }

    if (!out.empty() && out.back() == '\r') {
        out.pop_back();
    }

    return true;
}

void load(Trie& trie, const std::string& path) {
    try {
        const LoadStats st = loadDictionaryFile(trie, path);
        std::cout << "Завантажено " << path << ": рядків " << st.lines << ", нових слів "
                  << st.inserted << ", дублікатів " << st.duplicates << ", некоректних "
                  << st.invalid << ", час " << st.ms << " мс\n"
                  << "У дереві: слів " << trie.size() << ", вузлів " << trie.nodeCount() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Помилка: " << e.what() << "\n";
    }
}

void autocompleteMenu(const Trie& trie) {
    std::string prefix;

    if (!readLine("Префікс: ", prefix)) {
        return;
    }

    if (uk::normalizePrefix(prefix).empty() &&
        prefix.find_first_not_of(" \t") != std::string::npos) {
        std::cout << "Некоректний префікс (дозволено лише українські літери, ' та -)\n";
        return;
    }

    auto t = Clock::now();
    const auto fast = trie.autocomplete(prefix);
    const double fastUs = usSince(t);

    Trie::QueryStats st;
    t = Clock::now();
    const auto bfs = trie.autocompleteBfs(prefix, Trie::kTop, &st);
    const double bfsUs = usSince(t);

    if (fast.empty()) {
        std::cout << "Слів з префіксом \"" << prefix << "\" немає\n";
    } else {
        std::cout << "Топ-" << fast.size() << " найкоротших слів на \"" << prefix << "\":\n";

        for (std::size_t i = 0; i < fast.size(); ++i) {
            std::cout << "  " << i + 1 << ". " << fast[i] << " (" << utf8::decode(fast[i]).size()
                      << " літ.)\n";
        }
    }

    std::cout << "Час: кеш у вузлах " << fastUs << " мкс; пошук у ширину " << bfsUs << " мкс ("
              << st.nodesVisited << " вузлів)" << (fast == bfs ? "" : "  [РЕЗУЛЬТАТИ РІЗНІ!]")
              << "\n";
}

void searchMenu(const Trie& trie) {
    std::string w;

    if (!readLine("Слово: ", w)) {
        return;
    }

    const auto t = Clock::now();
    const bool found = trie.contains(w);
    const double us = usSince(t);
    std::cout << (found ? "Слово є у словнику" : "Слова немає у словнику") << " (" << us
              << " мкс)\n";
    if (!found && trie.hasPrefix(w)) {
        std::cout << "Але є слова, що з нього починаються\n";
    }
}

void addMenu(Trie& trie) {
    std::string w;

    if (!readLine("Нове слово: ", w)) {
        return;
    }

    if (uk::normalizeWord(w).empty()) {
        std::cout << "Некоректне слово (дозволено лише українські літери, ' та - між ними)\n";
        return;
    }

    const auto t = Clock::now();
    const bool ok = trie.insert(w);
    const double us = usSince(t);
    std::cout << (ok ? "Додано" : "Таке слово вже є") << " (" << us << " мкс)\n";
}

void removeMenu(Trie& trie) {
    std::string w;

    if (!readLine("Слово для видалення: ", w)) {
        return;
    }

    const auto t = Clock::now();
    const bool ok = trie.remove(w);
    const double us = usSince(t);
    std::cout << (ok ? "Видалено" : "Такого слова немає") << " (" << us << " мкс)\n";
}

}   // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "Префіксне дерево (Trie): словник і автодоповнення\n";
    Trie trie;
    const std::string path = argc > 1 ? argv[1] : findDefaultDictionary();

    if (path.empty()) {
        std::cout
            << "Файл data/uk_words.txt не знайдено, словник порожній (пункт 5 завантажує файл)\n";
    } else {
        load(trie, path);
    }

    while (true) {
        std::cout << "\n1 - автодоповнення (топ-5 найкоротших слів за префіксом)\n"
                     "2 - пошук слова\n"
                     "3 - додати слово\n"
                     "4 - видалити слово\n"
                     "5 - завантажити слова з файлу\n"
                     "6 - статистика\n"
                     "7 - бенчмарк (кеш vs пошук у ширину vs перебір)\n"
                     "0 - вихід\n> ";
        std::string choice;

        if (!std::getline(std::cin, choice)) {
            return 0;
        }

        if (!choice.empty() && choice.back() == '\r') {
            choice.pop_back();
        }

        if (choice == "1") {
            autocompleteMenu(trie);
        } else if (choice == "2") {
            searchMenu(trie);
        } else if (choice == "3") {
            addMenu(trie);
        } else if (choice == "4") {
            removeMenu(trie);
        } else if (choice == "5") {
            std::string p;

            if (readLine("Шлях до файлу: ", p)) {
                load(trie, p);
            }
        } else if (choice == "6") {
            std::cout << "Слів: " << trie.size() << ", вузлів: " << trie.nodeCount() << "\n";
        } else if (choice == "7") {
            runBenchmark(trie, std::cout);
        } else if (choice == "0") {
            return 0;
        } else if (!choice.empty()) {
            std::cout << "Невідомий пункт\n";
        }
    }
}
