// Code snippets: a keyword plus Tab expands into a template with fields to fill in.
// Plain data and string processing (no OS calls), so the host tests can check it.
//
// Body syntax (as in snipMate / VS Code):
//   ${1:default}  field 1 with default text      ${1}  $1   field 1, empty
//   ${0}  $0      where the cursor ends up        \$  \}  \\  a literal $, } or backslash
// A field number used again is a mirror: it gets the field's text when Tab leaves the field.
// A leading tab in a body line is one indent step; every line after the first also gets the
// indentation of the line the snippet was typed on.
//
// User snippets (ENVARC:AmiForge/Snippets) use the snipMate file format:
//   # a comment
//   snippet name  Optional description
//   <tab>body line            (a tab, or up to four spaces, starts each body line)
//   <tab>body line
// A user snippet with a built-in snippet's name replaces it.
#pragma once

#include <string>
#include <vector>
#include <stddef.h>

namespace af {

struct Snippet {
    std::string name;
    std::string description;
    std::string body;
};

struct SnippetField {
    int number;          // 1.. ; 0 = the final cursor position
    size_t start, end;   // in the expanded text (byte offsets, end exclusive)
    bool mirror;         // a later use of a number: copies the first one's text
};

struct SnippetExpansion {
    std::string text;
    std::vector<SnippetField> fields;   // in text order
    // the fields to visit with Tab, in order (indices into fields): 1, 2, ... then 0 (if any)
    std::vector<size_t> tabOrder() const;
};

const std::vector<Snippet> &builtinSnippets();
std::vector<Snippet> parseSnippets(const std::string &fileText);
// builtins with user snippets added (a user snippet with the same name replaces the builtin)
std::vector<Snippet> mergeSnippets(const std::vector<Snippet> &base, const std::vector<Snippet> &user);
const Snippet *findSnippet(const std::vector<Snippet> &list, const std::string &name);

SnippetExpansion expandSnippet(const std::string &body, const std::string &lineIndent,
                               const std::string &indentUnit = "    ");

// Keep field positions right while the text changes: `length` bytes were inserted at `pos`
// (inserted = true) or deleted from `pos`. Text typed at a field's edge goes into the field.
void adjustSnippetFields(std::vector<SnippetField> &fields, size_t pos, size_t length, bool inserted);

} // namespace af
