// ARexx command parsing.
#include "aftest.h"
#include "ide/RexxCmd.h"

using namespace af;

static bool P(const char *tmpl, const char *args, rexx::Args &a, std::string *err = nullptr)
{
    std::string e;
    bool ok = rexx::parseArgs(tmpl, args, a, e);
    if (err) *err = e;
    return ok;
}

TEST(rexx_tokenize)
{
    auto t = rexx::tokenize("GOTO  12 \"a b\" c");
    CHECK_EQ(t.size(), (size_t)4);
    CHECK_EQ(t[2], std::string("a b"));
    CHECK_EQ(rexx::tokenize("\"x*\"y*n\"")[0], std::string("x\"y\n"));
    CHECK(rexx::tokenize("   ").empty());
}

TEST(rexx_split)
{
    std::string c, r;
    rexx::splitCommand("  goto 10 5", c, r);
    CHECK_EQ(c, std::string("GOTO"));
    CHECK_EQ(r, std::string("10 5"));
    rexx::splitCommand("SAVE", c, r);
    CHECK_EQ(c, std::string("SAVE"));
    CHECK_EQ(r, std::string(""));
}

TEST(rexx_positional_and_numbers)
{
    rexx::Args a;
    CHECK(P("LINE/N/A,COL/N", "12 4", a));
    CHECK_EQ(a.num("LINE"), 12L);
    CHECK_EQ(a.num("COL"), 4L);
    CHECK(P("LINE/N/A,COL/N", "12", a));
    CHECK(!a.has("COL"));
    CHECK_EQ(a.num("COL", 1), 1L);
    std::string err;
    CHECK(!P("LINE/N/A,COL/N", "", a, &err));
    CHECK_EQ(err, std::string("missing LINE"));
    CHECK(!P("LINE/N/A,COL/N", "abc", a, &err));
    CHECK_EQ(err, std::string("LINE must be a number"));
    CHECK(!P("LINE/N/A", "1 2 3", a, &err));
}

TEST(rexx_keywords)
{
    rexx::Args a;
    CHECK(P("LINE/N/A,COL/N", "COL=3 LINE=7", a));
    CHECK_EQ(a.num("LINE"), 7L);
    CHECK_EQ(a.num("COL"), 3L);
    CHECK(P("LINE/N/A,COL/N", "col 9 line 2", a));
    CHECK_EQ(a.num("LINE"), 2L);
    CHECK_EQ(a.num("COL"), 9L);
}

TEST(rexx_rest_of_line)
{
    rexx::Args a;
    CHECK(P("TEXT/A/F", "hello there  world", a));
    CHECK_EQ(a.str("TEXT"), std::string("hello there  world"));
    CHECK(P("TEXT/A/F", "\"quoted all\"", a));
    CHECK_EQ(a.str("TEXT"), std::string("quoted all"));
    CHECK(P("TEXT/A/F", "say \"hi\" now", a));
    CHECK_EQ(a.str("TEXT"), std::string("say \"hi\" now"));
    CHECK(P("FILE/A/F", "DH1:My Projects/main.c", a));
    CHECK_EQ(a.str("FILE"), std::string("DH1:My Projects/main.c"));
    CHECK(P("TEXT/F", "", a));
    CHECK(!a.has("TEXT"));
}

TEST(rexx_switches)
{
    rexx::Args a;
    CHECK(P("FORCE/S", "FORCE", a));
    CHECK(a.has("FORCE"));
    CHECK(P("FORCE/S", "", a));
    CHECK(!a.has("FORCE"));
    CHECK(P("ALL/S", "all", a));
    CHECK(a.has("ALL"));
}

TEST(rexx_command_table)
{
    CHECK(rexx::findCommand("goto") != nullptr);
    CHECK(rexx::findCommand("GetText") != nullptr);
    CHECK(rexx::findCommand("frobnicate") == nullptr);
    for (const char *c : { "OPEN", "SAVE", "SAVEAS", "CLOSE", "GOTO", "GETLINE", "GETCOL", "GETFILE", "GETTEXT",
                           "INSERT", "REPLACE", "FIND", "BUILD", "RUN", "DEBUG", "STOP", "PROJECT", "QUIT", "HELP" })
        CHECK(rexx::findCommand(c) != nullptr);
    // every template parses, and HELP lists every command
    std::string help = rexx::helpText();
    for (auto &c : rexx::commands()) {
        CHECK(help.find(c.name) != std::string::npos);
        rexx::Args a;
        std::string err;
        rexx::parseArgs(c.tmpl, "", a, err);   // must not crash
    }
}
