// --- lang --- Languages, outlines, Python tooling, the key table, Python explanations.
#include "aftest.h"
#include "sci/Languages.h"
#include "Symbols.h"
#include "LangTools.h"
#include "KeyBindings.h"
#include "Commands.h"
#include "Snippets.h"
#include "Explain.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>

using namespace af;

// --- languages ---

TEST(lang_detects_by_extension)
{
    CHECK(languageFor("DH1:x/main.py") == Language::Python);
    CHECK(languageFor("t.PYW") == Language::Python);
    CHECK(languageFor("a.c") == Language::C);
    CHECK(languageFor("a.hpp") == Language::C);
    CHECK(languageFor("x.s") == Language::Asm);
    CHECK(languageFor("init.lua") == Language::Lua);
    CHECK(languageFor("tool.pl") == Language::Perl);
    CHECK(languageFor("build.sh") == Language::Shell);
    CHECK(languageFor("Rexx/SortLines.rexx") == Language::ARexx);
    CHECK(languageFor("x.rx") == Language::ARexx);
    CHECK(languageFor("data.json") == Language::JSON);
    CHECK(languageFor("ci.yml") == Language::YAML);
    CHECK(languageFor("README.md") == Language::Markdown);
    CHECK(languageFor("Makefile") == Language::Make);
    CHECK(languageFor("p.afp") == Language::Props);
    CHECK(languageFor("notes.txt") == Language::Text);
    CHECK(languageFor("python") == Language::Python);   // a bare language name
}

TEST(lang_detects_by_first_line_and_drawer)
{
    CHECK(languageFor("tool", "#!/usr/bin/env python3") == Language::Python);
    CHECK(languageFor("tool", "#!/bin/sh") == Language::Shell);
    CHECK(languageFor("tool", "#!/usr/bin/perl -w") == Language::Perl);
    CHECK(languageFor("S:Startup-Sequence", "; $VER: Startup-Sequence") == Language::AmigaDOS);
    CHECK(languageFor("SYS:S/User-Startup") == Language::AmigaDOS);
    CHECK(languageFor("DH1:scripts/backup", ".key FROM/A,TO/A") == Language::AmigaDOS);
    CHECK(languageFor("REXX:hello", "/* say hello */") == Language::ARexx);
    CHECK(languageFor("RAM:mymacro", "/* ARexx macro */") == Language::ARexx);
    CHECK(languageFor("RAM:notes", "/* notes */") == Language::Text);
    CHECK(languageFor("RAM:plain", "hello") == Language::Text);
}

TEST(lang_ids_round_trip)
{
    for (Language l : { Language::Python, Language::Lua, Language::ARexx, Language::AmigaDOS, Language::JSON,
                        Language::YAML, Language::Markdown, Language::Perl, Language::Shell, Language::C })
        CHECK(languageFromId(languageId(l)) == l);
    CHECK(languageFromId("nonsense") == Language::Text);
    CHECK_EQ(std::string(languageName(Language::Python)), std::string("Python"));
}

TEST(lang_comment_styles)
{
    CHECK_EQ(std::string(commentStyle(Language::Python).line), std::string("#"));
    CHECK_EQ(std::string(commentStyle(Language::C).line), std::string("//"));
    CHECK_EQ(std::string(commentStyle(Language::Lua).line), std::string("--"));
    CHECK_EQ(std::string(commentStyle(Language::AmigaDOS).line), std::string(";"));
    CHECK(commentStyle(Language::ARexx).line == nullptr);
    CHECK_EQ(std::string(commentStyle(Language::ARexx).blockStart), std::string("/*"));
    CHECK(commentStyle(Language::JSON).line == nullptr && commentStyle(Language::JSON).blockStart == nullptr);
}

TEST(lang_python_indentation)
{
    CHECK(indentRule(Language::Python) == IndentRule::Colon);
    CHECK_EQ(defaultIndentWidth(Language::Python), 4);
    CHECK_EQ(indentChangeAfter(Language::Python, "def f(x):"), 1);
    CHECK_EQ(indentChangeAfter(Language::Python, "    if x:   # a comment"), 1);
    CHECK_EQ(indentChangeAfter(Language::Python, "x = {'a:': 1}"), 0);   // the ':' is in a string
    CHECK_EQ(indentChangeAfter(Language::Python, "data = ["), 1);
    CHECK_EQ(indentChangeAfter(Language::Python, "        return x"), -1);
    CHECK_EQ(indentChangeAfter(Language::Python, "    pass"), -1);
    CHECK_EQ(indentChangeAfter(Language::Python, "    raise ValueError('x:')"), -1);
    CHECK_EQ(indentChangeAfter(Language::Python, "returned = 1"), 0);
    CHECK_EQ(indentChangeAfter(Language::Python, "print('hi')"), 0);
    CHECK_EQ(indentChangeAfter(Language::Python, "# comment:"), 0);
}

TEST(lang_block_keyword_indentation)
{
    CHECK_EQ(indentChangeAfter(Language::Lua, "function f(x)"), 1);
    CHECK_EQ(indentChangeAfter(Language::Lua, "local function g()"), 1);
    CHECK_EQ(indentChangeAfter(Language::Lua, "if x then"), 1);
    CHECK_EQ(indentChangeAfter(Language::Lua, "for i = 1, 3 do"), 1);
    CHECK_EQ(indentChangeAfter(Language::Lua, "f = function() return 1 end"), 0);
    CHECK_EQ(indentChangeAfter(Language::Lua, "else"), 1);
    CHECK_EQ(indentChangeAfter(Language::Lua, "-- then"), 0);
    CHECK_EQ(indentChangeAfter(Language::ARexx, "IF x > 1 THEN DO"), 1);
    CHECK_EQ(indentChangeAfter(Language::ARexx, "do i = 1 to 10"), 1);
    CHECK_EQ(indentChangeAfter(Language::ARexx, "SELECT"), 1);
    CHECK_EQ(indentChangeAfter(Language::ARexx, "SAY 'do'  /* do */"), 0);
    CHECK_EQ(indentChangeAfter(Language::ARexx, "IF x THEN SAY 1"), 0);
    CHECK_EQ(indentChangeAfter(Language::AmigaDOS, "If EXISTS SYS:MUI"), 1);
    CHECK_EQ(indentChangeAfter(Language::AmigaDOS, "Else"), 1);
    CHECK_EQ(indentChangeAfter(Language::AmigaDOS, "; If not"), 0);
    CHECK_EQ(indentChangeAfter(Language::YAML, "server:"), 1);
    CHECK(indentRule(Language::C) == IndentRule::Braces);
    CHECK(usesTabs(Language::Make) && !usesTabs(Language::Python));
    CHECK(foldable(Language::Python) && foldable(Language::ARexx) && !foldable(Language::Markdown));
}

TEST(lang_strip_line_comment)
{
    CHECK_EQ(stripLineComment(Language::Python, "x = '#not' # yes"), std::string("x = '#not'"));
    CHECK_EQ(stripLineComment(Language::Lua, "x = 1 -- c"), std::string("x = 1"));
    CHECK_EQ(stripLineComment(Language::ARexx, "say 1 /* c */"), std::string("say 1"));
    CHECK_EQ(stripLineComment(Language::AmigaDOS, "echo \"a;b\" ; c"), std::string("echo \"a;b\""));
}

TEST(lang_keyword_lists)
{
    std::string py = languageKeywords(Language::Python, 0);
    CHECK(py.find("def") != std::string::npos && py.find("lambda") != std::string::npos);
    CHECK(std::string(languageKeywords(Language::Python, 1)).find("print") != std::string::npos);
    CHECK(std::string(languageKeywords(Language::ARexx, 0)).find("parse") != std::string::npos);
    CHECK(std::string(languageKeywords(Language::ARexx, 1)).find("words") != std::string::npos);
    CHECK_EQ(std::string(languageKeywords(Language::C, 0)), std::string(""));
}

// --- outline, references ---

static std::string outlineText(const std::vector<Symbol> &v)
{
    std::string s;
    for (auto &x : v) s += std::string(x.depth * 2, ' ') + symKindName(x.kind) + " " + x.name + ":" + std::to_string(x.line) + x.detail + "\n";
    return s;
}

TEST(lang_python_outline)
{
    const char *src =
        "\"\"\"Docstring with def fake(): inside\"\"\"\n"     // 1
        "import os\n"                                       // 2
        "MAX = 10\n"                                        // 3
        "a, b = 1, 2\n"                                     // 4
        "name: str = 'x'\n"                                 // 5
        "if MAX == 3:\n"                                    // 6
        "    pass\n"                                        // 7
        "\n"                                                // 8
        "class Ball(object):\n"                             // 9
        "    speed = 3\n"                                   // 10
        "    def __init__(self, x,\n"                       // 11
        "                 y=0):\n"                          // 12
        "        self.x = x\n"                              // 13
        "    async def run(self):\n"                        // 14
        "        def inner():  # def nope(): in a comment\n" // 15
        "            return 1\n"                            // 16
        "\n"                                                // 17
        "def main(argv=None):\n"                            // 18
        "    s = '''\n"                                     // 19
        "def notreal():\n"                                  // 20
        "'''\n"                                             // 21
        "    return 0\n";                                   // 22
    std::string got = outlineText(outlineSymbols(src, Language::Python));
    std::string want =
        "variable MAX:3\n"
        "variable a:4\n"
        "variable b:4\n"
        "variable name:5\n"
        "class Ball:9\n"
        "  method __init__:11(self, x, y=0)\n"
        "  method run:14(self)\n"
        "    function inner:15()\n"
        "function main:18(argv=None)\n";
    CHECK_EQ(got, want);
}

TEST(lang_other_outlines)
{
    CHECK_EQ(outlineText(outlineSymbols("local function a(x)\nfunction M.b:c()\nend\nf = function(y) end\n-- function no()\n",
                                        Language::Lua)),
             std::string("function a:1(x)\nfunction M.b:c:2()\nfunction f:4(y)\n"));
    CHECK_EQ(outlineText(outlineSymbols("package Foo::Bar;\nsub new {\n}\n# sub no\n", Language::Perl)),
             std::string("package Foo::Bar:1\nfunction new:2\n"));
    CHECK_EQ(outlineText(outlineSymbols("build() {\n}\nfunction clean {\n}\n# x() {\n", Language::Shell)),
             std::string("function build:1\nfunction clean:3\n"));
    CHECK_EQ(outlineText(outlineSymbols("/* m */\nSAY 1\nCALL Sub\nEXIT\nSub: PROCEDURE\nRETURN\nError: SAY 'x'\n/* No: */\n", Language::ARexx)),
             std::string("function Sub:5\nlabel Error:7\n"));
    CHECK_EQ(outlineText(outlineSymbols(".key X\nLab loop\nSkip loop BACK\n; Lab no\nlab End\n", Language::AmigaDOS)),
             std::string("label loop:2\nlabel End:5\n"));
    CHECK_EQ(outlineText(outlineSymbols("# Title\ntext\n## Part #\n```\n# not a heading\n```\n### Deep\n#nospace\n", Language::Markdown)),
             std::string("heading Title:1\n  heading Part:3\n    heading Deep:7\n"));
    CHECK_EQ(outlineText(outlineSymbols("# c\nname: x\nserver:\n  port: 1\n- item\nurl: http://a\n", Language::YAML)),
             std::string("key name:2\nkey server:3\nkey url:6\n"));
    CHECK_EQ(outlineText(outlineSymbols("{\n  \"a\": 1,\n  \"b\": {\"c\": 2},\n  \"d\": [\"x\"]\n}\n", Language::JSON)),
             std::string("key a:2\nkey b:3\nkey d:4\n"));
    CHECK(hasOutline(Language::Python) && !hasOutline(Language::Text));
}

TEST(lang_python_references_skip_comments_and_strings)
{
    std::string t = "count = 0  # count here\ns = 'count'\nt = \"\"\"count\"\"\"\ncount += 1\nprint(count)\nrecount = 2\n";
    CHECK_EQ(findIdentifier(t, "count", Language::Python).size(), (size_t)3);
    int n = 0;
    std::string r = replaceIdentifier(t, "count", "total", Language::Python, &n);
    CHECK_EQ(n, 3);
    CHECK(r.find("# count here") != std::string::npos && r.find("recount") != std::string::npos);
    CHECK(r.find("print(total)") != std::string::npos);
    CHECK(isReservedWord("def", Language::Python) && !isReservedWord("count", Language::Python));
}

TEST(lang_arexx_references_ignore_case)
{
    std::string t = "/* Count */\ncount = 1\nSAY COUNT 'count'\nCount.1 = 2\ncounter = 3\n";
    // count, COUNT, Count (in Count.1); not the comment, the string or counter
    CHECK_EQ(findIdentifier(t, "count", Language::ARexx).size(), (size_t)3);
    CHECK(isReservedWord("PARSE", Language::ARexx));
}

TEST(lang_lua_and_shell_masking)
{
    std::string lua = "x = 1 --[[ x ]] y = [[x]] -- x\nprint(x)\n";
    CHECK_EQ(findIdentifier(lua, "x", Language::Lua).size(), (size_t)2);
    std::string sh = "x=1 # x\necho \"$x\" '$x' $x\n";
    CHECK_EQ(findIdentifier(sh, "x", Language::Shell).size(), (size_t)2);
}

// --- Python tooling ---

TEST(lang_traceback_parser)
{
    // output of the AmigaOS 4 port (seen on the guest)
    lang::TracebackParser p;
    lang::Diag d = p.feed("before");
    CHECK(d.kind == lang::DiagKind::None);
    d = p.feed("Traceback (most recent call last):");
    CHECK(d.kind == lang::DiagKind::Info);
    d = p.feed("  File \"DH1:AF-lang/exc.py\", line 4, in <module>");
    CHECK(d.kind == lang::DiagKind::Location);
    CHECK_EQ(d.file, std::string("DH1:AF-lang/exc.py"));
    CHECK_EQ(d.line, 4);
    d = p.feed("  File \"DH1:AF-lang/exc.py\", line 2, in f");
    CHECK_EQ(d.line, 2);
    CHECK(p.feed("    f()").kind == lang::DiagKind::None);
    d = p.feed("ZeroDivisionError: division by zero");
    CHECK(d.kind == lang::DiagKind::Error);
    CHECK_EQ(d.file, std::string("DH1:AF-lang/exc.py"));
    CHECK_EQ(d.line, 2);
    CHECK_EQ(d.text, std::string("ZeroDivisionError: division by zero"));
    // a later line that looks like an exception, with no traceback: ordinary output
    CHECK(p.feed("ValueError: printed by the program").kind == lang::DiagKind::None);
}

TEST(lang_traceback_parser_syntax_error_and_warnings)
{
    lang::TracebackParser p;
    lang::Diag d = p.feed("  File \"DH1:AF-lang/bad.py\", line 1");
    CHECK(d.kind == lang::DiagKind::Location);
    CHECK(p.feed("    def f(:").kind == lang::DiagKind::None);
    CHECK(p.feed("          ^").kind == lang::DiagKind::None);
    d = p.feed("SyntaxError: invalid syntax");
    CHECK(d.kind == lang::DiagKind::Error);
    CHECK_EQ(d.line, 1);
    d = p.feed("main.py:7: SyntaxWarning: invalid escape sequence '\\d'");
    CHECK(d.kind == lang::DiagKind::Warning);
    CHECK_EQ(d.file, std::string("main.py"));
    CHECK_EQ(d.line, 7);
    CHECK(p.feed("  File \"<string>\", line 1").kind == lang::DiagKind::None);
    CHECK(p.feed("json.decoder.JSONDecodeError: x").kind == lang::DiagKind::None);   // no location yet
    CHECK_EQ(lang::exceptionName("json.decoder.JSONDecodeError: Expecting value"), std::string("json.decoder.JSONDecodeError"));
    CHECK_EQ(lang::exceptionName("KeyboardInterrupt"), std::string("KeyboardInterrupt"));
    CHECK_EQ(lang::exceptionName("Hello: world"), std::string(""));
    CHECK_EQ(lang::exceptionName("error: x"), std::string(""));
}

TEST(lang_find_interpreter)
{
    std::set<std::string> files = { "DH1:python-os4", "DH1:lib/os.py", "Work:Python/python3", "Work:Python/lib/os.py" };
    auto exists = [&](const std::string &p) { return files.count(p) != 0; };
    std::string how;
    CHECK_EQ(lang::findInterpreter("", "", exists, &how), std::string("DH1:python-os4"));
    CHECK_EQ(how, std::string("found"));
    CHECK_EQ(lang::findInterpreter("  Work:Python/python3 ", "DH1:python-os4", exists, &how), std::string("Work:Python/python3"));
    CHECK_EQ(how, std::string("project setting"));
    CHECK_EQ(lang::findInterpreter("", "C:python3", exists, &how), std::string("C:python3"));
    CHECK_EQ(how, std::string("ENV:AmiForge/Python"));
    files.clear();
    CHECK_EQ(lang::findInterpreter("", "", exists, &how), std::string(""));
    files = { "C:python3", "DH1:python-os4" };
    CHECK_EQ(lang::findInterpreter("", "", exists), std::string("C:python3"));   // C: before DH1:
    files = { "C:python", "Python:python" };   // AmigaOS 4.1's Python 2.5: never picked
    CHECK_EQ(lang::findInterpreter("", "", exists), std::string(""));
    files = { "DH1:lib/os.py", "Work:Python/lib/os.py" };
    CHECK_EQ(lang::pythonHomeFor("DH1:python-os4", exists), std::string("DH1:"));
    CHECK_EQ(lang::pythonHomeFor("Work:Python/python3", exists), std::string("Work:Python"));
    CHECK_EQ(lang::pythonHomeFor("C:python3", exists), std::string(""));
    CHECK_EQ(lang::pythonHomeFor("python3", exists), std::string(""));
    CHECK_EQ(lang::pythonPathFor("DH1:"), std::string("DH1:lib"));
    CHECK_EQ(lang::pythonPathFor("Work:Python"), std::string("Work:Python/lib"));
}

TEST(lang_command_lines)
{
    CHECK_EQ(lang::quoteArg("main.py"), std::string("main.py"));
    CHECK_EQ(lang::quoteArg("My Drawer/x.py"), std::string("\"My Drawer/x.py\""));
    CHECK_EQ(lang::quoteArg("say \"hi\""), std::string("\"say *\"hi*\"\""));
    CHECK_EQ(lang::quoteArg(""), std::string("\"\""));
    CHECK_EQ(lang::checkCommand("DH1:python-os4", "T:AmiForge-pycheck.py", { "main.py", "lib/util.py" }),
             std::string("DH1:python-os4 T:AmiForge-pycheck.py main.py lib/util.py"));
    CHECK_EQ(lang::runCommand("DH1:python-os4", "T:r.py", "T:s.1", "T:t.1", "main.py", " -v \"a b\" "),
             std::string("DH1:python-os4 -u T:r.py T:s.1 T:t.1 main.py -v \"a b\""));
    CHECK_EQ(lang::pdbCommand("C:python3", "main.py", "", lang::pdbBreakCommands({ { "main.py", 12, "" }, { "u.py", 3, "x > 2" } })),
             std::string("C:python3 -m pdb -c \"b main.py:12\" -c \"b u.py:3, x > 2\" -c continue main.py"));
    CHECK(lang::pdbBreakCommands({}).empty());
    CHECK(lang::isPythonFile("a/b.py") && lang::isPythonFile("X.PYW") && !lang::isPythonFile("py") &&
          !lang::isPythonFile("a.py/b"));
}

// The checker and the runner really work: run them with the host's python3.
static std::string scratch()
{
    static std::string dir;
    if (dir.empty()) {
        char tmpl[] = "/tmp/aflangXXXXXX";
        dir = mkdtemp(tmpl);
    }
    return dir;
}
static void put(const std::string &path, const std::string &text) { std::ofstream(path) << text; }
static std::string get(const std::string &path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static std::string run(const std::string &cmd, int *rc = nullptr)
{
    std::string out;
    FILE *p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    int st = pclose(p);
    if (rc) *rc = WEXITSTATUS(st);
    return out;
}
static bool havePython() { return system("python3 -c pass >/dev/null 2>&1") == 0; }

TEST(lang_checker_script_reports_errors_like_gcc)
{
    if (!havePython()) { std::printf("  (no python3: skipped)\n"); return; }
    std::string d = scratch();
    put(d + "/check.py", lang::pyCheckScript);
    put(d + "/ok.py", "print('fine')\n");
    put(d + "/bad.py", "x = 1\ndef f(:\n    pass\n");
    put(d + "/ind.py", "if True:\nprint(1)\n");
    put(d + "/warn.py", "s = '\\d'\n");
    int rc = -1;
    std::string out = run("cd " + d + " && python3 check.py ok.py bad.py ind.py warn.py", &rc);
    CHECK_EQ(rc, 1);
    CHECK(out.find("bad.py:2:7: error: SyntaxError: invalid syntax") != std::string::npos);
    CHECK(out.find("ind.py:2:1: error: IndentationError: expected an indented block") != std::string::npos);
    // SyntaxWarning in Python 3.12 (the AmigaOS port), DeprecationWarning before
    CHECK(out.find("warn.py:1:1: warning: ") != std::string::npos && out.find("Warning: invalid escape sequence") != std::string::npos);
    CHECK(out.find("Checked 4 files: 2 with errors.") != std::string::npos);
    CHECK(out.find("ok.py") == std::string::npos);
    rc = -1;
    out = run("cd " + d + " && python3 check.py ok.py", &rc);
    CHECK_EQ(rc, 0);
    CHECK(out.find("Checked 1 file: no errors.") != std::string::npos);
    // nothing written next to the sources
    CHECK(run("ls " + d).find("__pycache__") == std::string::npos);
}

TEST(lang_runner_script_traceback_and_stop)
{
    if (!havePython()) { std::printf("  (no python3: skipped)\n"); return; }
    std::string d = scratch();
    put(d + "/run.py", lang::pyRunScript);
    put(d + "/helper.py", "def boom(x):\n    return 1 / x\n");
    put(d + "/main.py", "import sys, helper\nprint('args', sys.argv[1:])\nprint('name', __name__)\nhelper.boom(0)\n");
    int rc = -1;
    std::string out = run("cd " + d + " && rm -f tb stop && python3 -u run.py stop tb main.py a 'b c'", &rc);
    CHECK_EQ(rc, 10);
    CHECK(out.find("args ['a', 'b c']") != std::string::npos);
    CHECK(out.find("name __main__") != std::string::npos);
    std::string tb = get(d + "/tb");
    CHECK(tb.find("File \"main.py\", line 4") != std::string::npos);
    CHECK(tb.find("helper.py\", line 2, in boom") != std::string::npos);   // imported modules: absolute paths
    CHECK(tb.find("ZeroDivisionError") != std::string::npos);
    CHECK(tb.find("runpy") == std::string::npos);     // the runner's own frames are left out
    // the traceback parses into a clickable error
    lang::TracebackParser p;
    lang::Diag last;
    std::istringstream in(tb);
    std::string line;
    while (std::getline(in, line)) { lang::Diag x = p.feed(line); if (x.kind == lang::DiagKind::Error) last = x; }
    CHECK(last.file.size() >= 9 && last.file.compare(last.file.size() - 9, 9, "helper.py") == 0);
    CHECK_EQ(last.line, 2);
    // Stop Program: the stop file turns into a KeyboardInterrupt
    put(d + "/loop.py", "import time\nwhile True:\n    time.sleep(0.05)\n");
    out = run("cd " + d + " && rm -f tb stop && (sleep 1; touch stop) & cd " + d + " && python3 -u run.py stop tb loop.py", &rc);
    CHECK_EQ(rc, 5);
    CHECK(out.find("Stopped (KeyboardInterrupt).") != std::string::npos);
    // a long sleep is cut short too
    put(d + "/long.py", "import time\ntime.sleep(60)\n");
    out = run("cd " + d + " && rm -f tb stop && (sleep 1; touch stop) & cd " + d + " && python3 -u run.py stop tb long.py", &rc);
    CHECK_EQ(rc, 5);
    // no thread: a script that never sleeps runs on (here it ends by itself)
    put(d + "/count.py", "n = 0\nfor i in range(200000):\n    n += i\nprint('sum', n)\n");
    out = run("cd " + d + " && rm -f tb && touch stop && python3 -u run.py stop tb count.py", &rc);
    CHECK_EQ(rc, 0);
    CHECK(out.find("sum 19999900000") != std::string::npos);
    // a syntax error in the script: its location, not the runner's
    put(d + "/syn.py", "x = 1\nif x\n    pass\n");
    run("cd " + d + " && rm -f tb stop && python3 run.py stop tb syn.py", &rc);
    CHECK_EQ(rc, 10);
    tb = get(d + "/tb");
    CHECK(tb.find("File \"syn.py\", line 2") != std::string::npos);
    CHECK(tb.find("run.py") == std::string::npos);
    // sys.exit keeps its code, no traceback
    put(d + "/exit.py", "import sys\nsys.exit(3)\n");
    run("cd " + d + " && rm -f tb stop && python3 run.py stop tb exit.py", &rc);
    CHECK_EQ(rc, 3);
    CHECK(get(d + "/tb").empty());
}

// --- key table ---

TEST(keys_every_binding_is_unique)
{
    std::set<std::pair<int, int>> seen;
    for (auto &b : keys::bindings()) {
        bool fresh = seen.insert({ b.rawkey, b.mods }).second;
        if (!fresh) aftest::fail(__FILE__, __LINE__, "bound twice: " + keys::keyName(b.rawkey, b.mods));
        CHECK(b.cmd != 0);
        CHECK(b.what && *b.what);
    }
}

TEST(keys_lookup)
{
    using namespace keys;
    CHECK_EQ(commandFor(RAW_F5, 0), (uint32_t)CMD_DEBUG_START);
    CHECK_EQ(commandFor(RAW_F5, SHIFT), (uint32_t)CMD_DEBUG_STOP);
    CHECK_EQ(commandFor(RAW_F5, CTRL), (uint32_t)CMD_RUN);
    CHECK_EQ(commandFor(RAW_F5, SHIFT | CTRL), (uint32_t)CMD_STOP_PROGRAM);
    CHECK_EQ(commandFor(RAW_F5, ALT), (uint32_t)CMD_DEBUG_PAUSE);
    CHECK_EQ(commandFor(RAW_F5, SHIFT | ALT), (uint32_t)CMD_DEBUG_RESTART);
    CHECK_EQ(commandFor(RAW_BREAK, 0), (uint32_t)CMD_DEBUG_PAUSE);
    CHECK_EQ(commandFor(RAW_BREAK, CTRL), (uint32_t)CMD_DEBUG_PAUSE);      // Ctrl-Break: still pause
    CHECK_EQ(commandFor(RAW_F9, 0), (uint32_t)CMD_TOGGLE_BP);
    CHECK_EQ(commandFor(RAW_F9, SHIFT), (uint32_t)CMD_QUICK_WATCH);
    CHECK_EQ(commandFor(RAW_F9, CTRL), (uint32_t)CMD_APPLY_CHANGES);
    CHECK_EQ(commandFor(RAW_F9, ALT), (uint32_t)CMD_BP_TOGGLE_ENABLED);
    CHECK_EQ(commandFor(RAW_F9, SHIFT | CTRL), (uint32_t)CMD_BP_CONDITION);
    CHECK_EQ(commandFor(RAW_F10, 0), (uint32_t)CMD_STEP_OVER);
    CHECK_EQ(commandFor(RAW_F10, CTRL), (uint32_t)CMD_RUN_TO_CURSOR);
    CHECK_EQ(commandFor(RAW_F10, SHIFT | CTRL), (uint32_t)CMD_SET_NEXT_STATEMENT);
    CHECK_EQ(commandFor(RAW_F10, ALT), (uint32_t)CMD_RUN_TO_FUNCTION);
    CHECK_EQ(commandFor(RAW_F11, 0), (uint32_t)CMD_STEP_INTO);
    CHECK_EQ(commandFor(RAW_F11, SHIFT), (uint32_t)CMD_STEP_OUT);
    CHECK_EQ(commandFor(RAW_F12, SHIFT), (uint32_t)CMD_FIND_REFS);
    CHECK_EQ(commandFor(RAW_F7, SHIFT), (uint32_t)CMD_PROFILE);
    // unbound combinations fall back to the plain key, as before
    CHECK_EQ(commandFor(RAW_F7, CTRL), (uint32_t)CMD_BUILD);
    CHECK_EQ(commandFor(RAW_F8, SHIFT | CTRL | ALT), (uint32_t)CMD_STEP_INSN);
    CHECK_EQ(commandFor(RAW_F11, SHIFT | ALT), (uint32_t)CMD_STEP_OUT);
    CHECK_EQ(commandFor(0x20, 0), (uint32_t)0);
    CHECK(isGlobalKey(RAW_F11) && isGlobalKey(RAW_BREAK) && isGlobalKey(RAW_HELP) && !isGlobalKey(0x45));
    CHECK_EQ((int)modsFromQualifier(0x0001 | 0x0008), (int)(SHIFT | CTRL));
    CHECK_EQ((int)modsFromQualifier(0x0020), (int)ALT);
    CHECK_EQ((int)modsFromQualifier(0x8000), 0);
}

TEST(keys_labels_match_the_menus)
{
    // the menus show these labels (MainWindow.cpp); a changed binding must change them too
    CHECK_EQ(keys::keyFor(CMD_DEBUG_PAUSE), std::string("Alt F5"));
    CHECK_EQ(keys::keyFor(CMD_DEBUG_RESTART), std::string("Shift Alt F5"));
    CHECK_EQ(keys::keyFor(CMD_QUICK_WATCH), std::string("Shift F9"));
    CHECK_EQ(keys::keyFor(CMD_BP_TOGGLE_ENABLED), std::string("Alt F9"));
    CHECK_EQ(keys::keyFor(CMD_BP_CONDITION), std::string("Shift Ctrl F9"));
    CHECK_EQ(keys::keyFor(CMD_SET_NEXT_STATEMENT), std::string("Shift Ctrl F10"));
    CHECK_EQ(keys::keyFor(CMD_RUN_TO_FUNCTION), std::string("Alt F10"));
    CHECK_EQ(keys::keyFor(CMD_STOP_PROGRAM), std::string("Shift Ctrl F5"));
    CHECK_EQ(keys::keyFor(CMD_STEP_OUT), std::string("Shift F11"));
    CHECK_EQ(keys::keyName(keys::RAW_BREAK, 0), std::string("Break"));
    std::string d = keys::describe({ CMD_STEP_OVER, CMD_DEBUG_RESTART });
    CHECK(d.find("F10") != std::string::npos && d.find("restart debugging") != std::string::npos);
    // every new debugger command has a key
    for (uint32_t c = CMD_DEBUG_RESTART; c <= CMD_RUN_TO_FUNCTION; c++) CHECK(!keys::keyFor(c).empty());
}

// --- snippets ---

TEST(lang_python_snippets)
{
    const auto &all = builtinSnippets();
    for (const char *n : { "main", "ifmain", "def", "class", "for", "with", "try", "argparse" }) {
        const Snippet *s = findSnippet(all, n, "python");
        CHECK(s != nullptr);
        if (s) CHECK_EQ(s->lang, std::string("python"));
    }
    CHECK(findSnippet(all, "main", "python")->body.find("__main__") != std::string::npos);
    CHECK(findSnippet(all, "main", "c")->body.find("int main") != std::string::npos);
    CHECK(findSnippet(all, "openlib", "python") == nullptr);
    CHECK(findSnippet(all, "rexx", "arexx") != nullptr);
    CHECK(findSnippet(all, "rexx", "c") != nullptr);       // the C one (sending an ARexx command)
    for (auto &s : all) {
        auto e = expandSnippet(s.body, "");
        CHECK(e.text.find("${") == std::string::npos);
    }
    auto user = parseSnippets("snippet hi  c\n\tprintf(\"hi\");\nlanguage python\nsnippet hi  py\n\tprint('hi')\nlanguage c\nsnippet yo\n\tyo();\n");
    CHECK_EQ(user.size(), (size_t)3);
    CHECK_EQ(user[1].lang, std::string("python"));
    CHECK_EQ(user[2].lang, std::string(""));
    auto merged = mergeSnippets(all, user);
    CHECK_EQ(findSnippet(merged, "hi", "python")->body, std::string("print('hi')"));
    CHECK_EQ(findSnippet(merged, "hi", "c")->body, std::string("printf(\"hi\");"));
    CHECK(snippetForLanguage(*findSnippet(merged, "yo", "c"), "") && !snippetForLanguage(*findSnippet(merged, "yo", "c"), "lua"));
}

// --- explanations ---

TEST(lang_python_explanations)
{
    auto rule = [](const std::string &m, const std::string &src = "") { return explainDiagnostic(m, src).rule; };
    CHECK_EQ(rule("main.py:2:1: error: IndentationError: expected an indented block after 'if' statement on line 1"),
             std::string("py-indent-expected"));
    CHECK_EQ(rule("IndentationError: unexpected indent"), std::string("py-indent-unexpected"));
    CHECK_EQ(rule("IndentationError: unindent does not match any outer indentation level"), std::string("py-indent-mismatch"));
    CHECK_EQ(rule("TabError: inconsistent use of tabs and spaces in indentation"), std::string("py-tab"));
    CHECK_EQ(rule("x.py:3:9: error: SyntaxError: expected ':'"), std::string("py-colon"));
    CHECK_EQ(rule("SyntaxError: '(' was never closed"), std::string("py-unclosed"));
    CHECK_EQ(rule("SyntaxError: unterminated string literal (detected at line 4)"), std::string("py-string"));
    CHECK_EQ(rule("SyntaxError: Missing parentheses in call to 'print'. Did you mean print(...)?"), std::string("py-print"));
    CHECK_EQ(rule("SyntaxError: invalid syntax"), std::string("py-syntax"));
    CHECK_EQ(rule("NameError: name 'os' is not defined"), std::string("py-name-import"));
    Explanation e = explainDiagnostic("NameError: name 'cuont' is not defined. Did you mean: 'count'?");
    CHECK_EQ(e.rule, std::string("py-name"));
    CHECK(e.title.find("cuont") != std::string::npos);
    e = explainDiagnostic("ModuleNotFoundError: No module named 'numpy'");
    CHECK_EQ(e.rule, std::string("py-module"));
    CHECK(e.text.find(".so") != std::string::npos);
    CHECK_EQ(rule("UnicodeDecodeError: 'utf-8' codec can't decode byte 0xe4"), std::string("py-unicode"));
    CHECK_EQ(rule("RecursionError: maximum recursion depth exceeded"), std::string("py-recursion"));
    // Python 2 can't compile AmiForge's checker (seen with OS4.1's C:python)
    CHECK_EQ(rule("T:AmiForge-pycheck.py:7: Warning: 'with' will become a reserved keyword in Python 2.6"), std::string("py-version2"));
    CHECK_EQ(rule("SyntaxError: invalid syntax", ""), std::string("py-syntax"));
    CHECK_EQ(rule("FileNotFoundError: [Errno 2] No such file or directory: '/tmp/x'"), std::string("py-file"));
    // C messages still get their own rules
    CHECK(rule("main.c:3:5: error: implicit declaration of function 'OpenWindowTags'").find("py-") != 0);
}
