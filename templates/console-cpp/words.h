// ${NAME}: a small word list class.
#pragma once
#include <string>
#include <vector>

class WordList {
public:
    void add(const std::string &w) { words.push_back(w); }
    void sort();
    std::string longest() const;
    size_t size() const { return words.size(); }
    const std::vector<std::string> &all() const { return words; }

private:
    std::vector<std::string> words;
};
