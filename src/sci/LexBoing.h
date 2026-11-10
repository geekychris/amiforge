// --- basic --- A Lexilla lexer for Boing BASIC (.bas, .cls): VB6 syntax, case-insensitive.
#pragma once

namespace Lexilla { class LexerModule; }

// Keyword sets: 0 statements (Dim, Sub, If...), 1 built-in functions (Left, MsgBox...),
// 2 type names (Integer, String...). Folds Sub/Function/Property and If/For/Do/While/Select/
// With/Type/Enum blocks (BasicSyntax.h decides what a line opens and closes).
extern const Lexilla::LexerModule lmBoingBasic;

namespace af {

enum {
    AF_BAS_DEFAULT = 0,
    AF_BAS_COMMENT = 1,        // ' ... and Rem ...
    AF_BAS_STRING = 2,         // "..." ("" inside is a quote)
    AF_BAS_NUMBER = 3,         // 12, 1.5E3, &HFF, &O17, with type suffixes
    AF_BAS_KEYWORD = 4,
    AF_BAS_BUILTIN = 5,
    AF_BAS_TYPE = 6,
    AF_BAS_OPERATOR = 7,
    AF_BAS_IDENTIFIER = 8,
    AF_BAS_STRINGEOL = 9,      // a string that isn't closed on its line
    AF_BAS_PREPROCESSOR = 10,  // #If, #Const, #Include at the start of a line
    AF_BAS_LABEL = 11,         // a line number or "name:" at the start of a line
    AF_BAS_PROCNAME = 12,      // the name after Sub / Function / Property Get...
    AF_BAS_DATE = 13,          // #1/2/2026#
};

} // namespace af
