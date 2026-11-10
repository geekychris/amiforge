// --- lang --- The ARexx and AmigaDOS lexers (src/sci/LexAmiga.cpp) and Lexilla's Python lexer
// as AmiForge sets it up, run on a small IDocument.
#include "aftest.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "LexerModule.h"

#include "sci/LexAmiga.h"
#include "sci/Languages.h"

extern const Lexilla::LexerModule lmPython;

#include "lexer_testdoc.h"

namespace {

std::vector<const char *> rexxWords()
{
    return { af::languageKeywords(af::Language::ARexx, 0), af::languageKeywords(af::Language::ARexx, 1),
             af::languageKeywords(af::Language::ARexx, 2) };
}

} // namespace

using namespace af;

TEST(lexer_arexx_styles)
{
    TestDocument d(
        "/* SortLines.rexx /* nested */ still comment */\n"   // 0
        "OPTIONS RESULTS\n"                                   // 1
        "parse arg name\n"                                    // 2
        "say 'It''s' \"x\" Left(name, 3) 42 rc\n"             // 3
        "CALL Helper\n"                                       // 4
        "Helper: PROCEDURE\n"                                 // 5
        "say 'open\n"                                         // 6
        "left = 1\n");                                        // 7
    lex(lmARexx, d, rexxWords());
    CHECK_EQ(d.styleOf("still"), (int)AF_REXX_COMMENT);       // after the nested comment closed
    CHECK_EQ(d.styleOf("OPTIONS"), (int)AF_REXX_KEYWORD);
    CHECK_EQ(d.styleOf("RESULTS"), (int)AF_REXX_KEYWORD);
    CHECK_EQ(d.styleOf("parse"), (int)AF_REXX_KEYWORD);
    CHECK_EQ(d.styleOf("name"), (int)AF_REXX_IDENTIFIER);
    CHECK_EQ(d.styleOf("s'"), (int)AF_REXX_STRING);           // '' inside a string
    CHECK_EQ(d.styleOf("\"x\""), (int)AF_REXX_STRING);
    CHECK_EQ(d.styleOf("Left("), (int)AF_REXX_BUILTIN);
    CHECK_EQ(d.styleOf("42"), (int)AF_REXX_NUMBER);
    CHECK_EQ(d.styleOf("rc"), (int)AF_REXX_SPECIAL);
    CHECK_EQ(d.styleOf("Helper", 0), (int)AF_REXX_IDENTIFIER); // CALL Helper: a name
    CHECK_EQ(d.styleOf("Helper:"), (int)AF_REXX_LABEL);
    CHECK_EQ(d.styleOf("PROCEDURE"), (int)AF_REXX_KEYWORD);
    CHECK_EQ(d.styleOf("'open"), (int)AF_REXX_STRINGEOL);
    CHECK_EQ(d.styleOf("left ="), (int)AF_REXX_IDENTIFIER);   // a variable, not the function
}

TEST(lexer_arexx_comment_spans_lines_and_folds)
{
    TestDocument d(
        "/* a\n"            // 0
        "   b */\n"         // 1
        "DO i = 1 TO 3\n"   // 2
        "  IF i > 1 THEN DO\n"  // 3
        "    SAY i\n"       // 4
        "  END\n"           // 5
        "END\n"             // 6
        "SELECT\n"          // 7
        "  WHEN x THEN NOP\n" // 8
        "END\n");           // 9
    lex(lmARexx, d, rexxWords());
    CHECK_EQ(d.styleOf("b */"), (int)AF_REXX_COMMENT);
    CHECK_EQ(d.styleOf("DO i"), (int)AF_REXX_KEYWORD);
    CHECK(d.header(2) && d.header(3) && d.header(7));
    CHECK(!d.header(4));
    CHECK_EQ(d.foldDepth(4), 2);
    CHECK_EQ(d.foldDepth(6), 1);   // the END line belongs to the block
    CHECK_EQ(d.foldDepth(9), 1);
    CHECK_EQ(d.styleOf("SAY"), (int)AF_REXX_KEYWORD);
}

TEST(lexer_amigados_styles_and_folds)
{
    TestDocument d(
        ".key FROM/A,TO\n"                        // 0
        "; copy things\n"                         // 1
        "If EXISTS <FROM>\n"                      // 2
        "  C:Copy <FROM> TO \"RAM:x y\" QUIET\n"  // 3
        "  Echo \"a *\"quoted*\" word\" >NIL:\n"  // 4
        "Else\n"                                  // 5
        "  Skip done\n"                           // 6
        "EndIf\n"                                 // 7
        "Lab done\n"                              // 8
        "setenv Count $count\n");                 // 9
    lex(lmAmigaDOS, d, { languageKeywords(Language::AmigaDOS, 0) });
    CHECK_EQ(d.styleOf(".key"), (int)AF_DOS_DIRECTIVE);
    CHECK_EQ(d.styleOf("; copy"), (int)AF_DOS_COMMENT);
    CHECK_EQ(d.styleOf("If"), (int)AF_DOS_COMMAND);
    CHECK_EQ(d.styleOf("<FROM>"), (int)AF_DOS_PARAM);
    CHECK_EQ(d.styleOf("C:Copy"), (int)AF_DOS_COMMAND);
    CHECK_EQ(d.styleOf("\"RAM:x y\""), (int)AF_DOS_STRING);
    CHECK_EQ(d.styleOf("word\""), (int)AF_DOS_STRING);         // *" doesn't end the string
    CHECK_EQ(d.styleOf(">NIL:"), (int)AF_DOS_OPERATOR);
    CHECK_EQ(d.styleOf("done", 0), (int)AF_DOS_LABEL);
    CHECK_EQ(d.styleOf("done", 1), (int)AF_DOS_LABEL);
    CHECK_EQ(d.styleOf("$count"), (int)AF_DOS_VARIABLE);
    CHECK(d.header(2));
    CHECK_EQ(d.foldDepth(4), 1);
    CHECK_EQ(d.foldDepth(8), 0);
}

TEST(lexer_python_as_set_up)
{
    TestDocument d(
        "@decorator\n"                 // 0
        "def area(r):\n"               // 1
        "    \"\"\"Doc.\"\"\"\n"       // 2
        "    return 3.14 * r * r  # c\n" // 3
        "class Shape:\n"               // 4
        "    pass\n"                   // 5
        "print(f'{area(2)}')\n");      // 6
    lex(lmPython, d, { languageKeywords(Language::Python, 0), languageKeywords(Language::Python, 1) },
        { { "fold", "1" }, { "fold.quotes.python", "1" } });
    CHECK_EQ(d.styleOf("@decorator"), (int)SCE_P_DECORATOR);
    CHECK_EQ(d.styleOf("def"), (int)SCE_P_WORD);
    CHECK_EQ(d.styleOf("area"), (int)SCE_P_DEFNAME);
    CHECK_EQ(d.styleOf("\"\"\"Doc"), (int)SCE_P_TRIPLEDOUBLE);
    CHECK_EQ(d.styleOf("3.14"), (int)SCE_P_NUMBER);
    CHECK_EQ(d.styleOf("# c"), (int)SCE_P_COMMENTLINE);
    CHECK_EQ(d.styleOf("Shape"), (int)SCE_P_CLASSNAME);
    CHECK_EQ(d.styleOf("print"), (int)SCE_P_WORD2);
    CHECK_EQ(d.styleOf("f'"), (int)SCE_P_FCHARACTER);
    CHECK(d.header(1) && d.header(4));     // folds by indentation
}
