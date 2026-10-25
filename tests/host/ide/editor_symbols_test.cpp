#include "aftest.h"
#include "ide/Symbols.h"

using namespace af;

static const Symbol *find(const std::vector<Symbol> &v, const std::string &name)
{
    for (auto &s : v) if (s.name == name) return &s;
    return nullptr;
}

static std::string kinds(const std::vector<Symbol> &v)
{
    std::string s;
    for (auto &x : v) s += std::string(symKindName(x.kind)) + ":" + x.name + "@" + std::to_string(x.line) + " ";
    return s;
}

TEST(mask_comments_and_strings)
{
    std::string t = "a // x\nb /* y\nz */ c \"s // t\" 'q' d";
    std::string m = maskCode(t, false);
    CHECK_EQ(m.size(), t.size());
    CHECK_EQ(m, std::string("a     \nb     \n     c              d"));
}

TEST(mask_escapes_and_raw_strings)
{
    std::string t = "x = \"a\\\"b\"; y = R\"(un \" closed)\"; z";
    std::string m = maskCode(t, false);
    CHECK(m.find("x =") == 0);
    CHECK(m.find('y') != std::string::npos);
    CHECK(m.find("un") == std::string::npos);
    CHECK(m.back() == 'z');
}

TEST(mask_include_file_names)
{
    std::string m = maskCode("#include <exec/types.h>\n#include \"list.h\"\nint types;", false);
    CHECK(m.find("exec") == std::string::npos);
    CHECK(m.find("list") == std::string::npos);
    CHECK(m.find("int types;") != std::string::npos);
    CHECK(m.find("#include") == 0);
}

TEST(mask_assembler_comments)
{
    std::string t = "#include \"x.i\"\n\tli r3,0  # comment\n/* block */ blr\n#define X 1\n";
    std::string m = maskCode(t, true);
    CHECK(m.find("comment") == std::string::npos);
    CHECK(m.find("block") == std::string::npos);
    CHECK(m.find("li r3,0") != std::string::npos);
    CHECK(m.find("#define X 1") != std::string::npos);
    CHECK(m.find("x.i") == std::string::npos);
}

TEST(identifier_occurrences_skip_comments_and_strings)
{
    std::string t = "int count = 0; // count\nprintf(\"count\"); count++; recount = counter + count;";
    auto hits = findIdentifier(t, "count", false);
    CHECK_EQ(hits.size(), (size_t)3);
    CHECK_EQ(hits[0], (size_t)4);
    int line, col;
    lineColumn(t, hits[1], line, col);
    CHECK_EQ(line, 2);
    CHECK_EQ(col, 18);
}

TEST(rename_identifier)
{
    std::string t = "struct Ball b; /* Ball */ Ball *p = &b; BallSize; \"Ball\"";
    int n = 0;
    std::string r = replaceIdentifier(t, "Ball", "Sprite", false, &n);
    CHECK_EQ(n, 2);
    CHECK_EQ(r, std::string("struct Sprite b; /* Ball */ Sprite *p = &b; BallSize; \"Ball\""));
}

TEST(rename_in_assembler)
{
    std::string t = "\t.globl fastsum\nfastsum:\n\tbl fastsum.loop # fastsum\n\tb fastsum\n";
    int n = 0;
    std::string r = replaceIdentifier(t, "fastsum", "sum", true, &n);
    CHECK_EQ(n, 3);   // not fastsum.loop (a different label) or the comment
    CHECK(r.find("fastsum.loop") != std::string::npos);
    CHECK(r.find("sum:\n") != std::string::npos);
}

TEST(identifier_names)
{
    CHECK(isIdentifierName("abc_1"));
    CHECK(isIdentifierName("_x"));
    CHECK(!isIdentifierName("1x"));
    CHECK(!isIdentifierName("a-b"));
    CHECK(!isIdentifierName(""));
    CHECK(isReservedWord("while"));
    CHECK(!isReservedWord("main"));
}

TEST(outline_c_file)
{
    std::string t =
        "#include <stdio.h>\n"                          // 1
        "#define MAX_BALLS 32\n"                        // 2
        "#define SQUARE(x) ((x) * (x))\n"               // 3
        "struct Node {\n"                               // 4
        "    int value;\n"                              // 5
        "    struct Node *next;\n"                      // 6
        "};\n"                                          // 7
        "typedef struct { int x, y; } Point;\n"         // 8
        "typedef unsigned long ULONG32;\n"              // 9
        "typedef int (*Compare)(const void *, const void *);\n"   // 10
        "enum Colour { RED, GREEN };\n"                 // 11
        "static int total = 0, count;\n"                // 12
        "const char *names[] = { \"a\", \"b\" };\n"     // 13
        "int square(int x);\n"                          // 14
        "extern int other;\n"                           // 15
        "static int square(int x)\n"                    // 16
        "{\n"                                           // 17
        "    int local = x * x;\n"                      // 18
        "    if (local) { return local; }\n"            // 19
        "    return 0;\n"                               // 20
        "}\n"                                           // 21
        "int main(int argc, char **argv) {\n"           // 22
        "    struct Node n = {0};\n"                    // 23
        "    return square(2);\n"                       // 24
        "}\n";                                          // 25
    auto s = outlineSymbols(t, false);
    std::string k = kinds(s);
    auto expect = [&](const char *name, SymKind kind, int line) {
        const Symbol *x = find(s, name);
        if (!x) { aftest::fail(__FILE__, __LINE__, std::string("missing ") + name + " in " + k); return; }
        if (x->kind != kind || x->line != line)
            aftest::fail(__FILE__, __LINE__, std::string("wrong ") + name + " in " + k);
    };
    expect("MAX_BALLS", SymKind::Define, 2);
    expect("SQUARE", SymKind::Define, 3);
    expect("Node", SymKind::Struct, 4);
    expect("Point", SymKind::Struct, 8);       // an anonymous typedef'd struct
    expect("ULONG32", SymKind::Typedef, 9);
    expect("Compare", SymKind::Typedef, 10);
    expect("Colour", SymKind::Enum, 11);
    expect("total", SymKind::Global, 12);
    expect("count", SymKind::Global, 12);
    expect("names", SymKind::Global, 13);
    expect("square", SymKind::Function, 16);
    expect("main", SymKind::Function, 22);
    CHECK_EQ(find(s, "SQUARE")->detail, std::string("(x)"));
    CHECK_EQ(find(s, "main")->detail, std::string("(int argc, char **argv)"));
    // not in the outline: members, locals, prototypes, externs, enum values, includes
    for (const char *no : { "value", "next", "local", "other", "RED", "x", "n", "stdio", "argc" })
        if (find(s, no)) aftest::fail(__FILE__, __LINE__, std::string("unexpected ") + no + " in " + k);
    int squares = 0;
    for (auto &x : s) if (x.name == "square") squares++;
    CHECK_EQ(squares, 1);   // the definition, not the prototype
    // in line order
    for (size_t i = 1; i < s.size(); i++) CHECK(s[i - 1].line <= s[i].line);
}

TEST(outline_cpp_file)
{
    std::string t =
        "namespace os {\n"                                       // 1
        "class AppWindow : public Base {\n"                      // 2
        "public:\n"                                              // 3
        "    AppWindow(const char *title, int w, int h);\n"      // 4
        "    ~AppWindow() { close(); }\n"                        // 5
        "    bool open() const { return win != nullptr; }\n"     // 6
        "    bool operator==(const AppWindow &o) const { return this == &o; }\n"   // 7
        "private:\n"                                             // 8
        "    struct Window *win = nullptr;\n"                    // 9
        "};\n"                                                   // 10
        "}\n"                                                    // 11
        "os::AppWindow::AppWindow(const char *t, int w, int h)\n"   // 12
        "    : win(nullptr)\n"                                   // 13
        "{\n"                                                    // 14
        "    auto f = [](int a) { return a; };\n"                // 15
        "}\n"                                                    // 16
        "template <typename T> T maxOf(T a, T b) { return a > b ? a : b; }\n"   // 17
        "extern \"C\" {\n"                                       // 18
        "void c_entry(void) {}\n"                                // 19
        "}\n"                                                    // 20
        "std::vector<int> numbers;\n"                            // 21
        "static Ball balls[MAX] = {};\n";                        // 22
    auto s = outlineSymbols(t, false);
    std::string k = kinds(s);
    auto has = [&](const char *name, SymKind kind, int line) {
        const Symbol *x = find(s, name);
        if (!x || x->kind != kind || x->line != line)
            aftest::fail(__FILE__, __LINE__, std::string("expected ") + name + " in " + k);
    };
    has("os", SymKind::Namespace, 1);
    has("AppWindow", SymKind::Class, 2);
    has("AppWindow::~AppWindow", SymKind::Function, 5);
    has("AppWindow::open", SymKind::Function, 6);
    has("AppWindow::operator==", SymKind::Function, 7);
    has("os::AppWindow::AppWindow", SymKind::Function, 12);
    has("maxOf", SymKind::Function, 17);
    has("c_entry", SymKind::Function, 19);
    has("numbers", SymKind::Global, 21);
    has("balls", SymKind::Global, 22);
    CHECK(find(s, "AppWindow")->depth == 1);           // inside the namespace
    CHECK(find(s, "AppWindow::open")->depth == 2);
    CHECK(find(s, "win") == nullptr);
    CHECK(find(s, "f") == nullptr);
    CHECK(find(s, "close") == nullptr);
}

TEST(outline_forward_declarations_are_not_globals)
{
    auto s = outlineSymbols("struct Foo;\nclass Bar;\nstruct Foo *ptr;\n", false);
    CHECK(find(s, "Foo") == nullptr);
    CHECK(find(s, "Bar") == nullptr);
    CHECK(find(s, "ptr") != nullptr);
}

TEST(outline_assembler)
{
    std::string t =
        "# fastsum.s\n"                       // 1
        "#define COUNT r4\n"                  // 2
        "\t.text\n"                           // 3
        "\t.globl fastsum\n"                  // 4
        "\t.type fastsum, @function\n"        // 5
        "fastsum:\n"                          // 6
        "\tli r5,0\n"                         // 7
        ".Lloop:\n"                           // 8
        "1:\tadd r5,r5,r6\n"                  // 9
        "helper:   # not a function\n"        // 10
        "\t.macro push reg\n"                 // 11
        "\t.endm\n"                           // 12
        "\t.equ STACK, 64\n"                  // 13
        "SIZE = 16\n";                        // 14
    auto s = outlineSymbols(t, true);
    std::string k = kinds(s);
    auto has = [&](const char *name, SymKind kind, int line) {
        const Symbol *x = find(s, name);
        if (!x || x->kind != kind || x->line != line)
            aftest::fail(__FILE__, __LINE__, std::string("expected ") + name + " in " + k);
    };
    has("COUNT", SymKind::Define, 2);
    has("fastsum", SymKind::Function, 6);
    has("helper", SymKind::Label, 10);
    has("push", SymKind::Macro, 11);
    has("STACK", SymKind::Define, 13);
    has("SIZE", SymKind::Define, 14);
    CHECK_EQ(find(s, "fastsum")->detail, std::string("global"));
    CHECK(find(s, ".Lloop") == nullptr);
    CHECK(find(s, "1") == nullptr);
}

TEST(outline_real_example_hello)
{
    // the shape of examples/hello/list.c
    std::string t =
        "#include \"list.h\"\n#include <stdlib.h>\n\n"
        "struct item *list_push(struct item *list, const char *name, int value)\n{\n"
        "    struct item *it = malloc(sizeof *it);\n    return it;\n}\n\n"
        "int list_sum(const struct item *list)\n{\n    int s = 0;\n    for (; list; list = list->next) s += list->value;\n    return s;\n}\n";
    auto s = outlineSymbols(t, false);
    CHECK_EQ(s.size(), (size_t)2);
    CHECK(find(s, "list_push") && find(s, "list_push")->line == 4);
    CHECK(find(s, "list_sum") && find(s, "list_sum")->line == 10);
}
