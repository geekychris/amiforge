// --- lang --- Lexers for ARexx and AmigaDOS scripts (see LexAmiga.h).
//
// ARexx: /* */ comments nest (the depth at the end of each line is kept in the line state),
// strings use ' or " with the quote doubled inside, words are case-insensitive, and a symbol
// followed by ':' at the start of a clause is a label. DO/SELECT ... END fold.
//
// AmigaDOS: ';' starts a comment, "..." strings with '*' escapes, the first word of a line is
// the command, .key/.bra/.ket/.def lines are directives, <name> is a script parameter and $name
// a variable. If ... EndIf fold.
#include <cassert>
#include <cstring>
#include <cctype>
#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

#include "sci/LexAmiga.h"

using namespace Lexilla;
using namespace af;

// Lexilla has no ids for these languages: numbers above SCLEX_AUTOMATIC (1000) are free.

namespace {

// ---------------------------------------------------------------------------------------
// ARexx
// ---------------------------------------------------------------------------------------

bool RexxWordStart(int ch) {
    return ch < 0x80 && (std::isalpha(ch) || ch == '_' || ch == '!' || ch == '?' || ch == '$' || ch == '@' || ch == '#');
}

bool RexxWordChar(int ch) {
    return ch < 0x80 && (std::isalnum(ch) || ch == '_' || ch == '!' || ch == '?' || ch == '$' || ch == '@' ||
                         ch == '#' || ch == '.');
}

bool RexxOperator(int ch) {
    return ch && ch < 0x80 && std::strchr("+-*/%|&=<>\\^~(),;:", ch);
}

char NextNonBlank(Accessor &styler, Sci_Position pos) {
    char c;
    while ((c = styler.SafeGetCharAt(pos)) == ' ' || c == '\t')
        pos++;
    return c;
}

void ColouriseARexx(Sci_PositionU startPos, Sci_Position length, int initStyle,
                    WordList *keywordlists[], Accessor &styler) {
    const WordList &instructions = *keywordlists[0];
    const WordList &builtins = *keywordlists[1];
    const WordList &specials = *keywordlists[2];

    Sci_Position line = styler.GetLine(startPos);
    int depth = line > 0 ? styler.GetLineState(line - 1) : 0;
    if (initStyle != AF_REXX_COMMENT) {
        initStyle = AF_REXX_DEFAULT;
        depth = 0;
    } else if (depth <= 0) {
        depth = 1;
    }
    StyleContext sc(startPos, length, initStyle, styler);
    bool clauseStart = true;     // a symbol here followed by ':' is a label
    char quote = '\'';
    std::string prevWord;

    for (; sc.More(); sc.Forward()) {
        if (sc.atLineStart) {
            clauseStart = true;
            if (sc.state != AF_REXX_COMMENT)
                sc.SetState(AF_REXX_DEFAULT);
        }
        switch (sc.state) {
        case AF_REXX_OPERATOR:
            sc.SetState(AF_REXX_DEFAULT);
            break;
        case AF_REXX_NUMBER:
            if (!(sc.ch < 0x80 && (std::isalnum(sc.ch) || sc.ch == '.')))
                sc.SetState(AF_REXX_DEFAULT);
            break;
        case AF_REXX_IDENTIFIER:
            if (!RexxWordChar(sc.ch)) {
                char s[64];
                sc.GetCurrentLowered(s, sizeof(s));
                if (clauseStart && NextNonBlank(styler, sc.currentPos) == ':') {
                    sc.ChangeState(AF_REXX_LABEL);
                } else {
                    clauseStart = false;
                    if (instructions.InList(s))
                        sc.ChangeState(AF_REXX_KEYWORD);
                    else if (builtins.InList(s) && (NextNonBlank(styler, sc.currentPos) == '(' || prevWord == "call"))
                        sc.ChangeState(AF_REXX_BUILTIN);
                    else if (specials.InList(s))
                        sc.ChangeState(AF_REXX_SPECIAL);
                }
                prevWord = s;
                sc.SetState(AF_REXX_DEFAULT);
            }
            break;
        case AF_REXX_COMMENT:
            if (sc.Match('/', '*')) {
                depth++;
                sc.Forward();
            } else if (sc.Match('*', '/')) {
                sc.Forward();
                if (--depth <= 0) {
                    depth = 0;
                    sc.ForwardSetState(AF_REXX_DEFAULT);
                }
            }
            break;
        case AF_REXX_STRING:
            if (sc.ch == quote) {
                if (sc.chNext == quote)
                    sc.Forward();               // '' inside '...' is a quote
                else
                    sc.ForwardSetState(AF_REXX_DEFAULT);
            } else if (sc.atLineEnd) {
                sc.ChangeState(AF_REXX_STRINGEOL);
            }
            break;
        default:
            break;
        }

        if (sc.state == AF_REXX_DEFAULT) {
            if (sc.Match('/', '*')) {
                sc.SetState(AF_REXX_COMMENT);
                depth = 1;
                sc.Forward();
            } else if (IsADigit(sc.ch) || (sc.ch == '.' && IsADigit(sc.chNext))) {
                sc.SetState(AF_REXX_NUMBER);
                clauseStart = false;
            } else if (RexxWordStart(sc.ch)) {
                sc.SetState(AF_REXX_IDENTIFIER);
            } else if (sc.ch == '\'' || sc.ch == '"') {
                quote = static_cast<char>(sc.ch);
                sc.SetState(AF_REXX_STRING);
                clauseStart = false;
            } else if (RexxOperator(sc.ch)) {
                sc.SetState(AF_REXX_OPERATOR);
                if (sc.ch == ';' || sc.ch == ':')
                    clauseStart = true;         // a new clause after ';' or a label
                else
                    clauseStart = false;
            }
        }
        if (sc.atLineEnd)
            styler.SetLineState(sc.currentLine, sc.state == AF_REXX_COMMENT ? depth : 0);
    }
    sc.Complete();
}

// Fold levels from words in one style: `opens`/`closes` say what a word does.
template <typename Opens, typename Closes>
void FoldByWords(Sci_PositionU startPos, Sci_Position length, Accessor &styler, int wordStyle,
                 Opens opens, Closes closes) {
    const Sci_PositionU endPos = startPos + length;
    Sci_Position lineCurrent = styler.GetLine(startPos);
    int levelCurrent = SC_FOLDLEVELBASE;
    if (lineCurrent > 0)
        levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
    int levelNext = levelCurrent;
    std::string word;
    auto flush = [&]() {
        if (word.empty())
            return;
        if (opens(word))
            levelNext++;
        else if (closes(word))
            levelNext--;
        word.clear();
    };
    for (Sci_PositionU i = startPos; i < endPos; i++) {
        const char ch = styler.SafeGetCharAt(i);
        const char chNext = styler.SafeGetCharAt(i + 1);
        if (styler.StyleAt(i) == wordStyle && ch > 0 && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        else
            flush();
        const bool atEOL = (ch == '\r' && chNext != '\n') || ch == '\n' || i == endPos - 1;
        if (atEOL) {
            flush();
            if (levelNext < SC_FOLDLEVELBASE)
                levelNext = SC_FOLDLEVELBASE;
            int lev = levelCurrent | (levelNext << 16);
            if (levelNext > levelCurrent)
                lev |= SC_FOLDLEVELHEADERFLAG;
            if (lev != styler.LevelAt(lineCurrent))
                styler.SetLevel(lineCurrent, lev);
            lineCurrent++;
            levelCurrent = levelNext;
        }
    }
}

void FoldARexx(Sci_PositionU startPos, Sci_Position length, int, WordList *[], Accessor &styler) {
    FoldByWords(startPos, length, styler, AF_REXX_KEYWORD,
                [](const std::string &w) { return w == "do" || w == "select"; },
                [](const std::string &w) { return w == "end"; });
}

const char *const arexxWordLists[] = { "Instructions", "Built-in functions", "Special variables", nullptr };

// ---------------------------------------------------------------------------------------
// AmigaDOS scripts
// ---------------------------------------------------------------------------------------

bool DosWordChar(int ch) {
    return ch < 0x80 && (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.');
}

void ColouriseAmigaDOS(Sci_PositionU startPos, Sci_Position length, int initStyle,
                       WordList *keywordlists[], Accessor &styler) {
    const WordList &commands = *keywordlists[0];
    (void)initStyle;
    // nothing continues onto the next line
    StyleContext sc(startPos, length, AF_DOS_DEFAULT, styler);
    bool firstWord = true;
    bool labelNext = false;     // after Lab / Skip

    for (; sc.More(); sc.Forward()) {
        if (sc.atLineStart) {
            firstWord = true;
            labelNext = false;
            sc.SetState(AF_DOS_DEFAULT);
        }
        switch (sc.state) {
        case AF_DOS_OPERATOR:
            sc.SetState(AF_DOS_DEFAULT);
            break;
        case AF_DOS_NUMBER:
            if (!(sc.ch < 0x80 && std::isalnum(sc.ch)))
                sc.SetState(AF_DOS_DEFAULT);
            break;
        case AF_DOS_DIRECTIVE:
            if (!(sc.ch < 0x80 && std::isalpha(sc.ch)))
                sc.SetState(AF_DOS_DEFAULT);
            break;
        case AF_DOS_IDENTIFIER:
            if (!DosWordChar(sc.ch) && sc.ch != ':' && sc.ch != '/') {
                char s[64];
                sc.GetCurrentLowered(s, sizeof(s));
                if (labelNext) {
                    sc.ChangeState(AF_DOS_LABEL);
                    labelNext = false;
                } else if (firstWord) {
                    // "C:Copy" and "SYS:C/Copy" are the command too
                    const char *base = s;
                    for (const char *p = s; *p; p++)
                        if (*p == ':' || *p == '/') base = p + 1;
                    if (commands.InList(base))
                        sc.ChangeState(AF_DOS_COMMAND);
                    labelNext = std::strcmp(base, "lab") == 0 || std::strcmp(base, "skip") == 0;
                }
                firstWord = false;
                sc.SetState(AF_DOS_DEFAULT);
            }
            break;
        case AF_DOS_STRING:
            if (sc.ch == '*') {
                sc.Forward();                   // *" *N *E **
            } else if (sc.ch == '"') {
                sc.ForwardSetState(AF_DOS_DEFAULT);
            }
            break;
        case AF_DOS_PARAM:
            if (sc.ch == '>') {
                sc.ForwardSetState(AF_DOS_DEFAULT);
            } else if (!(DosWordChar(sc.ch) || sc.ch == '$' || sc.ch == '=' || sc.ch == '"' || sc.ch == ' ')) {
                sc.SetState(AF_DOS_DEFAULT);
            }
            break;
        case AF_DOS_VARIABLE:
            if (sc.ch == '}') {
                sc.ForwardSetState(AF_DOS_DEFAULT);
            } else if (!(DosWordChar(sc.ch) || sc.ch == '{')) {
                sc.SetState(AF_DOS_DEFAULT);
            }
            break;
        default:
            break;
        }

        if (sc.state == AF_DOS_DEFAULT) {
            if (sc.ch == ';') {
                sc.SetState(AF_DOS_COMMENT);
            } else if (sc.ch == '.' && firstWord && sc.chNext < 0x80 && std::isalpha(sc.chNext)) {
                sc.SetState(AF_DOS_DIRECTIVE);
                firstWord = false;
            } else if (sc.ch == '"') {
                sc.SetState(AF_DOS_STRING);
                firstWord = false;
            } else if (sc.ch == '$' && (sc.chNext == '{' || (sc.chNext < 0x80 && std::isalpha(sc.chNext)))) {
                sc.SetState(AF_DOS_VARIABLE);
                firstWord = false;
            } else if (sc.ch == '<' && sc.chNext < 0x80 && (std::isalpha(sc.chNext) || sc.chNext == '$')) {
                // <name> parameter, but "<file" (input redirection) is an operator
                Sci_Position p = sc.currentPos + 1;
                char c;
                while ((c = styler.SafeGetCharAt(p)) != '>' && c != '\n' && c != '\r' && c != 0 && p - sc.currentPos < 40)
                    p++;
                if (c == '>') {
                    sc.SetState(AF_DOS_PARAM);
                } else {
                    sc.SetState(AF_DOS_OPERATOR);
                }
                firstWord = false;
            } else if (IsADigit(sc.ch) && !firstWord) {
                sc.SetState(AF_DOS_NUMBER);
            } else if (DosWordChar(sc.ch)) {
                sc.SetState(AF_DOS_IDENTIFIER);
            } else if (sc.ch && sc.ch < 0x80 && std::strchr("<>|`*#?=+", sc.ch)) {
                sc.SetState(AF_DOS_OPERATOR);
                firstWord = false;
            }
        }
    }
    sc.Complete();
}

void FoldAmigaDOS(Sci_PositionU startPos, Sci_Position length, int, WordList *[], Accessor &styler) {
    FoldByWords(startPos, length, styler, AF_DOS_COMMAND,
                [](const std::string &w) { return w == "if"; },
                [](const std::string &w) { return w == "endif"; });
}

const char *const amigaDosWordLists[] = { "Commands", nullptr };

} // namespace

extern const LexerModule lmARexx(1100, ColouriseARexx, "arexx", FoldARexx, arexxWordLists);
extern const LexerModule lmAmigaDOS(1101, ColouriseAmigaDOS, "amigados", FoldAmigaDOS, amigaDosWordLists);
