#include "aftest.h"
#include "sci/Theme.h"
#include <set>

using namespace af;

TEST(theme_light_and_dark_exist)
{
    CHECK(themes().size() >= 2);
    CHECK_EQ(std::string(themes()[0].name), std::string("Light"));
    CHECK_EQ(std::string(themeNamed("dark").name), std::string("Dark"));
    CHECK(themeNamed("Dark").dark);
    CHECK(!themeNamed("Light").dark);
}

TEST(theme_unknown_falls_back_to_light)
{
    CHECK_EQ(std::string(themeNamed("no such theme").name), std::string("Light"));
    setCurrentTheme("DARK");
    CHECK_EQ(std::string(currentTheme().name), std::string("Dark"));
    setCurrentTheme("garbage");
    CHECK_EQ(std::string(currentTheme().name), std::string("Light"));
}

TEST(theme_colour_conversion)
{
    CHECK_EQ(sciColour(0x112233), (intptr_t)0x332211);
    CHECK_EQ(sciColour(0xFF0000), (intptr_t)0x0000FF);
}

TEST(theme_contrast_ratio_maths)
{
    CHECK(contrastRatio(0x000000, 0xFFFFFF) > 20.9);
    CHECK(contrastRatio(0x777777, 0x777777) < 1.001);
    CHECK(contrastRatio(0xFFFFFF, 0x000000) == contrastRatio(0x000000, 0xFFFFFF));
}

// Every theme must be readable: body text at least 7:1 (WCAG AAA), syntax colours and the
// line numbers at least 3:1 against the background they're drawn on.
TEST(theme_text_is_readable)
{
    for (auto &t : themes()) {
        std::string n = t.name;
        CHECK(contrastRatio(t.textFore, t.textBack) >= 7.0);
        for (uint32_t c : { t.comment, t.docComment, t.keyword, t.type, t.string, t.number, t.preprocessor,
                            t.oper, t.label, t.asmOper })
            if (contrastRatio(c, t.textBack) < 3.0)
                aftest::fail(__FILE__, __LINE__, n + ": syntax colour too faint: " + std::to_string(c));
        CHECK(contrastRatio(t.marginFore, t.marginBack) >= 3.0);
        CHECK(contrastRatio(t.calltipFore, t.calltipBack) >= 7.0);
        CHECK(contrastRatio(t.listFore, t.listBack) >= 7.0);
        CHECK(contrastRatio(t.listSelFore, t.listSelBack) >= 4.5);
        CHECK(contrastRatio(t.textFore, t.selection) >= 4.5);              // selected text
        CHECK(contrastRatio(t.textFore, t.execLineBack) >= 4.5);           // the line where it stopped
        CHECK(contrastRatio(t.textFore, t.errorBack) >= 4.5);
        CHECK(contrastRatio(t.textFore, t.warningBack) >= 4.5);
        CHECK(contrastRatio(t.diffAddedFore, t.diffAddedBack) >= 4.5);
        CHECK(contrastRatio(t.diffDeletedFore, t.diffDeletedBack) >= 4.5);
        CHECK(contrastRatio(t.docLink, t.textBack) >= 4.5);
        CHECK(contrastRatio(t.caret, t.textBack) >= 4.5);
    }
}

TEST(theme_dark_is_dark)
{
    const Theme &d = themeNamed("Dark");
    // a dark background and light text
    CHECK(contrastRatio(d.textBack, 0x000000) < 2.0);
    CHECK(contrastRatio(d.textFore, 0xFFFFFF) < 2.0);
    const Theme &l = themeNamed("Light");
    CHECK(contrastRatio(l.textBack, 0xFFFFFF) < 1.1);
}

TEST(theme_syntax_colours_are_distinct)
{
    for (auto &t : themes()) {
        std::set<uint32_t> s = { t.comment, t.keyword, t.type, t.string, t.number, t.preprocessor };
        CHECK_EQ(s.size(), (size_t)6);
    }
}
