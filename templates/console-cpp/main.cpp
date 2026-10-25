// ${NAME} - a Shell program in C++ for AmigaOS 4.1.
// ${DATE}, ${AUTHOR}
//
// Try: ${LOWER} banana apple cherry
#include <cstdio>
#include "words.h"

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

int main(int argc, char **argv)
{
    WordList list;
    for (int i = 1; i < argc; i++) list.add(argv[i]);
    if (list.size() == 0) {
        std::printf("Usage: %s word...\n", argv[0]);
        return 5;
    }
    list.sort();
    std::printf("%lu words, sorted:\n", (unsigned long)list.size());
    for (const auto &w : list.all()) std::printf("  %s\n", w.c_str());
    std::printf("The longest is \"%s\".\n", list.longest().c_str());
    return 0;
}
