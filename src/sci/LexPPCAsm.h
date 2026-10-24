// A Lexilla lexer for PowerPC GNU as sources (.s, and .S run through the C preprocessor).
#pragma once

namespace Lexilla { class LexerModule; }

// Keyword sets: 0 instructions, 1 registers, 2 directives.
extern const Lexilla::LexerModule lmPPCAsm;

namespace af {

enum {
    AF_ASM_DEFAULT = 0,
    AF_ASM_COMMENT = 1,        // # to end of line
    AF_ASM_COMMENTBLOCK = 2,   // /* ... */ and // in preprocessed sources
    AF_ASM_NUMBER = 3,
    AF_ASM_STRING = 4,
    AF_ASM_CHARACTER = 5,
    AF_ASM_OPERATOR = 6,
    AF_ASM_IDENTIFIER = 7,
    AF_ASM_INSTRUCTION = 8,
    AF_ASM_REGISTER = 9,
    AF_ASM_DIRECTIVE = 10,
    AF_ASM_LABEL = 11,
    AF_ASM_PREPROCESSOR = 12,  // #include, #define... at the start of a line
    AF_ASM_STRINGEOL = 13,
};

}
