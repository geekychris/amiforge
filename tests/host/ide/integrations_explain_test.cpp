// Explanations, one test per rule, with real GCC 11.5 / binutils messages from the SDK's
// toolchain (captured with typographic quotes, as a UTF-8 locale prints them; the Amiga's C
// locale prints ASCII quotes, which is tested too).
#include "aftest.h"
#include "ide/Explain.h"

using namespace af;

static std::string R(const std::string &msg, const std::string &src = "")
{
    return explainDiagnostic(msg, src).rule;
}

TEST(explain_os_implicit_declaration)
{
    Explanation e = explainDiagnostic("c1.c:6:24: warning: implicit declaration of function \xE2\x80\x98OpenWindowTags\xE2\x80\x99 [-Wimplicit-function-declaration]");
    CHECK_EQ(e.rule, std::string("os-implicit-decl"));
    CHECK(e.text.find("IIntuition->OpenWindowTags") != std::string::npos);
    CHECK(e.text.find("proto/intuition.h") != std::string::npos);
    CHECK_EQ(R("c1.c:12:5: warning: implicit declaration of function 'Delay' [-Wimplicit-function-declaration]"), std::string("os-implicit-decl"));
    // a lookup (the AutoDocs index) is used before the built-in table
    Explanation l = explainDiagnostic("x.c:1:1: warning: implicit declaration of function 'ObtainPens'", "",
                                      [](const std::string &f) { return f == "ObtainPens" ? std::string("pens.library") : std::string(); });
    CHECK_EQ(l.rule, std::string("os-implicit-decl"));
    CHECK(l.text.find("IPens->ObtainPens") != std::string::npos);
    // C++: "was not declared in this scope"
    CHECK_EQ(R("c2.cpp:8:5: error: 'CloseWindow' was not declared in this scope"), std::string("os-implicit-decl"));
}

TEST(explain_std_and_unknown_functions)
{
    Explanation e = explainDiagnostic("a.c:3:5: warning: implicit declaration of function 'printf' [-Wimplicit-function-declaration]");
    CHECK_EQ(e.rule, std::string("std-implicit-decl"));
    CHECK(e.title.find("stdio.h") != std::string::npos);
    CHECK_EQ(R("c2.cpp:8:5: error: \xE2\x80\x98" "foo\xE2\x80\x99 was not declared in this scope"), std::string("not-declared"));
    CHECK_EQ(R("a.c:3:5: warning: implicit declaration of function 'my_helper'"), std::string("implicit-decl"));
}

TEST(explain_use_inline_clash)
{
    Explanation e = explainDiagnostic("c6.cpp:4:36: error: macro \"Remove\" passed 2 arguments, but takes just 1");
    CHECK_EQ(e.rule, std::string("use-inline-clash"));
    CHECK(e.text.find("__USE_INLINE__") != std::string::npos);
}

TEST(explain_constants)
{
    Explanation e = explainDiagnostic("c7.c:3:80: error: \xE2\x80\x98MEMF_ANY\xE2\x80\x99 undeclared (first use in this function)");
    CHECK_EQ(e.rule, std::string("os-constant-undeclared"));
    CHECK(e.text.find("exec/memory.h") != std::string::npos);
    CHECK(explainDiagnostic("c10.c:2:101: error: 'WA_Width' undeclared (first use in this function)").text.find("intuition/intuition.h") != std::string::npos);
    CHECK_EQ(R("c10.c:2:78: error: 'unknownvar' undeclared (first use in this function)"), std::string("undeclared"));
    CHECK_EQ(R("x.c:1:1: error: 'NULL' undeclared (first use in this function)"), std::string("null-undeclared"));
}

TEST(explain_amiga_types)
{
    CHECK_EQ(R("c11.c:1:1: error: unknown type name \xE2\x80\x98" "BOOL\xE2\x80\x99"), std::string("amiga-type"));
    CHECK_EQ(R("c11.c:2:1: error: unknown type name 'uint32'"), std::string("amiga-type"));
    CHECK_EQ(R("c11.c:2:1: error: unknown type name 'Widget'"), std::string("unknown-type"));
}

TEST(explain_formats)
{
    CHECK_EQ(R("c1.c:9:14: warning: format \xE2\x80\x98%d\xE2\x80\x99 expects argument of type \xE2\x80\x98int\xE2\x80\x99, but argument 2 has type \xE2\x80\x98int32\xE2\x80\x99 {aka \xE2\x80\x98long int\xE2\x80\x99} [-Wformat=]"),
             std::string("format-long"));
    CHECK_EQ(R("c1.c:9:17: warning: format '%d' expects argument of type 'int', but argument 3 has type 'uint32' {aka 'long unsigned int'} [-Wformat=]"),
             std::string("format-long"));
    CHECK_EQ(R("c1.c:10:15: warning: format '%ld' expects argument of type 'long int', but argument 2 has type 'int' [-Wformat=]"),
             std::string("format-int-as-long"));
    CHECK_EQ(R("x.c:1:1: warning: format '%s' expects argument of type 'char *', but argument 2 has type 'int' [-Wformat=]"),
             std::string("format"));
}

TEST(explain_redeclared_bases)
{
    Explanation e = explainDiagnostic("c4.c:3:18: error: conflicting types for \xE2\x80\x98SysBase\xE2\x80\x99; have \xE2\x80\x98struct ExecBase *\xE2\x80\x99");
    CHECK_EQ(e.rule, std::string("base-redeclared"));
    CHECK(e.text.find("__amigaos4__") != std::string::npos);
    CHECK_EQ(R("x.c:2:1: error: conflicting types for 'IntuitionBase'; have 'struct IntuitionBase *'"), std::string("base-redeclared"));
}

TEST(explain_linker_interfaces)
{
    Explanation e = explainDiagnostic("c5.c:(.text+0x1e): undefined reference to `IIntuition'");
    CHECK_EQ(e.rule, std::string("interface-undefined"));
    CHECK(e.text.find("-lauto") != std::string::npos);
    CHECK(e.text.find("intuition.library") != std::string::npos);
    CHECK_EQ(R("main.c:(.text+0x10): undefined reference to `GfxBase'"), std::string("interface-undefined"));
}

TEST(explain_linker_athread)
{
    CHECK_EQ(R("/opt/adtools/native-build/gcc-cross-build-11.5.0/ppc-amigaos/libstdc++-v3/include/ext/concurrence.h:139: undefined reference to `__gthread_active_p'"),
             std::string("athread"));
    CHECK_EQ(R("x.o: in function `f': undefined reference to `pthread_create'"), std::string("athread"));
}

TEST(explain_linker_other)
{
    CHECK_EQ(R("(.text+0x1aa): undefined reference to `main'"), std::string("no-main"));
    CHECK_EQ(R("errors.c:11: undefined reference to `OpenWindowTags'"), std::string("os-undefined"));
    CHECK_EQ(R("c5.c:(.text+0x40): undefined reference to `helper'"), std::string("undefined-reference"));
    CHECK_EQ(R("m.c:(.text+0x40): undefined reference to `sqrt'"), std::string("libm"));
    Explanation e = explainDiagnostic("/tmp/ccOadvHF.o:(.sdata+0x0): multiple definition of `x'; /tmp/cc0qsQPg.o:(.sdata+0x0): first defined here");
    CHECK_EQ(e.rule, std::string("multiple-definition"));
    CHECK(e.text.find("extern int x;") != std::string::npos);
    CHECK_EQ(R("a.o:(.text+0x8): relocation truncated to fit: R_PPC_REL24 against symbol `far'"), std::string("reloc-truncated"));
    CHECK(explainDiagnostic("a.o: relocation truncated to fit: R_PPC_EMB_SDA21 against `x' in .sdata").text.find("-msdata=none") != std::string::npos);
}

TEST(explain_cxx_casts_and_strings)
{
    CHECK_EQ(R("c2.cpp:7:36: error: invalid conversion from \xE2\x80\x98" "APTR\xE2\x80\x99 {aka \xE2\x80\x98void*\xE2\x80\x99} to \xE2\x80\x98" "char*\xE2\x80\x99 [-fpermissive]"),
             std::string("cxx-aptr-cast"));
    CHECK_EQ(R("c13.c:3:20: warning: ISO C++ forbids converting a string constant to 'STRPTR' {aka 'char*'} [-Wwrite-strings]"),
             std::string("strptr-const"));
    CHECK_EQ(R("c11.c:5:39: warning: passing argument 1 of 'f' discards 'const' qualifier from pointer target type [-Wdiscarded-qualifiers]", "f(\"x\");"),
             std::string("strptr-const"));
    CHECK_EQ(R("c11.c:5:39: warning: passing argument 1 of 'f' discards 'const' qualifier from pointer target type [-Wdiscarded-qualifiers]", "f(c);"),
             std::string("const"));
}

TEST(explain_headers)
{
    Explanation e = explainDiagnostic("c3.c:2:10: fatal error: proto/foo.h: No such file or directory");
    CHECK_EQ(e.rule, std::string("missing-header"));
    CHECK(e.title.find("proto/foo.h") != std::string::npos);
    Explanation o = explainDiagnostic("x.c:1:10: fatal error: clib/exec_protos.h: No such file or directory");
    CHECK(o.text.find("proto/exec.h") != std::string::npos);
    CHECK_EQ(R("x.c:1:10: fatal error: mystuff.h: No such file or directory"), std::string("missing-header"));
}

TEST(explain_structs)
{
    CHECK_EQ(R("x.c:9:8: error: 'struct TimeVal' has no member named 'tv_secs'"), std::string("old-timeval"));
    CHECK_EQ(R("x.c:9:8: error: 'struct Window' has no member named 'Foo'"), std::string("no-member"));
    CHECK_EQ(R("c1.c:13:24: error: storage size of \xE2\x80\x98tr\xE2\x80\x99 isn\xE2\x80\x99t known"), std::string("incomplete-type"));
    CHECK_EQ(R("c1.c:6:24: warning: initialization of 'struct Window *' from 'int' makes pointer from integer without a cast [-Wint-conversion]"),
             std::string("int-to-pointer"));
    CHECK_EQ(R("x.c:3:5: warning: the frame size of 200016 bytes is larger than 65536 bytes [-Wframe-larger-than=]"), std::string("big-frame"));
}

TEST(explain_nothing_for_ordinary_messages)
{
    CHECK(explainDiagnostic("c1.c:11:10: warning: unused variable 'p' [-Wunused-variable]").empty());
    CHECK(explainDiagnostic("c1.c: In function 'main':").empty());
}

TEST(explain_interfaces)
{
    CHECK_EQ(interfaceForLibrary("dos.library"), std::string("IDOS"));
    CHECK_EQ(interfaceForLibrary("gadtools.library"), std::string("IGadTools"));
    CHECK_EQ(interfaceForLibrary("foo.library"), std::string("IFoo"));
    CHECK_EQ(protoForLibrary("timer.device"), std::string("proto/timer.h"));
    CHECK_EQ(builtinLibraryOf("RectFill"), std::string("graphics.library"));
    CHECK_EQ(builtinLibraryOf("NotAFunction"), std::string(""));
}

// --- crashes ---

TEST(crash_null_pointer_field)
{
    CrashInfo c;
    c.trapType = 0x300;
    c.dar = 0x18;
    c.insn = "lwz r9,24(r3)";
    c.gpr[3] = 0;
    c.sp = 0x5000000;
    Explanation e = explainCrash(c);
    CHECK_EQ(e.rule, std::string("null-pointer"));
    CHECK(e.text.find("offset 24") != std::string::npos);
    CHECK(e.text.find("r3 = 0x00000000") != std::string::npos);
    // a NULL interface
    c.sourceLine = "    IGraphics->RectFill(rp, 0, 0, 10, 10);";
    e = explainCrash(c);
    CHECK(e.text.find("IGraphics is NULL") != std::string::npos);
    // a store
    c.insn = "stw r0,8(r9)";
    c.dar = 8;
    CHECK(explainCrash(c).title.find("writing") != std::string::npos);
}

TEST(crash_patterns)
{
    CrashInfo c;
    c.trapType = 0x300;
    c.sp = 0x5000000;
    c.dar = 0xDEADBEEF + 8;
    CHECK_EQ(explainCrash(c).rule, std::string("bad-pattern"));
    CHECK(explainCrash(c).text.find("freed") != std::string::npos);
    c.dar = 0xABADCAFE;
    CHECK(explainCrash(c).text.find("initialised") != std::string::npos);
    c.dar = 0xFFFFFFFC;
    CHECK_EQ(explainCrash(c).rule, std::string("negative-pointer"));
    c.dar = 0x12345678;
    CHECK_EQ(explainCrash(c).rule, std::string("bad-pointer"));
}

TEST(crash_in_os)
{
    CrashInfo c;
    c.trapType = 0x300;
    c.dar = 0x7FF01234;
    c.sp = 0x5000000;
    c.pcInProgram = false;
    c.function = "RectFill";
    c.module = "graphics.library";
    c.callerFunction = "draw_ball";
    c.callerLine = 42;
    Explanation e = explainCrash(c);
    CHECK_EQ(e.rule, std::string("os-bad-argument"));
    CHECK(e.text.find("draw_ball()") != std::string::npos);
    CHECK(e.text.find("TAG_END") != std::string::npos);
    // a NULL argument passed to the OS
    c.dar = 0x20;
    e = explainCrash(c);
    CHECK_EQ(e.rule, std::string("null-pointer"));
    CHECK(e.text.find("inside RectFill()") != std::string::npos);
}

TEST(crash_stack_overflow)
{
    CrashInfo c;
    c.trapType = 0x300;
    c.sp = 0x01000100;
    c.dar = 0x01000100 - 64;
    c.insn = "stwu r1,-64(r1)";
    CHECK_EQ(explainCrash(c).rule, std::string("stack-overflow"));
    CrashInfo r;
    r.trapType = 0x300;
    r.dar = 0x40000000;
    r.sp = 0x01000000;
    r.deepestRepeat = 5000;
    Explanation e = explainCrash(r);
    CHECK_EQ(e.rule, std::string("stack-overflow"));
    CHECK(e.text.find("calls itself") != std::string::npos);
    CHECK(e.text.find("$STACK") != std::string::npos);
}

TEST(crash_other_exceptions)
{
    CrashInfo c;
    c.trapType = 0x400;
    c.pc = 0;
    CHECK_EQ(explainCrash(c).rule, std::string("null-function-pointer"));
    c.pc = 0x41414140;
    CHECK_EQ(explainCrash(c).rule, std::string("jump-to-data"));
    c.trapType = 0x600;
    CHECK_EQ(explainCrash(c).rule, std::string("alignment"));
    c.trapType = 0x700;
    CHECK_EQ(explainCrash(c).rule, std::string("program-check"));
    c.trapType = 0x800;
    CHECK_EQ(explainCrash(c).rule, std::string("fpu-unavailable"));
    c.trapType = 0x900;
    CHECK(explainCrash(c).empty());
}

TEST(crash_operand_parsing)
{
    int base;
    int32_t disp;
    bool st;
    CHECK(parseMemOperand("lwz r9,24(r3)", base, disp, st) && base == 3 && disp == 24 && !st);
    CHECK(parseMemOperand("stw r0,-8(r1)", base, disp, st) && base == 1 && disp == -8 && st);
    CHECK(parseMemOperand("lbzu r0,1(r31)", base, disp, st) && base == 31 && disp == 1);
    CHECK(!parseMemOperand("addi r3,r3,1", base, disp, st));
    CHECK(!parseMemOperand("", base, disp, st));
}
