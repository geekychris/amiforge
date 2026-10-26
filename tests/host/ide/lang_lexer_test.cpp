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

namespace {

class TestDocument : public Scintilla::IDocument {
public:
    std::string text;
    std::vector<char> styles;
    std::vector<int> levels, states;
    std::vector<Sci_Position> starts;   // line starts
    Sci_Position styleAt = 0;

    explicit TestDocument(const std::string &t) : text(t), styles(t.size(), 0)
    {
        starts.push_back(0);
        for (size_t i = 0; i < t.size(); i++)
            if (t[i] == '\n') starts.push_back((Sci_Position)i + 1);
        levels.assign(starts.size() + 1, SC_FOLDLEVELBASE);
        states.assign(starts.size() + 1, 0);
    }
    int SCI_METHOD Version() const override { return Scintilla::dvRelease4; }
    void SCI_METHOD SetErrorStatus(int) override {}
    Sci_Position SCI_METHOD Length() const override { return (Sci_Position)text.size(); }
    void SCI_METHOD GetCharRange(char *buffer, Sci_Position position, Sci_Position len) const override
    {
        for (Sci_Position i = 0; i < len; i++)
            buffer[i] = position + i < (Sci_Position)text.size() ? text[position + i] : 0;
    }
    char SCI_METHOD StyleAt(Sci_Position position) const override
    {
        return position >= 0 && position < (Sci_Position)styles.size() ? styles[position] : 0;
    }
    Sci_Position SCI_METHOD LineFromPosition(Sci_Position position) const override
    {
        Sci_Position line = 0;
        for (size_t i = 1; i < starts.size(); i++)
            if (starts[i] <= position) line = (Sci_Position)i;
        return line;
    }
    Sci_Position SCI_METHOD LineStart(Sci_Position line) const override
    {
        if (line < 0) return 0;
        if (line >= (Sci_Position)starts.size()) return (Sci_Position)text.size();
        return starts[line];
    }
    int SCI_METHOD GetLevel(Sci_Position line) const override { return line >= 0 && line < (Sci_Position)levels.size() ? levels[line] : SC_FOLDLEVELBASE; }
    int SCI_METHOD SetLevel(Sci_Position line, int level) override
    {
        if (line >= 0 && line < (Sci_Position)levels.size()) levels[line] = level;
        return level;
    }
    int SCI_METHOD GetLineState(Sci_Position line) const override { return line >= 0 && line < (Sci_Position)states.size() ? states[line] : 0; }
    int SCI_METHOD SetLineState(Sci_Position line, int state) override
    {
        if (line >= 0 && line < (Sci_Position)states.size()) states[line] = state;
        return state;
    }
    void SCI_METHOD StartStyling(Sci_Position position) override { styleAt = position; }
    bool SCI_METHOD SetStyleFor(Sci_Position length, char style) override
    {
        for (Sci_Position i = 0; i < length && styleAt < (Sci_Position)styles.size(); i++) styles[styleAt++] = style;
        return true;
    }
    bool SCI_METHOD SetStyles(Sci_Position length, const char *s) override
    {
        for (Sci_Position i = 0; i < length && styleAt < (Sci_Position)styles.size(); i++) styles[styleAt++] = s[i];
        return true;
    }
    void SCI_METHOD DecorationSetCurrentIndicator(int) override {}
    void SCI_METHOD DecorationFillRange(Sci_Position, int, Sci_Position) override {}
    void SCI_METHOD ChangeLexerState(Sci_Position, Sci_Position) override {}
    int SCI_METHOD CodePage() const override { return 0; }
    bool SCI_METHOD IsDBCSLeadByte(char) const override { return false; }
    const char *SCI_METHOD BufferPointer() override { return text.c_str(); }
    int SCI_METHOD GetLineIndentation(Sci_Position line) override
    {
        int n = 0;
        for (Sci_Position p = LineStart(line); p < (Sci_Position)text.size() && (text[p] == ' ' || text[p] == '\t'); p++)
            n = text[p] == '\t' ? (n / 8 + 1) * 8 : n + 1;
        return n;
    }
    Sci_Position SCI_METHOD LineEnd(Sci_Position line) const override
    {
        Sci_Position e = LineStart(line + 1);
        if (e > LineStart(line) && e <= (Sci_Position)text.size() && text[e - 1] == '\n') e--;
        return e;
    }
    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position start, Sci_Position offset) const override
    {
        Sci_Position p = start + offset;
        return p < 0 || p > (Sci_Position)text.size() ? -1 : p;
    }
    int SCI_METHOD GetCharacterAndWidth(Sci_Position position, Sci_Position *width) const override
    {
        if (width) *width = 1;
        return position < (Sci_Position)text.size() ? (unsigned char)text[position] : 0;
    }

    int styleOf(const std::string &needle, size_t nth = 0) const
    {
        size_t p = std::string::npos, from = 0;
        for (size_t k = 0; k <= nth; k++) {
            p = text.find(needle, from);
            if (p == std::string::npos) return -1;
            from = p + 1;
        }
        return styles[p];
    }
    int foldDepth(int line) const { return (levels[line] & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE; }
    bool header(int line) const { return (levels[line] & SC_FOLDLEVELHEADERFLAG) != 0; }
};

// Lex (and fold) with a lexer module, keyword lists and properties like SciSetup sets them.
void lex(const Lexilla::LexerModule &m, TestDocument &doc, const std::vector<const char *> &words,
         const std::vector<std::pair<const char *, const char *>> &props = { { "fold", "1" } })
{
    Scintilla::ILexer5 *lx = m.Create();
    for (size_t i = 0; i < words.size(); i++) lx->WordListSet((int)i, words[i]);
    for (auto &p : props) lx->PropertySet(p.first, p.second);
    lx->Lex(0, doc.Length(), 0, &doc);
    lx->Fold(0, doc.Length(), 0, &doc);
    lx->Release();
}

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
