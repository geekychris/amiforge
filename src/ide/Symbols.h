// Source symbols without a compiler: the outline of a file (functions, types, macros, globals,
// assembler labels), and whole-word identifier search/replace that skips comments and strings
// (Find All References, Rename Symbol). String processing only, so the host tests can check it.
#pragma once

#include <string>
#include <vector>
#include <stddef.h>

namespace af {

enum class SymKind { Function, Struct, Class, Union, Enum, Namespace, Typedef, Define, Global, Label, Macro };

struct Symbol {
    SymKind kind;
    std::string name;      // "main", "Ball::step", "MAX_BALLS"
    int line;              // 1-based
    int depth = 0;         // nesting (members of a class/namespace are 1 deeper)
    std::string detail;    // the parameter list of a function, "global" for an exported label...
};

const char *symKindName(SymKind k);           // "function", "struct"...
// assembler: .s/.S/.asm/.i files
std::vector<Symbol> outlineSymbols(const std::string &text, bool assembler);

// Comments, string and character literals (and #include file names) replaced by spaces; the
// length and the newlines stay the same, so offsets carry over to the original text.
std::string maskCode(const std::string &text, bool assembler);

bool isIdentifierName(const std::string &s);
bool isReservedWord(const std::string &s);    // C/C++ keywords: can't be a rename target

// Offsets of whole-word uses of `name` outside comments and strings.
std::vector<size_t> findIdentifier(const std::string &text, const std::string &name, bool assembler);
// The same occurrences replaced by newName; *count gets how many.
std::string replaceIdentifier(const std::string &text, const std::string &name, const std::string &newName,
                              bool assembler, int *count = nullptr);

// 1-based line and column of an offset.
void lineColumn(const std::string &text, size_t offset, int &line, int &col);

} // namespace af
