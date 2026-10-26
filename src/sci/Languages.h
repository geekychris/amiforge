// --- lang --- The languages the editor knows: which one a file is, its comment marks and its
// indentation rules. Plain data and string processing (no OS or Scintilla calls), so the host
// tests can check it. sciSetupSourceEditor() picks the lexer and the styles from languageFor().
#pragma once

#include <string>

namespace af {

enum class Language {
    Text, C, Asm, Make, Props, Diff,
    Python, Lua, Perl, Shell, AmigaDOS, ARexx, JSON, YAML, Markdown,
};

// The language of a file, by its name; `firstLine` (optional) catches "#!/usr/bin/env python3",
// "/* ARexx */"-less scripts with no extension, and AmigaDOS scripts (".key", ".bra" or a file
// in S:). A bare language name ("python", "c") also works, for documents that only have a title.
Language languageFor(const std::string &fileName, const std::string &firstLine = "");

const char *languageName(Language l);      // "Python", "C/C++", "ARexx"...
const char *languageId(Language l);        // "python", "c", "arexx"... (snippets, settings)
Language languageFromId(const std::string &id);   // Text if unknown

// Comment marks for Comment/Uncomment Lines: a line comment ("#", "//", "--", ";"), or empty
// when the language only has block comments (then blockStart/blockEnd, e.g. ARexx "/* */").
struct CommentStyle {
    const char *line;
    const char *blockStart, *blockEnd;
};
CommentStyle commentStyle(Language l);

enum class IndentRule {
    Braces,     // one level more after a line ending in '{'; a '}' typed first is outdented
    Colon,      // Python: one level more after ':', one less after return/pass/break/continue/raise
    Keywords,   // Lua, ARexx, AmigaDOS: one level more after a line opening a block (then, do...)
    Keep,       // just keep the previous line's indentation
};
IndentRule indentRule(Language l);
int defaultIndentWidth(Language l);        // 4; 2 for YAML, JSON and Lua
bool usesTabs(Language l);                 // makefiles
bool foldable(Language l);                 // shows the fold margin

// For IndentRule::Colon/Keywords: the indentation change (in levels: +1, 0, -1) after a line.
// `line` is the previous line's text (comments are ignored).
int indentChangeAfter(Language l, const std::string &line);

// Keyword lists for the lexers and for completion (space-separated). Set 0: keywords;
// 1: built-in functions/types; 2: more (ARexx special variables, Lua's standard library). "" if none.
const char *languageKeywords(Language l, int set);

// The code part of a line: without a trailing comment (outside strings) and trailing blanks.
std::string stripLineComment(Language l, const std::string &line);

} // namespace af
