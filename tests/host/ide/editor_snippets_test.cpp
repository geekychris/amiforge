#include "aftest.h"
#include "ide/Snippets.h"

using namespace af;

TEST(snippet_plain_text)
{
    auto e = expandSnippet("hello", "");
    CHECK_EQ(e.text, std::string("hello"));
    CHECK(e.fields.empty());
}

TEST(snippet_fields_and_defaults)
{
    auto e = expandSnippet("for (${1:int} ${2:i} = 0; $2 < ${3:n}; ${2}++) {$0}", "");
    CHECK_EQ(e.text, std::string("for (int i = 0; i < n; i++) {}"));
    CHECK_EQ(e.fields.size(), (size_t)6);
    CHECK_EQ(e.fields[0].number, 1);
    CHECK_EQ(e.text.substr(e.fields[0].start, e.fields[0].end - e.fields[0].start), std::string("int"));
    CHECK(!e.fields[1].mirror);
    CHECK(e.fields[2].mirror);      // $2 again
    CHECK_EQ(e.text.substr(e.fields[2].start, 1), std::string("i"));
    CHECK(e.fields[4].mirror);
    CHECK_EQ(e.fields[5].number, 0);
    CHECK_EQ(e.fields[5].start, e.text.size() - 1);
    CHECK_EQ(e.fields[5].end, e.fields[5].start);
}

TEST(snippet_tab_order)
{
    auto e = expandSnippet("${2:b} ${0} ${1:a} ${2}", "");
    auto order = e.tabOrder();
    CHECK_EQ(order.size(), (size_t)3);
    CHECK_EQ(e.fields[order[0]].number, 1);
    CHECK_EQ(e.fields[order[1]].number, 2);
    CHECK_EQ(e.fields[order[2]].number, 0);
    CHECK(!e.fields[order[1]].mirror);
}

TEST(snippet_indentation)
{
    auto e = expandSnippet("if (x) {\n\t${0}\n}", "  ", "    ");
    CHECK_EQ(e.text, std::string("if (x) {\n      \n  }"));
    CHECK_EQ(e.fields[0].start, (size_t)15);
}

TEST(snippet_escapes)
{
    auto e = expandSnippet("\\$1 costs \\$5 \\\\ ${1:a\\}b}", "");
    CHECK_EQ(e.text, std::string("$1 costs $5 \\ a}b"));
    CHECK_EQ(e.fields.size(), (size_t)1);
}

TEST(snippet_dollar_without_number_is_literal)
{
    auto e = expandSnippet("cost $x ${y} $", "");
    CHECK_EQ(e.text, std::string("cost $x ${y} $"));
    CHECK(e.fields.empty());
}

TEST(snippet_parse_file)
{
    std::string f =
        "# my snippets\n"
        "snippet hi  Say hello\n"
        "\tIDOS->Printf(\"hello\\n\");\n"
        "\t${0}\n"
        "\n"
        "snippet two\n"
        "\tline 1\n"
        "\n"
        "\t\tline 3\n"
        "snippet\n"
        "not a body line\n";
    auto list = parseSnippets(f);
    CHECK_EQ(list.size(), (size_t)2);
    CHECK_EQ(list[0].name, std::string("hi"));
    CHECK_EQ(list[0].description, std::string("Say hello"));
    CHECK_EQ(list[0].body, std::string("IDOS->Printf(\"hello\\n\");\n${0}"));
    CHECK_EQ(list[1].name, std::string("two"));
    CHECK_EQ(list[1].body, std::string("line 1\n\n\tline 3"));
}

TEST(snippet_merge_user_overrides)
{
    std::vector<Snippet> user = { { "for", "mine", "for(;;) {}" }, { "zzz", "", "z" } };
    auto all = mergeSnippets(builtinSnippets(), user);
    CHECK_EQ(all.size(), builtinSnippets().size() + 1);
    CHECK_EQ(findSnippet(all, "for")->body, std::string("for(;;) {}"));
    CHECK(findSnippet(all, "zzz") != nullptr);
    CHECK(findSnippet(all, "nope") == nullptr);
}

TEST(snippet_builtins_present_and_valid)
{
    for (const char *n : { "openlib", "window", "timer", "rexx", "main", "for", "struct", "class", "tags" })
        CHECK(findSnippet(builtinSnippets(), n) != nullptr);
    for (auto &s : builtinSnippets()) {
        auto e = expandSnippet(s.body, "");
        // every builtin has somewhere for the cursor to go and no leftover syntax
        CHECK(!e.tabOrder().empty());
        CHECK(e.text.find("${") == std::string::npos);
        // braces balance in the C code
        int b = 0;
        for (char c : e.text) b += c == '{' ? 1 : c == '}' ? -1 : 0;
        if (b != 0) aftest::fail(__FILE__, __LINE__, "unbalanced braces in " + s.name);
    }
}

TEST(snippet_openlib_mirrors)
{
    auto e = expandSnippet(findSnippet(builtinSnippets(), "openlib")->body, "");
    CHECK(e.text.find("struct Library *FooBase = IExec->OpenLibrary(\"foo.library\", 53);") == 0);
    CHECK(e.text.find("struct FooIFace *IFoo = FooBase") != std::string::npos);
    int mirrors = 0;
    for (auto &f : e.fields) if (f.number == 1 && f.mirror) mirrors++;
    CHECK(mirrors >= 5);
}

TEST(snippet_adjust_fields_insert_delete)
{
    std::vector<SnippetField> f = { {1, 4, 7, false}, {2, 10, 10, false}, {1, 20, 23, true} };
    adjustSnippetFields(f, 5, 2, true);          // typing inside field 1
    CHECK_EQ(f[0].start, (size_t)4); CHECK_EQ(f[0].end, (size_t)9);
    CHECK_EQ(f[1].start, (size_t)12); CHECK_EQ(f[2].start, (size_t)22);
    adjustSnippetFields(f, 12, 3, true);         // typing into the empty field 2
    CHECK_EQ(f[1].start, (size_t)12); CHECK_EQ(f[1].end, (size_t)15);
    adjustSnippetFields(f, 4, 5, false);         // delete all of field 1's text
    CHECK_EQ(f[0].start, (size_t)4); CHECK_EQ(f[0].end, (size_t)4);
    CHECK_EQ(f[1].start, (size_t)7); CHECK_EQ(f[1].end, (size_t)10);
    adjustSnippetFields(f, 2, 10, false);        // a deletion over several fields
    CHECK_EQ(f[0].start, (size_t)2); CHECK_EQ(f[1].end, (size_t)2);
    CHECK_EQ(f[2].start, (size_t)10);
}

TEST(snippet_parse_space_indented_bodies)
{
    // the editor indents with spaces: four spaces work like a tab
    auto list = parseSnippets("snippet sp  spaces\n    a();\n        b();\n\n    c();\nsnippet next\n\tx\n");
    CHECK_EQ(list.size(), (size_t)2);
    CHECK_EQ(list[0].body, std::string("a();\n    b();\n\nc();"));
    CHECK_EQ(list[1].body, std::string("x"));
}
