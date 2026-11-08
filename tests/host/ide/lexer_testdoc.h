// A small Scintilla IDocument for running Lexilla lexers in the host tests, and lex() to run
// one with keyword lists and properties the way SciSetup sets them up.
#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "LexerModule.h"

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

} // namespace
