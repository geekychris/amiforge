// --- lang --- Lexilla lexers for ARexx (.rexx, .rx) and AmigaDOS scripts (S:Startup-Sequence,
// .key scripts), which Lexilla doesn't have.
#pragma once

namespace Lexilla { class LexerModule; }

// Keyword sets: 0 instructions (SAY, PARSE, DO...), 1 built-in functions (LEFT, WORDS...),
// 2 special variables (RC, RESULT, SIGL). Folds DO/SELECT ... END.
extern const Lexilla::LexerModule lmARexx;
// Keyword sets: 0 script commands and common C: commands (If, Skip, Echo, Copy...).
// Folds If ... EndIf.
extern const Lexilla::LexerModule lmAmigaDOS;

namespace af {

enum {
    AF_REXX_DEFAULT = 0,
    AF_REXX_COMMENT = 1,       // /* ... */, nested
    AF_REXX_STRING = 2,        // '...' or "..." ('' and "" inside are a quote)
    AF_REXX_NUMBER = 3,
    AF_REXX_KEYWORD = 4,       // instructions and their sub-keywords
    AF_REXX_BUILTIN = 5,       // built-in functions
    AF_REXX_OPERATOR = 6,
    AF_REXX_IDENTIFIER = 7,
    AF_REXX_LABEL = 8,         // name: at the start of a clause
    AF_REXX_STRINGEOL = 9,     // a string that isn't closed on its line
    AF_REXX_SPECIAL = 10,      // RC, RESULT, SIGL
};

enum {
    AF_DOS_DEFAULT = 0,
    AF_DOS_COMMENT = 1,        // ; to the end of the line
    AF_DOS_STRING = 2,         // "..." (*" and *N escapes)
    AF_DOS_NUMBER = 3,
    AF_DOS_COMMAND = 4,        // the first word of a line, when it's a known command
    AF_DOS_DIRECTIVE = 5,      // .key .bra .ket .def ...
    AF_DOS_PARAM = 6,          // <name> in a .key script (with the current .bra/.ket: < >)
    AF_DOS_VARIABLE = 7,       // $name, ${name}
    AF_DOS_OPERATOR = 8,       // > < >> | ` * #?
    AF_DOS_LABEL = 9,          // the name after Lab / Skip
    AF_DOS_IDENTIFIER = 10,
};

} // namespace af
