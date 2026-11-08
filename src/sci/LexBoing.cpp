// --- basic --- Lexer for Boing BASIC (see LexBoing.h).
//
// Everything is case-insensitive. ' starts a comment, and so does Rem at the start of a
// statement; strings are "..." with "" for a quote and end at the line's end. Numbers take
// &H/&O prefixes and the type suffixes % & ! # @. A word followed by ':' at the start of a line,
// or a leading line number, is a label. # at the start of a line is a preprocessor line;
// #...# around a date is a date literal. Folding works line by line with basic::classify().
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

#include "sci/LexBoing.h"
#include "sci/BasicSyntax.h"

using namespace Lexilla;
using namespace af;

namespace {

bool BasWordStart(int ch) { return ch >= 0x80 || std::isalpha(ch) || ch == '_'; }
bool BasWordChar(int ch) { return ch >= 0x80 || std::isalnum(ch) || ch == '_'; }
bool BasOperator(int ch) { return ch && ch < 0x80 && std::strchr("+-*/\\^&=<>(),.:;", ch); }

void ColouriseBoing(Sci_PositionU startPos, Sci_Position length, int, WordList *keywordlists[],
                    Accessor &styler) {
    const WordList &statements = *keywordlists[0];
    const WordList &builtins = *keywordlists[1];
    const WordList &types = *keywordlists[2];

    // nothing continues onto the next line: always start at a line start in the default state
    StyleContext sc(startPos, length, AF_BAS_DEFAULT, styler);
    bool lineStart = true;        // nothing but blanks so far on this line
    bool stmtStart = true;        // where Rem can start
    bool procNext = false;        // after Sub / Function / Property Get
    std::string prevWord;

    for (; sc.More(); sc.Forward()) {
        if (sc.atLineStart) {
            lineStart = stmtStart = true;
            procNext = false;
            prevWord.clear();
            sc.SetState(AF_BAS_DEFAULT);
        }
        switch (sc.state) {
        case AF_BAS_OPERATOR:
            sc.SetState(AF_BAS_DEFAULT);
            break;
        case AF_BAS_NUMBER:
            if (!(sc.ch < 0x80 && (std::isalnum(sc.ch) || sc.ch == '.'))) {
                if (sc.ch == '%' || sc.ch == '&' || sc.ch == '!' || sc.ch == '#' || sc.ch == '@')
                    sc.Forward();
                sc.SetState(AF_BAS_DEFAULT);
            }
            break;
        case AF_BAS_LABEL:
            if (!BasWordChar(sc.ch)) {
                if (sc.ch == ':') sc.Forward();
                sc.SetState(AF_BAS_DEFAULT);
                stmtStart = true;
            }
            break;
        case AF_BAS_IDENTIFIER:
            if (!BasWordChar(sc.ch)) {
                char s[64];
                sc.GetCurrentLowered(s, sizeof(s));
                const bool member = sc.LengthCurrent() < (Sci_Position)sizeof(s) &&
                                    styler.SafeGetCharAt(sc.currentPos - sc.LengthCurrent() - 1) == '.';
                if (std::strcmp(s, "rem") == 0 && stmtStart) {
                    sc.ChangeState(AF_BAS_COMMENT);
                    break;   // to the end of the line
                }
                // a type suffix belongs to the name: Left$, count%
                if (sc.ch == '$' || sc.ch == '%' || sc.ch == '&' || sc.ch == '!' || sc.ch == '#' || sc.ch == '@') {
                    const int n = sc.chNext;
                    if (!BasWordChar(n)) sc.Forward();
                }
                if (procNext) {
                    sc.ChangeState(AF_BAS_PROCNAME);
                    procNext = false;
                } else if (!member && statements.InList(s)) {
                    sc.ChangeState(AF_BAS_KEYWORD);
                    if (std::strcmp(s, "sub") == 0 || std::strcmp(s, "function") == 0)
                        procNext = prevWord != "end" && prevWord != "exit";
                    else if ((std::strcmp(s, "get") == 0 || std::strcmp(s, "let") == 0 || std::strcmp(s, "set") == 0) &&
                             prevWord == "property")
                        procNext = true;
                } else if (!member && types.InList(s)) {
                    sc.ChangeState(AF_BAS_TYPE);
                } else if (!member && builtins.InList(s)) {
                    sc.ChangeState(AF_BAS_BUILTIN);
                }
                prevWord = s;
                stmtStart = false;
                sc.SetState(AF_BAS_DEFAULT);
            }
            break;
        case AF_BAS_COMMENT:
        case AF_BAS_PREPROCESSOR:
            break;   // to the end of the line
        case AF_BAS_STRING:
            if (sc.ch == '"') {
                if (sc.chNext == '"') sc.Forward();
                else sc.ForwardSetState(AF_BAS_DEFAULT);
            } else if (sc.atLineEnd) {
                sc.ChangeState(AF_BAS_STRINGEOL);
            }
            break;
        case AF_BAS_DATE:
            if (sc.ch == '#') sc.ForwardSetState(AF_BAS_DEFAULT);
            else if (sc.atLineEnd) sc.SetState(AF_BAS_DEFAULT);
            break;
        default:
            break;
        }

        if (sc.state == AF_BAS_DEFAULT && !sc.atLineEnd) {
            if (sc.ch == '\'') {
                sc.SetState(AF_BAS_COMMENT);
            } else if (sc.ch == '"') {
                sc.SetState(AF_BAS_STRING);
                lineStart = stmtStart = false;
            } else if (sc.ch == '#' && lineStart) {
                sc.SetState(AF_BAS_PREPROCESSOR);
            } else if (sc.ch == '#' && IsADigit(sc.chNext) && prevWord != "" && !BasWordChar(styler.SafeGetCharAt(sc.currentPos - 1))) {
                // #1/2/2026# is a date; "#1" after Open/Print/Close is a file number
                Sci_Position e = sc.currentPos + 1;
                bool date = false;
                while (e < sc.currentPos + 30) {
                    const char c = styler.SafeGetCharAt(e);
                    if (c == '#') { date = true; break; }
                    if (!(IsADigit(c) || c == '/' || c == '-' || c == ':' || c == ' ' || std::isalpha((unsigned char)c))) break;
                    e++;
                }
                sc.SetState(date ? AF_BAS_DATE : AF_BAS_OPERATOR);
            } else if (IsADigit(sc.ch) || (sc.ch == '.' && IsADigit(sc.chNext))) {
                sc.SetState(lineStart ? AF_BAS_LABEL : AF_BAS_NUMBER);
                lineStart = false;
            } else if (sc.ch == '&' && (sc.chNext == 'h' || sc.chNext == 'H' || sc.chNext == 'o' || sc.chNext == 'O')) {
                sc.SetState(AF_BAS_NUMBER);
                sc.Forward();
                lineStart = stmtStart = false;
            } else if (BasWordStart(sc.ch)) {
                // name: at the start of a line is a label (but not "a := ..." or "x:=")
                if (lineStart) {
                    Sci_Position e = sc.currentPos;
                    while (BasWordChar(styler.SafeGetCharAt(e))) e++;
                    if (styler.SafeGetCharAt(e) == ':' && styler.SafeGetCharAt(e + 1) != '=') {
                        sc.SetState(AF_BAS_LABEL);
                        lineStart = false;
                        continue;
                    }
                }
                sc.SetState(AF_BAS_IDENTIFIER);
                lineStart = false;
            } else if (BasOperator(sc.ch)) {
                sc.SetState(AF_BAS_OPERATOR);
                lineStart = false;
                stmtStart = sc.ch == ':';
            } else if (sc.ch != ' ' && sc.ch != '\t') {
                lineStart = stmtStart = false;
            }
        }
    }
    sc.Complete();
}

void FoldBoing(Sci_PositionU startPos, Sci_Position length, int, WordList *[], Accessor &styler) {
    const Sci_PositionU endPos = startPos + length;
    Sci_Position lineCurrent = styler.GetLine(startPos);
    int levelCurrent = SC_FOLDLEVELBASE;
    if (lineCurrent > 0)
        levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
    std::string text;
    for (Sci_PositionU i = startPos; i < endPos; i++) {
        const char ch = styler.SafeGetCharAt(i);
        const char chNext = styler.SafeGetCharAt(i + 1);
        if (ch != '\r' && ch != '\n') text.push_back(ch);
        const bool atEOL = (ch == '\r' && chNext != '\n') || ch == '\n' || i == endPos - 1;
        if (!atEOL) continue;
        const basic::LineInfo li = basic::classify(text);
        text.clear();
        int levelNext = levelCurrent;
        if (li.opens != basic::Block::None) levelNext++;
        if (li.closes != basic::Block::None) levelNext--;
        if (levelNext < SC_FOLDLEVELBASE) levelNext = SC_FOLDLEVELBASE;
        int lev = levelCurrent | (levelNext << 16);
        if (levelNext > levelCurrent) lev |= SC_FOLDLEVELHEADERFLAG;
        if (lev != styler.LevelAt(lineCurrent)) styler.SetLevel(lineCurrent, lev);
        lineCurrent++;
        levelCurrent = levelNext;
    }
}

const char *const boingWordLists[] = { "Statements", "Built-in functions", "Types", nullptr };

} // namespace

extern const LexerModule lmBoingBasic(1102, ColouriseBoing, "boingbasic", FoldBoing, boingWordLists);
