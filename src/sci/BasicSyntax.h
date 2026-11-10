// --- basic --- Boing BASIC (a Visual Basic 6 dialect) line syntax: comments and strings, which
// block a line opens or closes (Sub ... End Sub, If ... Then ... End If, For ... Next, Select
// Case ... End Select...), and the indentation that follows from it. Plain string processing,
// shared by the lexer's folding (LexBoing.cpp), the editor's auto-indent and the IDE's
// code-behind model (BasicModel.h); the host tests check it.
#pragma once

#include <string>
#include <vector>

namespace af {
namespace basic {

enum class Block { None, Sub, Function, Property, If, For, Do, While, Select, With, Type, Enum };

struct LineInfo {
    Block opens = Block::None;    // the line starts a block ("Private Sub x()", "For i = 1 To 3", "If a Then")
    Block closes = Block::None;   // the line ends one ("End Sub", "Next", "Loop Until x", "Wend")
    Block middle = Block::None;   // Else / ElseIf (If), Case (Select): ends one part, starts the next
};

// The code part of a line: without its comment (' or Rem, outside "strings") and trailing blanks.
std::string stripComment(const std::string &line);
// The line with "strings" blanked out (same length; the quotes stay), comment removed.
std::string maskStrings(const std::string &line);
// Lower-case words of the code part, after a leading line number or label ("10", "retry:").
std::vector<std::string> codeWords(const std::string &line);

LineInfo classify(const std::string &line);

// Auto-indent: the change in levels for the line after `line` (+1 after a line that opens a
// block or is an Else/Case, else 0).
int indentChangeAfter(const std::string &line);
// The indentation (in columns) a closing or middle line (End If, Next, Else, Case, End Select)
// should have, from the lines before it (`before[i]` is the text of line i, `indents[i]` its
// indentation, the last one is the line just above): the indentation of the line that opened
// its block (Case: one level more than Select Case). -1 if `line` doesn't close anything or
// no opener is found.
int closerIndent(const std::string &line, const std::vector<std::string> &before,
                 const std::vector<int> &indents, int indentSize);

// The keyword lists (space-separated, lower case): 0 statements and operators, 1 built-in
// functions and objects, 2 type names.
const char *keywords(int set);
bool isKeyword(const std::string &word);    // any case; sets 0 and 2

} // namespace basic
} // namespace af
