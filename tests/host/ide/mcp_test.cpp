// --- mcp --- McpCmd: escaping, envelopes, line edits; RexxCmd: the MCP command templates.
#include "aftest.h"
#include "ide/McpCmd.h"
#include "ide/RexxCmd.h"

using namespace af;

TEST(mcp_escape_roundtrip)
{
    std::string s = "a b\tc\nd|e\"f\\g\r\x01\xe9";
    std::string e = mcp::escape(s, false);
    CHECK_EQ(e, std::string("a b\\tc\\nd\\pe\\qf\\\\g\\r\\x01\\xE9"));
    CHECK_EQ(mcp::unescape(e), s);
    std::string w = mcp::escape(s, true);
    CHECK(w.find(' ') == std::string::npos);
    CHECK_EQ(mcp::unescape(w), s);
    for (unsigned char c : w) CHECK(c >= 0x20 && c < 0x7f);
    // unknown escapes and a lone backslash stay as they are
    CHECK_EQ(mcp::unescape("\\z\\"), std::string("\\z\\"));
    CHECK_EQ(mcp::unescape("\\x4"), std::string("\\x4"));
    CHECK_EQ(mcp::unescape("\\e"), std::string("\x1b"));
}

TEST(mcp_slots)
{
    std::map<int, std::string> slots = { {1, "int x;\n"} };
    std::string out;
    CHECK(mcp::resolve("\\@1", slots, out));
    CHECK_EQ(out, std::string("int x;\n"));
    CHECK(!mcp::resolve("\\@2", slots, out));
    CHECK(mcp::resolve("a\\sb", slots, out));
    CHECK_EQ(out, std::string("a b"));
    // the escaped text "\@1" (a backslash, then @1) is not a slot reference
    CHECK(mcp::resolve(mcp::escape("\\@1", true), slots, out));
    CHECK_EQ(out, std::string("\\@1"));
}

TEST(mcp_envelope_small)
{
    std::string rest;
    std::string env = mcp::envelope(0, "hello\nworld", 4000, rest);
    CHECK_EQ(env, std::string("AF1 0 0 12 hello\\nworld"));
    CHECK(rest.empty());
    int rc; size_t more; std::string p;
    CHECK(mcp::parseEnvelope(env, rc, more, p));
    CHECK_EQ(rc, 0);
    CHECK_EQ(more, (size_t)0);
    CHECK_EQ(p, std::string("hello\nworld"));
    env = mcp::envelope(10, "no project", 4000, rest);
    CHECK(mcp::parseEnvelope(env, rc, more, p));
    CHECK_EQ(rc, 10);
    CHECK_EQ(p, std::string("no project"));
    CHECK(!mcp::parseEnvelope("hello", rc, more, p));
    // empty payload
    env = mcp::envelope(0, "", 4000, rest);
    CHECK_EQ(env, std::string("AF1 0 0 0 "));
    CHECK(mcp::parseEnvelope("AF1 0 0 0", rc, more, p));
    CHECK_EQ(p, std::string(""));
}

TEST(mcp_envelope_trailing_spaces)
{
    std::string rest;
    std::string env = mcp::envelope(0, "x   ", 4000, rest);
    // the transport strips trailing spaces: the length puts them back
    std::string stripped = env;
    while (!stripped.empty() && stripped.back() == ' ') stripped.pop_back();
    int rc; size_t more; std::string p;
    CHECK(mcp::parseEnvelope(stripped, rc, more, p));
    CHECK_EQ(p, std::string("x   "));
}

TEST(mcp_envelope_paging)
{
    // a payload of escapes and plain text, paged in small chunks: never split a sequence
    std::string payload;
    for (int i = 0; i < 300; i++) payload += (i % 7 == 0) ? "\n" : (i % 11 == 0) ? "\xe9" : (i % 13 == 0) ? "\\" : "a";
    for (size_t chunk : { (size_t)5, (size_t)7, (size_t)64, (size_t)4000 }) {
        std::string rest, all;
        std::string env = mcp::envelope(0, payload, chunk, rest);
        int rc; size_t more; std::string p;
        int parts = 0;
        for (;;) {
            CHECK(mcp::parseEnvelope(env, rc, more, p));
            all += p;
            parts++;
            CHECK_EQ(more, rest.size());
            if (!more) break;
            env = mcp::nextChunk(rest, chunk);
            CHECK(parts < 1000);
            if (parts >= 1000) break;
        }
        CHECK_EQ(all, payload);
        if (chunk == 4000) CHECK_EQ(parts, 1);
        else CHECK(parts > 1);
    }
}

TEST(mcp_records)
{
    CHECK_EQ(mcp::record({"error", "main.c", "3", "x\ty", "a\nb\\c"}),
             std::string("error\tmain.c\t3\tx\\ty\ta\\nb\\\\c"));
    CHECK_EQ(mcp::kv("file", "a\nb"), std::string("file=a b"));
}

TEST(mcp_lines)
{
    std::string t = "one\ntwo\nthree\n";
    CHECK_EQ(mcp::lineCount(t), 4);
    CHECK_EQ(mcp::lineCount(""), 1);
    CHECK_EQ(mcp::lines(t, 2, 3), std::string("two\nthree"));
    CHECK_EQ(mcp::lines(t, 1, -1), std::string("one\ntwo\nthree\n"));
    CHECK_EQ(mcp::lines(t, 3, 99), std::string("three\n"));
    CHECK_EQ(mcp::lines(t, 9, 10), std::string(""));
    size_t s, e;
    std::string err;
    CHECK(mcp::lineRange(t, 2, 2, s, e, err));
    CHECK_EQ(s, (size_t)4);
    CHECK_EQ(e, (size_t)8);
    CHECK(!mcp::lineRange(t, 9, 9, s, e, err));
    CHECK(!mcp::lineRange(t, 3, 1, s, e, err));
}

static std::string R(const std::string &t, long from, long to, const std::string &repl)
{
    std::string out, with, err;
    size_t s, e;
    if (!mcp::replaceLines(t, from, to, repl, out, s, e, with, err)) return "ERR " + err;
    // the range + replacement describe the same edit
    CHECK_EQ(t.substr(0, s) + with + t.substr(e), out);
    return out;
}

TEST(mcp_replace_lines)
{
    std::string t = "a\nb\nc";
    CHECK_EQ(R(t, 2, 2, "B"), std::string("a\nB\nc"));
    CHECK_EQ(R(t, 3, 3, "C"), std::string("a\nb\nC"));
    CHECK_EQ(R(t, 1, 0, "top"), std::string("top\na\nb\nc"));          // insert before line 1
    CHECK_EQ(R(t, 2, 1, "x\ny"), std::string("a\nx\ny\nb\nc"));        // insert before line 2
    CHECK_EQ(R(t, 4, 3, "d"), std::string("a\nb\nc\nd"));              // append
    CHECK_EQ(R(t, 1, 2, ""), std::string("c"));                        // delete
    CHECK_EQ(R(t, 2, 3, "z\n"), std::string("a\nz\n"));                // text ending in a newline is kept
    std::string u = "a\nb\n";
    CHECK_EQ(R(u, 2, 2, "B"), std::string("a\nB\n"));
    CHECK_EQ(R(u, 1, 1, "A1\nA2"), std::string("A1\nA2\nb\n"));
    CHECK_EQ(R("", 1, 1, "x"), std::string("x"));
    CHECK(R(t, 7, 7, "x").compare(0, 3, "ERR") == 0);
}

TEST(mcp_substitute)
{
    std::string out, err;
    int line = 0;
    std::string t = "int main(void)\n{\n    return 0;\n}\n";
    CHECK_EQ(mcp::substitute(t, "return 0;", "return 1;", false, out, line, err), 1);
    CHECK_EQ(line, 3);
    CHECK(out.find("return 1;") != std::string::npos);
    CHECK_EQ(mcp::substitute(t, "nothing", "x", false, out, line, err), 0);
    CHECK(err.find("isn't in the file") != std::string::npos);
    std::string d = "x = 1; x = 1;";
    CHECK_EQ(mcp::substitute(d, "x = 1;", "y", false, out, line, err), 0);
    CHECK(err.find("2 times") != std::string::npos);
    CHECK_EQ(mcp::substitute(d, "x = 1;", "y", true, out, line, err), 2);
    CHECK_EQ(out, std::string("y y"));
    CHECK_EQ(mcp::substitute(d, "", "y", true, out, line, err), 0);
    CHECK_EQ(mcp::occurrences("aaaa", "aa").size(), (size_t)2);
}

TEST(mcp_names)
{
    CHECK_EQ(std::string(mcp::stopReasonName(0)), std::string("breakpoint"));
    CHECK_EQ(std::string(mcp::stopReasonName(4)), std::string("exception"));
    CHECK_EQ(std::string(mcp::stopReasonName(9)), std::string("unknown"));
    CHECK_EQ(std::string(mcp::debugStateName(3)), std::string("stopped"));
}

// the new commands parse what the devbench sends
static rexx::Args A(const char *cmd, const std::string &args, bool expectOk = true)
{
    const rexx::CommandDef *d = rexx::findCommand(cmd);
    CHECK(d != nullptr);
    rexx::Args a;
    std::string err;
    bool ok = d && rexx::parseArgs(d->tmpl, args, a, err);
    CHECK_EQ(ok, expectOk);
    return a;
}

TEST(mcp_command_templates)
{
    rexx::Args a = A("MCP", "SUBST FILE=main.c OLD=x\\s=\\s1; NEW=x\\s=\\s2;");
    CHECK_EQ(a.str("COMMAND"), std::string("SUBST FILE=main.c OLD=x\\s=\\s1; NEW=x\\s=\\s2;"));
    a = A("SUBST", "FILE=DH1:AF-mcp/hello/main.c OLD=x\\s=\\s1; NEW=x\\s=\\s2; ALL");
    CHECK_EQ(a.str("FILE"), std::string("DH1:AF-mcp/hello/main.c"));
    CHECK_EQ(mcp::unescape(a.str("OLD")), std::string("x = 1;"));
    CHECK_EQ(mcp::unescape(a.str("NEW")), std::string("x = 2;"));
    CHECK(a.has("ALL"));
    a = A("SUBST", "OLD=abc NEW=");
    CHECK_EQ(a.str("NEW"), std::string(""));
    CHECK(!a.has("FILE"));
    a = A("EDITLINES", "FILE=main.c FROMLINE=3 TOLINE=2 TEXT=\\@1");
    CHECK_EQ(a.num("FROMLINE"), 3L);
    CHECK_EQ(a.num("TOLINE"), 2L);
    CHECK_EQ(a.str("TEXT"), std::string("\\@1"));
    a = A("READ", "FILE=main.c FROMLINE=1 TOLINE=40");
    CHECK_EQ(a.num("TOLINE"), 40L);
    a = A("READ", "");
    CHECK(!a.has("FILE"));
    a = A("MCPBUF", "SLOT=1 CLEAR TEXT=int\\sx;\\n");
    CHECK(a.has("CLEAR"));
    CHECK_EQ(a.num("SLOT"), 1L);
    a = A("MCPBUF", "SLOT=2 TEXT=more");
    CHECK(!a.has("CLEAR"));
    a = A("BREAK", "FILE=main.c LINE=12 COND=i\\s==\\s3");
    CHECK_EQ(a.num("LINE"), 12L);
    CHECK_EQ(mcp::unescape(a.str("COND")), std::string("i == 3"));
    a = A("BREAK", "main.c 12");
    CHECK_EQ(a.str("FILE"), std::string("main.c"));
    a = A("UNBREAK", "ALL");
    CHECK(a.has("ALL"));
    a = A("EVAL", "p->next->value");
    CHECK_EQ(a.str("EXPR"), std::string("p->next->value"));
    a = A("EVAL", "CHILDREN *p");
    CHECK(a.has("CHILDREN"));
    CHECK_EQ(a.str("EXPR"), std::string("*p"));
    a = A("STARTBUILD", "REBUILD");
    CHECK(a.has("REBUILD"));
    a = A("NEWPROJECT", "TEMPLATE=console-c NAME=demo DRAWER=RAM:");
    CHECK_EQ(a.str("DRAWER"), std::string("RAM:"));
    A("NEWPROJECT", "TEMPLATE=console-c", false);
    a = A("RUNTO", "FILE=main.c LINE=9");
    CHECK_EQ(a.num("LINE"), 9L);
    a = A("FRAME", "1");
    CHECK_EQ(a.num("LEVEL"), 1L);
    A("DEBUGSTART", "MAIN");
    A("RUNPROGRAM", "CAPTURE");
    A("REFERENCES", "NAME=count");
    // every command has a unique name
    for (auto &c : rexx::commands()) CHECK(rexx::findCommand(c.name) == &c);
}
