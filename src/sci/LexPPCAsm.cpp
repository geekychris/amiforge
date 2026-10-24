// Lexer for PowerPC GNU as. Comments are '#' to end of line, /* */ and // (in .S files the
// C preprocessor removes those); a '#' starting a line followed by a preprocessor directive is
// a preprocessor line. The first word of a statement is a label (when ':' follows), a directive
// (".word") or an instruction; register names are recognised in the operands, with or without '%'.
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

#include "sci/LexPPCAsm.h"

using namespace Lexilla;
using namespace af;

namespace {

bool IsWordStart(int ch) {
    return ch < 0x80 && (std::isalpha(ch) || ch == '_' || ch == '.' || ch == '$');
}

bool IsWordChar(int ch) {
    return ch < 0x80 && (std::isalnum(ch) || ch == '_' || ch == '.' || ch == '$' || ch == '@');
}

bool IsOperator(int ch) {
    return ch < 0x80 && std::strchr("+-*/%()[],<>=&|^~!:;", ch) && ch;
}

// The next character after spaces and tabs from pos.
char NextNonBlank(Accessor &styler, Sci_Position pos) {
    char c;
    while ((c = styler.SafeGetCharAt(pos)) == ' ' || c == '\t')
        pos++;
    return c;
}

bool IsPreprocessorLine(Accessor &styler, Sci_Position hashPos) {
    static const char *const directives[] = {
        "include", "define", "undef", "if", "ifdef", "ifndef", "elif", "else", "endif",
        "error", "warning", "pragma", "line", nullptr
    };
    Sci_Position p = hashPos + 1;
    while (styler.SafeGetCharAt(p) == ' ' || styler.SafeGetCharAt(p) == '\t')
        p++;
    std::string word;
    while (word.size() < 10 && std::isalpha(static_cast<unsigned char>(styler.SafeGetCharAt(p))))
        word.push_back(styler.SafeGetCharAt(p++));
    for (const char *const *d = directives; *d; d++)
        if (word == *d)
            return true;
    return false;
}

void ColourisePPCAsm(Sci_PositionU startPos, Sci_Position length, int initStyle,
                     WordList *keywordlists[], Accessor &styler) {
    const WordList &instructions = *keywordlists[0];
    const WordList &registers = *keywordlists[1];
    const WordList &directives = *keywordlists[2];

    // Only block comments continue onto the next line.
    if (initStyle != AF_ASM_COMMENTBLOCK)
        initStyle = AF_ASM_DEFAULT;
    StyleContext sc(startPos, length, initStyle, styler);
    bool statementStart = true;    // the next word is a label, directive or instruction
    bool lineStart = true;         // only blanks so far on this line

    for (; sc.More(); sc.Forward()) {
        if (sc.atLineStart) {
            statementStart = true;
            lineStart = true;
            if (sc.state != AF_ASM_COMMENTBLOCK)
                sc.SetState(AF_ASM_DEFAULT);
        }

        switch (sc.state) {
        case AF_ASM_OPERATOR:
            sc.SetState(AF_ASM_DEFAULT);
            break;
        case AF_ASM_NUMBER:
            if (!IsWordChar(sc.ch)) {
                // "1:" is a local label.
                if (statementStart && NextNonBlank(styler, sc.currentPos) == ':')
                    sc.ChangeState(AF_ASM_LABEL);
                else
                    statementStart = false;
                sc.SetState(AF_ASM_DEFAULT);
            }
            break;
        case AF_ASM_IDENTIFIER:
            if (!IsWordChar(sc.ch)) {
                char s[64];
                sc.GetCurrentLowered(s, sizeof(s));
                const char *word = s[0] == '%' ? s + 1 : s;
                if (statementStart && NextNonBlank(styler, sc.currentPos) == ':') {
                    sc.ChangeState(AF_ASM_LABEL);
                } else if (statementStart) {
                    statementStart = false;
                    if (word[0] == '.') {
                        sc.ChangeState(AF_ASM_DIRECTIVE);
                    } else {
                        // "add." sets CR0; the dot is not part of the mnemonic.
                        std::string mnemonic(word);
                        if (!mnemonic.empty() && mnemonic.back() == '.')
                            mnemonic.pop_back();
                        if (instructions.InList(mnemonic.c_str()))
                            sc.ChangeState(AF_ASM_INSTRUCTION);
                    }
                } else if (registers.InList(word)) {
                    sc.ChangeState(AF_ASM_REGISTER);
                } else if (word[0] == '.' && directives.InList(word)) {
                    sc.ChangeState(AF_ASM_DIRECTIVE);
                }
                sc.SetState(AF_ASM_DEFAULT);
            }
            break;
        case AF_ASM_COMMENTBLOCK:
            if (sc.Match('*', '/')) {
                sc.Forward();
                sc.ForwardSetState(AF_ASM_DEFAULT);
            }
            break;
        case AF_ASM_STRING:
        case AF_ASM_CHARACTER: {
            const int quote = sc.state == AF_ASM_STRING ? '"' : '\'';
            if (sc.ch == '\\') {
                sc.Forward();
            } else if (sc.ch == quote) {
                sc.ForwardSetState(AF_ASM_DEFAULT);
            } else if (sc.atLineEnd) {
                sc.ChangeState(AF_ASM_STRINGEOL);
            }
            break;
        }
        default:
            break;
        }

        if (sc.state == AF_ASM_DEFAULT) {
            if (sc.Match('/', '*')) {
                sc.SetState(AF_ASM_COMMENTBLOCK);
                sc.Forward();
            } else if (sc.Match('/', '/')) {
                sc.SetState(AF_ASM_COMMENT);
            } else if (sc.ch == '#') {
                sc.SetState(lineStart && IsPreprocessorLine(styler, sc.currentPos) ? AF_ASM_PREPROCESSOR : AF_ASM_COMMENT);
            } else if (IsADigit(sc.ch)) {
                sc.SetState(AF_ASM_NUMBER);
            } else if (sc.ch == '%' && IsWordStart(sc.chNext)) {
                sc.SetState(AF_ASM_IDENTIFIER);
            } else if (IsWordStart(sc.ch)) {
                sc.SetState(AF_ASM_IDENTIFIER);
            } else if (sc.ch == '"') {
                sc.SetState(AF_ASM_STRING);
            } else if (sc.ch == '\'') {
                sc.SetState(AF_ASM_CHARACTER);
            } else if (IsOperator(sc.ch)) {
                sc.SetState(AF_ASM_OPERATOR);
                if (sc.ch == ';')
                    statementStart = true;     // statement separator
            }
        }
        if (!sc.atLineEnd && sc.ch != ' ' && sc.ch != '\t')
            lineStart = false;
    }
    sc.Complete();
}

const char *const ppcAsmWordLists[] = { "Instructions", "Registers", "Directives", nullptr };

} // namespace

extern const LexerModule lmPPCAsm(SCLEX_AS, ColourisePPCAsm, "ppcasm", nullptr, ppcAsmWordLists);
