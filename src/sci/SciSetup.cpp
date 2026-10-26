// sciSetupSourceEditor: font, the current colour theme (Theme.h), margins, markers, folding and the lexer
// for a file name. Auto-indent and brace highlighting are done by SciView itself
// (AF_SCI_SETAUTOINDENT, AF_SCI_SETBRACEMATCH), both turned on here.
#include <cstdint>
#include <cstring>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "LexerModule.h"

#include "sci/SciView.h"
#include "sci/LexPPCAsm.h"
#include "sci/Theme.h"
#include "sci/Languages.h"   // --- lang ---
#include "sci/LexAmiga.h"    // --- lang ---

extern const Lexilla::LexerModule lmCPP;
// --- lang ---
extern const Lexilla::LexerModule lmPython;
extern const Lexilla::LexerModule lmLua;
extern const Lexilla::LexerModule lmPerl;
extern const Lexilla::LexerModule lmBash;
extern const Lexilla::LexerModule lmJSON;
extern const Lexilla::LexerModule lmYAML;
extern const Lexilla::LexerModule lmMarkdown;
// --- end lang ---
extern const Lexilla::LexerModule lmMake;
extern const Lexilla::LexerModule lmProps;
extern const Lexilla::LexerModule lmDiff;
extern const Lexilla::LexerModule lmNull;

namespace af {

namespace {

// --- keyword lists ---

const char cKeywords[] =
    "alignas alignof and and_eq asm auto bitand bitor break case catch class compl concept "
    "const consteval constexpr constinit const_cast continue co_await co_return co_yield "
    "decltype default delete do dynamic_cast else enum explicit export extern false final for "
    "friend goto if inline mutable namespace new noexcept not not_eq nullptr operator or or_eq "
    "override private protected public register reinterpret_cast requires restrict return "
    "sizeof static static_assert static_cast struct switch template this thread_local throw "
    "true try typedef typeid typename union using virtual volatile while xor xor_eq "
    "_Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local "
    "__asm__ __attribute__ __extension__ __inline__ __restrict__ __typeof__ __volatile__ typeof";

const char cTypes[] =
    "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t "
    "_Bool _Complex size_t ssize_t ptrdiff_t intptr_t uintptr_t off_t time_t FILE va_list "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
    "APTR BPTR BSTR BOOL BYTE UBYTE WORD UWORD LONG ULONG QUAD UQUAD FLOAT DOUBLE TEXT STRPTR "
    "CONST_STRPTR CONST_APTR VOID RPTR "
    "int8 uint8 int16 uint16 int32 uint32 int64 uint64 float32 float64 "
    "std string string_view vector map set unique_ptr shared_ptr";

const char ppcInstructions[] =
    // integer arithmetic
    "add addc adde addi addic addis addme addze subf subfc subfe subfic subfme subfze "
    "sub subc subi subis subic neg mulli mullw mulhw mulhwu divw divwu mulld mulhd mulhdu divd divdu "
    "addo addco addeo addmeo addzeo subfo subfco subfeo subfmeo subfzeo nego mullwo divwo divwuo "
    // logical, simplified moves
    "and andc andi andis or orc ori oris xor xori xoris nand nor eqv extsb extsh extsw "
    "cntlzw cntlzd not nop mr li lis la "
    // rotate and shift
    "rlwinm rlwnm rlwimi rldicl rldicr rldic rldimi rldcl rldcr slw srw sraw srawi sld srd srad sradi "
    "rotlw rotlwi rotrwi slwi srwi clrlwi clrrwi clrlslwi extlwi extrwi inslwi insrwi "
    // compare
    "cmp cmpi cmpl cmpli cmpw cmpwi cmplw cmplwi cmpd cmpdi cmpld cmpldi "
    // branch
    "b ba bl bla bc bca bcl bcla bclr bclrl bcctr bcctrl blr blrl bctr bctrl "
    "beq bne blt bgt ble bge bso bns bun bnu bng bnl bdnz bdz bdnzt bdnzf bdzt bdzf bt bf "
    "beql bnel bltl bgtl blel bgel bdnzl bdzl "
    "beqlr bnelr bltlr bgtlr blelr bgelr bsolr bnslr bdnzlr bdzlr "
    "beqctr bnectr bltctr bgtctr blectr bgectr beqlrl bnelrl beqctrl bnectrl "
    // condition register
    "crand crandc creqv crnand crnor cror crorc crxor crset crclr crmove crnot mcrf mcrxr "
    // load and store
    "lbz lbzu lbzx lbzux lhz lhzu lhzx lhzux lha lhau lhax lhaux lwz lwzu lwzx lwzux "
    "lwa lwax lwaux ld ldu ldx ldux stb stbu stbx stbux sth sthu sthx sthux stw stwu stwx stwux "
    "std stdu stdx stdux lhbrx lwbrx sthbrx stwbrx lmw stmw lswi lswx stswi stswx "
    "lwarx stwcx ldarx stdcx "
    // floating point
    "lfs lfsu lfsx lfsux lfd lfdu lfdx lfdux stfs stfsu stfsx stfsux stfd stfdu stfdx stfdux stfiwx "
    "fmr fneg fabs fnabs fadd fadds fsub fsubs fmul fmuls fdiv fdivs fmadd fmadds fmsub fmsubs "
    "fnmadd fnmadds fnmsub fnmsubs frsp fctiw fctiwz fctid fctidz fcfid fcmpu fcmpo fres frsqrte "
    "fsel fsqrt fsqrts mffs mtfsf mtfsfi mtfsb0 mtfsb1 mcrfs "
    // special registers
    "mflr mtlr mfctr mtctr mfxer mtxer mfcr mtcr mtcrf mfmsr mtmsr mfspr mtspr mftb mftbu "
    "mfsprg mtsprg mfsrr0 mtsrr0 mfsrr1 mtsrr1 mfdec mtdec mfpvr mfdcr mtdcr mfsr mtsr wrtee wrteei "
    // synchronisation and caches
    "sync isync eieio msync mbar lwsync ptesync dcbf dcbi dcbst dcbt dcbtst dcbz dcba icbi icbt "
    "dccci iccci tlbie tlbsync tlbre tlbwe tlbsx "
    // system and traps
    "sc rfi rfci rfmci tw twi trap tweq twne twlt twgt twge twle twlge twllt td tdi";

const char ppcRegisters[] =
    "r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 r13 r14 r15 r16 r17 r18 r19 r20 r21 r22 r23 "
    "r24 r25 r26 r27 r28 r29 r30 r31 "
    "f0 f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 f13 f14 f15 f16 f17 f18 f19 f20 f21 f22 f23 "
    "f24 f25 f26 f27 f28 f29 f30 f31 "
    "cr0 cr1 cr2 cr3 cr4 cr5 cr6 cr7 lr ctr xer msr sp rtoc toc fpscr lt gt eq so un";

const char gasDirectives[] =
    ".text .data .bss .rodata .sdata .sdata2 .sbss .section .previous .pushsection .popsection "
    ".globl .global .local .weak .hidden .protected .extern .type .size .align .balign .p2align "
    ".byte .short .hword .half .word .long .int .quad .octa .float .single .double "
    ".ascii .asciz .string .space .skip .fill .zero .org .equ .set .equiv .eqv .comm .lcomm "
    ".file .loc .ident .macro .endm .exitm .purgem .rept .irp .irpc .endr "
    ".if .ifdef .ifndef .ifeq .ifne .else .elseif .endif .include .incbin .err .error .warning "
    ".end .abort .gnu_attribute .machine .nops .print .reloc .symver .uleb128 .sleb128 "
    ".cfi_startproc .cfi_endproc .cfi_def_cfa .cfi_def_cfa_offset .cfi_def_cfa_register "
    ".cfi_offset .cfi_restore .cfi_register .cfi_adjust_cfa_offset .cfi_sections "
    ".stabs .stabn .stabd .tc .toc .list .nolist .title .struct";

// --- colours (0xRRGGBB; Scintilla wants 0xBBGGRR), from the current theme ---

constexpr intptr_t RGB(uint32_t rgb) { return sciColour(rgb); }

class Setup {
public:
    explicit Setup(SciView *v_) : v(v_) {}
    intptr_t operator()(unsigned int msg, uintptr_t w = 0, intptr_t l = 0) { return v->send(msg, w, l); }
    void Style(int style, uint32_t fore, std::optional<uint32_t> back = std::nullopt) {
        v->send(SCI_STYLESETFORE, style, RGB(fore));
        if (back)
            v->send(SCI_STYLESETBACK, style, RGB(*back));
    }
    void Property(const char *key, const char *value) {
        v->send(SCI_SETPROPERTY, reinterpret_cast<uintptr_t>(key), reinterpret_cast<intptr_t>(value));
    }
    void Keywords(int set, const char *words) {
        v->send(SCI_SETKEYWORDS, set, reinterpret_cast<intptr_t>(words));
    }
    void Marker(int marker, int symbol, uint32_t fore, uint32_t back) {
        v->send(SCI_MARKERDEFINE, marker, symbol);
        v->send(SCI_MARKERSETFORE, marker, RGB(fore));
        v->send(SCI_MARKERSETBACK, marker, RGB(back));
    }
private:
    SciView *v;
};

void SetupCpp(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmCPP.Create()));
    s.Keywords(0, cKeywords);
    s.Keywords(1, cTypes);
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Property("fold.comment", "1");
    s.Property("fold.preprocessor", "1");
    s.Property("lexer.cpp.track.preprocessor", "0");
    s.Style(SCE_C_COMMENT, t.comment);
    s.Style(SCE_C_COMMENTLINE, t.comment);
    s.Style(SCE_C_COMMENTDOC, t.docComment);
    s.Style(SCE_C_COMMENTLINEDOC, t.docComment);
    s.Style(SCE_C_COMMENTDOCKEYWORD, t.docComment);
    s.Style(SCE_C_COMMENTDOCKEYWORDERROR, t.docComment);
    s.Style(SCE_C_PREPROCESSORCOMMENT, t.comment);
    s.Style(SCE_C_PREPROCESSORCOMMENTDOC, t.docComment);
    s.Style(SCE_C_NUMBER, t.number);
    s.Style(SCE_C_WORD, t.keyword);
    s.Style(SCE_C_WORD2, t.type);
    s.Style(SCE_C_GLOBALCLASS, t.type);
    s.Style(SCE_C_STRING, t.string);
    s.Style(SCE_C_CHARACTER, t.string);
    s.Style(SCE_C_STRINGRAW, t.string);
    s.Style(SCE_C_VERBATIM, t.string);
    s.Style(SCE_C_HASHQUOTEDSTRING, t.string);
    s.Style(SCE_C_ESCAPESEQUENCE, t.number);
    s.Style(SCE_C_STRINGEOL, t.string, t.stringEolBack);
    s.Style(SCE_C_USERLITERAL, t.number);
    s.Style(SCE_C_PREPROCESSOR, t.preprocessor);
    s.Style(SCE_C_OPERATOR, t.oper);
    s.Style(SCE_C_TASKMARKER, t.taskMarker);
}

void SetupAsm(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmPPCAsm.Create()));
    s.Keywords(0, ppcInstructions);
    s.Keywords(1, ppcRegisters);
    s.Keywords(2, gasDirectives);
    s.Style(AF_ASM_COMMENT, t.comment);
    s.Style(AF_ASM_COMMENTBLOCK, t.comment);
    s.Style(AF_ASM_NUMBER, t.number);
    s.Style(AF_ASM_STRING, t.string);
    s.Style(AF_ASM_CHARACTER, t.string);
    s.Style(AF_ASM_STRINGEOL, t.string, t.stringEolBack);
    s.Style(AF_ASM_OPERATOR, t.asmOper);
    s.Style(AF_ASM_INSTRUCTION, t.keyword);
    s.Style(AF_ASM_REGISTER, t.type);
    s.Style(AF_ASM_DIRECTIVE, t.preprocessor);
    s.Style(AF_ASM_LABEL, t.label);
    s.Style(AF_ASM_PREPROCESSOR, t.preprocessor);
}

void SetupMake(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmMake.Create()));
    s(SCI_SETUSETABS, 1);
    s.Style(SCE_MAKE_COMMENT, t.comment);
    s.Style(SCE_MAKE_PREPROCESSOR, t.preprocessor);
    s.Style(SCE_MAKE_IDENTIFIER, t.type);
    s.Style(SCE_MAKE_OPERATOR, t.oper);
    s.Style(SCE_MAKE_TARGET, t.keyword);
    s.Style(SCE_MAKE_IDEOL, t.string, t.stringEolBack);
}

void SetupProps(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmProps.Create()));
    s.Style(SCE_PROPS_COMMENT, t.comment);
    s.Style(SCE_PROPS_SECTION, t.keyword, t.propsSectionBack);
    s.Style(SCE_PROPS_ASSIGNMENT, t.oper);
    s.Style(SCE_PROPS_DEFVAL, t.number);
    s.Style(SCE_PROPS_KEY, t.type);
}

void SetupDiff(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmDiff.Create()));
    s.Style(SCE_DIFF_COMMENT, t.comment);
    s.Style(SCE_DIFF_COMMAND, t.keyword);
    s.Style(SCE_DIFF_HEADER, t.preprocessor, t.diffHeaderBack);
    s.Style(SCE_DIFF_POSITION, t.number);
    s.Style(SCE_DIFF_DELETED, t.diffDeletedFore, t.diffDeletedBack);
    s.Style(SCE_DIFF_ADDED, t.diffAddedFore, t.diffAddedBack);
    s.Style(SCE_DIFF_CHANGED, t.diffChangedFore, t.diffChangedBack);
}

// --- lang --- the other languages' lexers and styles
void SetupPython(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmPython.Create()));
    s.Keywords(0, languageKeywords(Language::Python, 0));
    s.Keywords(1, languageKeywords(Language::Python, 1));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Property("fold.quotes.python", "1");
    s.Property("lexer.python.strings.f.pep.701", "1");
    s.Style(SCE_P_COMMENTLINE, t.comment);
    s.Style(SCE_P_COMMENTBLOCK, t.comment);
    s.Style(SCE_P_NUMBER, t.number);
    s.Style(SCE_P_STRING, t.string);
    s.Style(SCE_P_CHARACTER, t.string);
    s.Style(SCE_P_TRIPLE, t.docComment);
    s.Style(SCE_P_TRIPLEDOUBLE, t.docComment);
    s.Style(SCE_P_FSTRING, t.string);
    s.Style(SCE_P_FCHARACTER, t.string);
    s.Style(SCE_P_FTRIPLE, t.string);
    s.Style(SCE_P_FTRIPLEDOUBLE, t.string);
    s.Style(SCE_P_STRINGEOL, t.string, t.stringEolBack);
    s.Style(SCE_P_WORD, t.keyword);
    s.Style(SCE_P_WORD2, t.builtin);
    s.Style(SCE_P_CLASSNAME, t.type);
    s.Style(SCE_P_DEFNAME, t.funcName);
    s.Style(SCE_P_OPERATOR, t.oper);
    s.Style(SCE_P_DECORATOR, t.decorator);
    s.Style(SCE_P_ATTRIBUTE, t.textFore);
}

void SetupLua(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmLua.Create()));
    for (int i = 0; i < 3; i++) s.Keywords(i, languageKeywords(Language::Lua, i));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Style(SCE_LUA_COMMENT, t.comment);
    s.Style(SCE_LUA_COMMENTLINE, t.comment);
    s.Style(SCE_LUA_COMMENTDOC, t.docComment);
    s.Style(SCE_LUA_NUMBER, t.number);
    s.Style(SCE_LUA_WORD, t.keyword);
    s.Style(SCE_LUA_WORD2, t.builtin);
    s.Style(SCE_LUA_WORD3, t.builtin);
    s.Style(SCE_LUA_STRING, t.string);
    s.Style(SCE_LUA_CHARACTER, t.string);
    s.Style(SCE_LUA_LITERALSTRING, t.string);
    s.Style(SCE_LUA_STRINGEOL, t.string, t.stringEolBack);
    s.Style(SCE_LUA_PREPROCESSOR, t.preprocessor);
    s.Style(SCE_LUA_OPERATOR, t.oper);
    s.Style(SCE_LUA_LABEL, t.decorator);
}

void SetupPerl(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmPerl.Create()));
    s.Keywords(0, languageKeywords(Language::Perl, 0));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Style(SCE_PL_ERROR, t.lexError);
    s.Style(SCE_PL_COMMENTLINE, t.comment);
    s.Style(SCE_PL_POD, t.docComment, t.codeBack);
    s.Style(SCE_PL_POD_VERB, t.docComment, t.codeBack);
    s.Style(SCE_PL_NUMBER, t.number);
    s.Style(SCE_PL_WORD, t.keyword);
    for (int st : { SCE_PL_STRING, SCE_PL_CHARACTER, SCE_PL_STRING_Q, SCE_PL_STRING_QQ, SCE_PL_STRING_QX,
                    SCE_PL_STRING_QR, SCE_PL_STRING_QW, SCE_PL_LONGQUOTE, SCE_PL_BACKTICKS, SCE_PL_STRING_VAR })
        s.Style(st, t.string);
    s.Style(SCE_PL_PREPROCESSOR, t.preprocessor);
    s.Style(SCE_PL_OPERATOR, t.oper);
    s.Style(SCE_PL_PUNCTUATION, t.oper);
    for (int st : { SCE_PL_SCALAR, SCE_PL_ARRAY, SCE_PL_HASH, SCE_PL_SYMBOLTABLE, SCE_PL_VARIABLE_INDEXER })
        s.Style(st, t.variable);
    for (int st : { SCE_PL_REGEX, SCE_PL_REGSUBST, SCE_PL_XLAT, SCE_PL_REGEX_VAR, SCE_PL_REGSUBST_VAR })
        s.Style(st, t.preprocessor);
    for (int st : { SCE_PL_HERE_DELIM, SCE_PL_HERE_Q, SCE_PL_HERE_QQ, SCE_PL_HERE_QX, SCE_PL_DATASECTION })
        s.Style(st, t.emphasis);
    s.Style(SCE_PL_SUB_PROTOTYPE, t.type);
}

void SetupShell(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmBash.Create()));
    s.Keywords(0, languageKeywords(Language::Shell, 0));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Style(SCE_SH_ERROR, t.lexError);
    s.Style(SCE_SH_COMMENTLINE, t.comment);
    s.Style(SCE_SH_NUMBER, t.number);
    s.Style(SCE_SH_WORD, t.keyword);
    s.Style(SCE_SH_STRING, t.string);
    s.Style(SCE_SH_CHARACTER, t.string);
    s.Style(SCE_SH_BACKTICKS, t.preprocessor);
    s.Style(SCE_SH_OPERATOR, t.oper);
    s.Style(SCE_SH_SCALAR, t.variable);
    s.Style(SCE_SH_PARAM, t.variable);
    s.Style(SCE_SH_HERE_DELIM, t.emphasis);
    s.Style(SCE_SH_HERE_Q, t.emphasis);
}

void SetupARexx(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmARexx.Create()));
    for (int i = 0; i < 3; i++) s.Keywords(i, languageKeywords(Language::ARexx, i));
    s.Property("fold", "1");
    s.Style(AF_REXX_COMMENT, t.comment);
    s.Style(AF_REXX_STRING, t.string);
    s.Style(AF_REXX_STRINGEOL, t.string, t.stringEolBack);
    s.Style(AF_REXX_NUMBER, t.number);
    s.Style(AF_REXX_KEYWORD, t.keyword);
    s.Style(AF_REXX_BUILTIN, t.builtin);
    s.Style(AF_REXX_OPERATOR, t.oper);
    s.Style(AF_REXX_LABEL, t.funcName);
    s.Style(AF_REXX_SPECIAL, t.type);
}

void SetupAmigaDOS(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmAmigaDOS.Create()));
    s.Keywords(0, languageKeywords(Language::AmigaDOS, 0));
    s.Property("fold", "1");
    s.Style(AF_DOS_COMMENT, t.comment);
    s.Style(AF_DOS_STRING, t.string);
    s.Style(AF_DOS_NUMBER, t.number);
    s.Style(AF_DOS_COMMAND, t.keyword);
    s.Style(AF_DOS_DIRECTIVE, t.preprocessor);
    s.Style(AF_DOS_PARAM, t.type);
    s.Style(AF_DOS_VARIABLE, t.variable);
    s.Style(AF_DOS_OPERATOR, t.oper);
    s.Style(AF_DOS_LABEL, t.funcName);
}

void SetupJSON(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmJSON.Create()));
    s.Keywords(0, languageKeywords(Language::JSON, 0));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Property("lexer.json.allow.comments", "1");
    s.Property("lexer.json.escape.sequence", "1");
    s.Style(SCE_JSON_NUMBER, t.number);
    s.Style(SCE_JSON_STRING, t.string);
    s.Style(SCE_JSON_STRINGEOL, t.string, t.stringEolBack);
    s.Style(SCE_JSON_PROPERTYNAME, t.heading);
    s.Style(SCE_JSON_ESCAPESEQUENCE, t.number);
    s.Style(SCE_JSON_LINECOMMENT, t.comment);
    s.Style(SCE_JSON_BLOCKCOMMENT, t.comment);
    s.Style(SCE_JSON_OPERATOR, t.oper);
    s.Style(SCE_JSON_URI, t.variable);
    s.Style(SCE_JSON_COMPACTIRI, t.variable);
    s.Style(SCE_JSON_KEYWORD, t.keyword);
    s.Style(SCE_JSON_LDKEYWORD, t.keyword);
    s.Style(SCE_JSON_ERROR, t.lexError, t.stringEolBack);
}

void SetupYAML(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmYAML.Create()));
    s.Keywords(0, languageKeywords(Language::YAML, 0));
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Style(SCE_YAML_COMMENT, t.comment);
    s.Style(SCE_YAML_IDENTIFIER, t.heading);
    s.Style(SCE_YAML_KEYWORD, t.keyword);
    s.Style(SCE_YAML_NUMBER, t.number);
    s.Style(SCE_YAML_REFERENCE, t.variable);
    s.Style(SCE_YAML_DOCUMENT, t.preprocessor);
    s.Style(SCE_YAML_TEXT, t.string);
    s.Style(SCE_YAML_ERROR, t.lexError, t.stringEolBack);
    s.Style(SCE_YAML_OPERATOR, t.oper);
}

void SetupMarkdown(Setup &s) {
    const Theme &t = currentTheme();
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmMarkdown.Create()));
    for (int st = SCE_MARKDOWN_HEADER1; st <= SCE_MARKDOWN_HEADER6; st++) {
        s.Style(st, t.heading);
        s(SCI_STYLESETBOLD, st, 1);
    }
    s.Style(SCE_MARKDOWN_STRONG1, t.emphasis);
    s.Style(SCE_MARKDOWN_STRONG2, t.emphasis);
    s(SCI_STYLESETBOLD, SCE_MARKDOWN_STRONG1, 1);
    s(SCI_STYLESETBOLD, SCE_MARKDOWN_STRONG2, 1);
    s.Style(SCE_MARKDOWN_EM1, t.emphasis);
    s.Style(SCE_MARKDOWN_EM2, t.emphasis);
    s(SCI_STYLESETITALIC, SCE_MARKDOWN_EM1, 1);
    s(SCI_STYLESETITALIC, SCE_MARKDOWN_EM2, 1);
    s.Style(SCE_MARKDOWN_PRECHAR, t.comment);
    s.Style(SCE_MARKDOWN_ULIST_ITEM, t.keyword);
    s.Style(SCE_MARKDOWN_OLIST_ITEM, t.keyword);
    s.Style(SCE_MARKDOWN_BLOCKQUOTE, t.comment);
    s.Style(SCE_MARKDOWN_STRIKEOUT, t.comment);
    s.Style(SCE_MARKDOWN_HRULE, t.oper);
    s.Style(SCE_MARKDOWN_LINK, t.variable);
    s.Style(SCE_MARKDOWN_CODE, t.string, t.codeBack);
    s.Style(SCE_MARKDOWN_CODE2, t.string, t.codeBack);
    s.Style(SCE_MARKDOWN_CODEBK, t.string, t.codeBack);
}

// The first line of the document the view shows (for files without an extension).
std::string FirstLine(Setup &s) {
    const intptr_t len = s(SCI_LINELENGTH, 0);
    if (len <= 0 || len > 400) return std::string();
    std::string buf(static_cast<size_t>(len) + 1, '\0');
    s(SCI_GETLINE, 0, reinterpret_cast<intptr_t>(&buf[0]));
    buf.resize(static_cast<size_t>(len));
    return buf;
}
// --- end lang ---

} // namespace

void sciSetupSourceEditor(SciView *v, const char *fileName, int fontSize) {
    Setup s(v);
    // --- lang ---
    const Language lang = languageFor(fileName ? fileName : "", FirstLine(s));

    s(SCI_SETCODEPAGE, 0);
    s(SCI_SETEOLMODE, SC_EOL_LF);
    s(SCI_SETTABWIDTH, 4);
    s(SCI_SETINDENT, defaultIndentWidth(lang));   // --- lang ---
    s(SCI_SETUSETABS, 0);
    s(SCI_SETTABINDENTS, 1);
    s(SCI_SETBACKSPACEUNINDENTS, 1);
    s(SCI_SETSCROLLWIDTH, 1);
    s(SCI_SETSCROLLWIDTHTRACKING, 1);
    s(SCI_SETXCARETPOLICY, CARET_SLOP | CARET_EVEN, 24);
    s(SCI_SETYCARETPOLICY, CARET_SLOP | CARET_EVEN, 2);
    s(SCI_SETCARETPERIOD, 530);
    s(SCI_SETCARETWIDTH, 2);

    // Base style, copied to every style by STYLECLEARALL.
    const Theme &t = currentTheme();
    s(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<intptr_t>("DejaVu Sans Mono"));
    s(SCI_STYLESETSIZE, STYLE_DEFAULT, fontSize);
    s.Style(STYLE_DEFAULT, t.textFore, t.textBack);
    s(SCI_STYLECLEARALL);
    s.Style(STYLE_LINENUMBER, t.marginFore, t.marginBack);
    s.Style(STYLE_BRACELIGHT, t.braceFore, t.braceBack);
    s.Style(STYLE_BRACEBAD, t.braceBadFore, t.braceBadBack);
    s.Style(STYLE_CONTROLCHAR, t.controlChar);
    s.Style(STYLE_INDENTGUIDE, t.indentGuide);
    s.Style(STYLE_CALLTIP, t.calltipFore, t.calltipBack);
    s(SCI_CALLTIPSETBACK, RGB(t.calltipBack));
    s(SCI_CALLTIPSETFORE, RGB(t.calltipFore));
    s(SCI_CALLTIPSETFOREHLT, RGB(t.calltipHighlight));
    s(SCI_SETWHITESPACEFORE, 1, RGB(t.indentGuide));

    // Selection, caret and the caret line (translucent, so marker backgrounds show through).
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, RGB(t.selection) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, RGB(t.selectionInactive) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, RGB(t.additionalSelection) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, RGB(t.caret) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_ADDITIONAL, RGB(t.additionalCaret) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, RGB(t.caretLine & 0xFFFFFF) | (intptr_t)(t.caretLine & 0xFF000000));
    s(SCI_SETCARETLINELAYER, SC_LAYER_UNDER_TEXT);
    s(SCI_SETCARETLINEVISIBLE, 1);
    // the autocompletion list
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST, RGB(t.listFore) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, RGB(t.listBack) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, RGB(t.listSelFore) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, RGB(t.listSelBack) | 0xFF000000);

    // Multiple cursors: Ctrl-click (the IDE turns Alt-click into it) adds a caret, typing and
    // pasting go to every caret.
    s(SCI_SETMULTIPLESELECTION, 1);
    s(SCI_SETADDITIONALSELECTIONTYPING, 1);
    s(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    s(SCI_SETADDITIONALCARETSBLINK, 1);

    // Margins: line numbers, symbols (breakpoints, execution arrow), folding.
    s(SCI_SETMARGINS, 3);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_NUMBERS, SC_MARGIN_NUMBER);
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_NUMBERS,
      s(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<intptr_t>("_99999")));
    s(SCI_SETMARGINMASKN, AF_MARGIN_NUMBERS, 0);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_SYMBOLS, SC_MARGIN_COLOUR);
    s(SCI_SETMARGINBACKN, AF_MARGIN_SYMBOLS, RGB(t.marginBack));
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_SYMBOLS, fontSize + 4);
    s(SCI_SETMARGINMASKN, AF_MARGIN_SYMBOLS,
      (1 << AF_MARKER_BREAKPOINT) | (1 << AF_MARKER_BREAKPOINT_OFF) |
      (1 << AF_MARKER_CURRENT_LINE) | (1 << AF_MARKER_FRAME));
    s(SCI_SETMARGINSENSITIVEN, AF_MARGIN_SYMBOLS, 1);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_FOLD, SC_MARGIN_SYMBOL);
    s(SCI_SETMARGINMASKN, AF_MARGIN_FOLD, SC_MASK_FOLDERS);
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_FOLD, foldable(lang) ? fontSize : 0);   // --- lang ---
    s(SCI_SETMARGINSENSITIVEN, AF_MARGIN_FOLD, 1);
    s(SCI_SETMARGINLEFT, 0, 4);
    s(SCI_SETFOLDMARGINCOLOUR, 1, RGB(t.foldBack));
    s(SCI_SETFOLDMARGINHICOLOUR, 1, RGB(t.foldBack));
    s(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    s(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);

    s.Marker(AF_MARKER_BREAKPOINT, SC_MARK_CIRCLE, t.bpFore, t.bpBack);
    s.Marker(AF_MARKER_BREAKPOINT_OFF, SC_MARK_CIRCLE, t.bpOffFore, t.marginBack);
    s.Marker(AF_MARKER_CURRENT_LINE, SC_MARK_SHORTARROW, t.execFore, t.execBack);
    s.Marker(AF_MARKER_CURRENT_BG, SC_MARK_BACKGROUND, t.textFore, t.execLineBack);
    s.Marker(AF_MARKER_ERROR, SC_MARK_BACKGROUND, t.textFore, t.errorBack);
    s.Marker(AF_MARKER_WARNING, SC_MARK_BACKGROUND, t.textFore, t.warningBack);
    s.Marker(AF_MARKER_FRAME, SC_MARK_SHORTARROW, t.frameFore, t.frameBack);

    // indicators the IDE uses
    s(SCI_INDICSETSTYLE, AF_INDIC_SNIPPET, INDIC_FULLBOX);
    s(SCI_INDICSETFORE, AF_INDIC_SNIPPET, RGB(t.snippetField));
    s(SCI_INDICSETALPHA, AF_INDIC_SNIPPET, 50);
    s(SCI_INDICSETOUTLINEALPHA, AF_INDIC_SNIPPET, 160);
    s(SCI_INDICSETSTYLE, AF_INDIC_OCCURRENCE, INDIC_ROUNDBOX);
    s(SCI_INDICSETFORE, AF_INDIC_OCCURRENCE, RGB(t.occurrence));
    s(SCI_INDICSETALPHA, AF_INDIC_OCCURRENCE, 70);
    s(SCI_INDICSETOUTLINEALPHA, AF_INDIC_OCCURRENCE, 200);
    s(SCI_INDICSETUNDER, AF_INDIC_OCCURRENCE, 1);

    const struct { int marker, symbol; } folders[] = {
        { SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS }, { SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS },
        { SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE }, { SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER },
        { SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED },
        { SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED },
        { SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER },
    };
    for (const auto &f : folders)
        s.Marker(f.marker, f.symbol, t.foldMarkFore, t.foldMarkBack);

    switch (lang) {
    case Language::C: SetupCpp(s); break;
    case Language::Asm: SetupAsm(s); break;
    case Language::Make: SetupMake(s); break;
    case Language::Props: SetupProps(s); break;
    case Language::Diff: SetupDiff(s); break;
    // --- lang ---
    case Language::Python: SetupPython(s); break;
    case Language::Lua: SetupLua(s); break;
    case Language::Perl: SetupPerl(s); break;
    case Language::Shell: SetupShell(s); break;
    case Language::ARexx: SetupARexx(s); break;
    case Language::AmigaDOS: SetupAmigaDOS(s); break;
    case Language::JSON: SetupJSON(s); break;
    case Language::YAML: SetupYAML(s); break;
    case Language::Markdown: SetupMarkdown(s); break;
    // --- end lang ---
    case Language::Text: s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmNull.Create())); break;
    }
    s(SCI_COLOURISE, 0, -1);

    s(AF_SCI_SETLANGUAGE, static_cast<uintptr_t>(lang));   // --- lang --- auto-indent rules
    s(AF_SCI_SETAUTOINDENT, 1);
    s(AF_SCI_SETBRACEMATCH, 1);
}

} // namespace af
