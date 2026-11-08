// --- basic --- The Boing BASIC lexer (src/sci/LexBoing.cpp) and its folding.
#include "aftest.h"

#include "sci/LexBoing.h"
#include "sci/Languages.h"

#include "lexer_testdoc.h"

using namespace af;

namespace {
std::vector<const char *> basWords()
{
    return { languageKeywords(Language::Basic, 0), languageKeywords(Language::Basic, 1),
             languageKeywords(Language::Basic, 2) };
}
}

TEST(lexer_basic_styles)
{
    TestDocument d(
        "' a comment\n"                                   // 0
        "Option Explicit\n"                               // 1
        "Private Sub ok_Click()\n"                        // 2
        "    Dim n As Integer, s$\n"                      // 3
        "    s = \"say \"\"hi\"\"\" & Left$(s, 2) ' tail\n"   // 4
        "    n = &HFF + 12% : Rem the rest\n"             // 5
        "10  Print #1, n\n"                               // 6
        "retry: d = #1/2/2026#\n"                         // 7
        "#If DEBUG Then\n"                                // 8
        "    msg = \"open\n"                              // 9
        "    ok.Text = name.Left\n"                       // 10
        "End Sub\n");                                     // 11
    lex(lmBoingBasic, d, basWords());
    CHECK_EQ(d.styleOf("a comment"), (int)AF_BAS_COMMENT);
    CHECK_EQ(d.styleOf("Option"), (int)AF_BAS_KEYWORD);
    CHECK_EQ(d.styleOf("Explicit"), (int)AF_BAS_KEYWORD);
    CHECK_EQ(d.styleOf("Private"), (int)AF_BAS_KEYWORD);
    CHECK_EQ(d.styleOf("ok_Click"), (int)AF_BAS_PROCNAME);
    CHECK_EQ(d.styleOf("Integer"), (int)AF_BAS_TYPE);
    CHECK_EQ(d.styleOf("s$"), (int)AF_BAS_IDENTIFIER);
    CHECK_EQ(d.styleOf("hi"), (int)AF_BAS_STRING);
    CHECK_EQ(d.styleOf("Left$"), (int)AF_BAS_BUILTIN);
    CHECK_EQ(d.styleOf("$(s"), (int)AF_BAS_BUILTIN);      // the suffix belongs to the name
    CHECK_EQ(d.styleOf("tail"), (int)AF_BAS_COMMENT);
    CHECK_EQ(d.styleOf("&HFF"), (int)AF_BAS_NUMBER);
    CHECK_EQ(d.styleOf("12%"), (int)AF_BAS_NUMBER);
    CHECK_EQ(d.styleOf("% :"), (int)AF_BAS_NUMBER);
    CHECK_EQ(d.styleOf("Rem the"), (int)AF_BAS_COMMENT);
    CHECK_EQ(d.styleOf("the rest"), (int)AF_BAS_COMMENT);
    CHECK_EQ(d.styleOf("10  Print"), (int)AF_BAS_LABEL);
    CHECK_EQ(d.styleOf("Print #"), (int)AF_BAS_KEYWORD);
    CHECK_EQ(d.styleOf("#1, n"), (int)AF_BAS_OPERATOR);    // a file number, not a date
    CHECK_EQ(d.styleOf("retry"), (int)AF_BAS_LABEL);
    CHECK_EQ(d.styleOf("#1/2/2026#"), (int)AF_BAS_DATE);
    CHECK_EQ(d.styleOf("#If DEBUG"), (int)AF_BAS_PREPROCESSOR);
    CHECK_EQ(d.styleOf("\"open"), (int)AF_BAS_STRINGEOL);
    CHECK_EQ(d.styleOf("ok.Text"), (int)AF_BAS_IDENTIFIER);
    CHECK_EQ(d.styleOf("Left\n"), (int)AF_BAS_IDENTIFIER); // a member, not the builtin
    CHECK_EQ(d.styleOf("End Sub"), (int)AF_BAS_KEYWORD);
}

TEST(lexer_basic_folds)
{
    TestDocument d(
        "Sub Main()\n"                 // 0
        "    If a Then\n"              // 1
        "        b = 1\n"              // 2
        "    ElseIf c Then\n"          // 3
        "        b = 2\n"              // 4
        "    End If\n"                 // 5
        "    If a Then b = 3\n"        // 6
        "    For i = 1 To 3\n"         // 7
        "        Select Case i\n"      // 8
        "        Case 1\n"             // 9
        "        End Select\n"         // 10
        "    Next\n"                   // 11
        "End Sub\n"                    // 12
        "' Sub NotOne()\n");           // 13
    lex(lmBoingBasic, d, basWords());
    CHECK(d.header(0) && d.header(1) && d.header(7) && d.header(8));
    CHECK(!d.header(6) && !d.header(13));
    CHECK_EQ(d.foldDepth(2), 2);
    CHECK_EQ(d.foldDepth(6), 1);
    CHECK_EQ(d.foldDepth(9), 3);
    CHECK_EQ(d.foldDepth(12), 1);   // the End Sub line belongs to the block
    CHECK_EQ(d.foldDepth(13), 0);
}
