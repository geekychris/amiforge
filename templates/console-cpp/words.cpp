#include "words.h"
#include <algorithm>

void WordList::sort()
{
    std::sort(words.begin(), words.end());
}

std::string WordList::longest() const
{
    auto it = std::max_element(words.begin(), words.end(),
                               [](const std::string &a, const std::string &b) { return a.size() < b.size(); });
    return it == words.end() ? std::string() : *it;
}
