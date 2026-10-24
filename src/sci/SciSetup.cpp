// sciSetupSourceEditor: font, light colour theme, margins, markers, folding and the lexer
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

extern const Lexilla::LexerModule lmCPP;
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

// --- colours (0xRRGGBB; Scintilla wants 0xBBGGRR) ---

constexpr intptr_t RGB(uint32_t rgb) {
    return static_cast<intptr_t>(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}

constexpr uint32_t textFore = 0x1A1A1A, textBack = 0xFFFFFF;
constexpr uint32_t marginBack = 0xF0F0F0, marginFore = 0x909090;
constexpr uint32_t comment = 0x3F8F3F, docComment = 0x4F7F9F, keyword = 0x0033B3, type = 0x007A7A;
constexpr uint32_t string = 0xB8261A, number = 0xA85E00, preprocessor = 0x8A2E9E, oper = 0x806000;
constexpr uint32_t label = 0xB00060, asmOper = 0x505050;

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

enum class Lang { text, cpp, asm_, make, props, diff };

Lang LangForFile(const char *fileName) {
    std::string name = fileName ? fileName : "";
    const size_t slash = name.find_last_of("/:");
    if (slash != std::string::npos)
        name.erase(0, slash + 1);
    std::string lower;
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // A bare language name.
    if (lower == "c" || lower == "cpp")
        return Lang::cpp;
    if (lower == "asm")
        return Lang::asm_;
    if (lower == "makefile" || lower == "gnumakefile" || lower == "smakefile")
        return Lang::make;
    if (lower == "props")
        return Lang::props;
    if (lower == "diff")
        return Lang::diff;
    const size_t dot = lower.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : lower.substr(dot + 1);
    if (ext == "c" || ext == "h" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++" ||
        ext == "hpp" || ext == "hh" || ext == "hxx")
        return Lang::cpp;
    if (ext == "s" || ext == "asm" || ext == "i")
        return Lang::asm_;
    if (ext == "mk" || ext == "mak" || lower.compare(0, 8, "makefile") == 0)
        return Lang::make;
    if (ext == "props" || ext == "properties" || ext == "ini" || ext == "cfg" || ext == "afp")
        return Lang::props;
    if (ext == "diff" || ext == "patch")
        return Lang::diff;
    return Lang::text;
}

void SetupCpp(Setup &s) {
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmCPP.Create()));
    s.Keywords(0, cKeywords);
    s.Keywords(1, cTypes);
    s.Property("fold", "1");
    s.Property("fold.compact", "0");
    s.Property("fold.comment", "1");
    s.Property("fold.preprocessor", "1");
    s.Property("lexer.cpp.track.preprocessor", "0");
    s.Style(SCE_C_COMMENT, comment);
    s.Style(SCE_C_COMMENTLINE, comment);
    s.Style(SCE_C_COMMENTDOC, docComment);
    s.Style(SCE_C_COMMENTLINEDOC, docComment);
    s.Style(SCE_C_COMMENTDOCKEYWORD, docComment);
    s.Style(SCE_C_COMMENTDOCKEYWORDERROR, docComment);
    s.Style(SCE_C_PREPROCESSORCOMMENT, comment);
    s.Style(SCE_C_PREPROCESSORCOMMENTDOC, docComment);
    s.Style(SCE_C_NUMBER, number);
    s.Style(SCE_C_WORD, keyword);
    s.Style(SCE_C_WORD2, type);
    s.Style(SCE_C_GLOBALCLASS, type);
    s.Style(SCE_C_STRING, string);
    s.Style(SCE_C_CHARACTER, string);
    s.Style(SCE_C_STRINGRAW, string);
    s.Style(SCE_C_VERBATIM, string);
    s.Style(SCE_C_HASHQUOTEDSTRING, string);
    s.Style(SCE_C_ESCAPESEQUENCE, number);
    s.Style(SCE_C_STRINGEOL, string, 0xFFE0E0);
    s.Style(SCE_C_USERLITERAL, number);
    s.Style(SCE_C_PREPROCESSOR, preprocessor);
    s.Style(SCE_C_OPERATOR, oper);
    s.Style(SCE_C_TASKMARKER, 0xC00000);
}

void SetupAsm(Setup &s) {
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmPPCAsm.Create()));
    s.Keywords(0, ppcInstructions);
    s.Keywords(1, ppcRegisters);
    s.Keywords(2, gasDirectives);
    s.Style(AF_ASM_COMMENT, comment);
    s.Style(AF_ASM_COMMENTBLOCK, comment);
    s.Style(AF_ASM_NUMBER, number);
    s.Style(AF_ASM_STRING, string);
    s.Style(AF_ASM_CHARACTER, string);
    s.Style(AF_ASM_STRINGEOL, string, 0xFFE0E0);
    s.Style(AF_ASM_OPERATOR, asmOper);
    s.Style(AF_ASM_INSTRUCTION, keyword);
    s.Style(AF_ASM_REGISTER, type);
    s.Style(AF_ASM_DIRECTIVE, preprocessor);
    s.Style(AF_ASM_LABEL, label);
    s.Style(AF_ASM_PREPROCESSOR, preprocessor);
}

void SetupMake(Setup &s) {
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmMake.Create()));
    s(SCI_SETUSETABS, 1);
    s.Style(SCE_MAKE_COMMENT, comment);
    s.Style(SCE_MAKE_PREPROCESSOR, preprocessor);
    s.Style(SCE_MAKE_IDENTIFIER, type);
    s.Style(SCE_MAKE_OPERATOR, oper);
    s.Style(SCE_MAKE_TARGET, keyword);
    s.Style(SCE_MAKE_IDEOL, string, 0xFFE0E0);
}

void SetupProps(Setup &s) {
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmProps.Create()));
    s.Style(SCE_PROPS_COMMENT, comment);
    s.Style(SCE_PROPS_SECTION, keyword, 0xEEF2FF);
    s.Style(SCE_PROPS_ASSIGNMENT, oper);
    s.Style(SCE_PROPS_DEFVAL, number);
    s.Style(SCE_PROPS_KEY, type);
}

void SetupDiff(Setup &s) {
    s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmDiff.Create()));
    s.Style(SCE_DIFF_COMMENT, comment);
    s.Style(SCE_DIFF_COMMAND, keyword);
    s.Style(SCE_DIFF_HEADER, preprocessor, 0xF4ECF8);
    s.Style(SCE_DIFF_POSITION, number);
    s.Style(SCE_DIFF_DELETED, 0xA00000, 0xFFECEC);
    s.Style(SCE_DIFF_ADDED, 0x006000, 0xEAFBEA);
    s.Style(SCE_DIFF_CHANGED, 0x805000, 0xFFF6DC);
}

} // namespace

void sciSetupSourceEditor(SciView *v, const char *fileName, int fontSize) {
    Setup s(v);
    const Lang lang = LangForFile(fileName);

    s(SCI_SETCODEPAGE, 0);
    s(SCI_SETEOLMODE, SC_EOL_LF);
    s(SCI_SETTABWIDTH, 4);
    s(SCI_SETINDENT, 4);
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
    s(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<intptr_t>("DejaVu Sans Mono"));
    s(SCI_STYLESETSIZE, STYLE_DEFAULT, fontSize);
    s.Style(STYLE_DEFAULT, textFore, textBack);
    s(SCI_STYLECLEARALL);
    s.Style(STYLE_LINENUMBER, marginFore, marginBack);
    s.Style(STYLE_BRACELIGHT, keyword, 0xC8E0FF);
    s.Style(STYLE_BRACEBAD, 0xC00000, 0xFFD0D0);
    s.Style(STYLE_CONTROLCHAR, textFore);
    s.Style(STYLE_INDENTGUIDE, 0xD0D0D0);
    s(SCI_CALLTIPSETBACK, RGB(0xFFFFE8));
    s(SCI_CALLTIPSETFORE, RGB(0x303030));
    s(SCI_CALLTIPSETFOREHLT, RGB(keyword));

    // Selection, caret and the caret line (translucent, so marker backgrounds show through).
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, RGB(0xB4D5FE) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, RGB(0xDCDCDC) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, RGB(0x000000) | 0xFF000000);
    s(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, RGB(0x2060FF) | 0x14000000);
    s(SCI_SETCARETLINELAYER, SC_LAYER_UNDER_TEXT);
    s(SCI_SETCARETLINEVISIBLE, 1);

    // Margins: line numbers, symbols (breakpoints, execution arrow), folding.
    s(SCI_SETMARGINS, 3);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_NUMBERS, SC_MARGIN_NUMBER);
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_NUMBERS,
      s(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<intptr_t>("_99999")));
    s(SCI_SETMARGINMASKN, AF_MARGIN_NUMBERS, 0);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_SYMBOLS, SC_MARGIN_COLOUR);
    s(SCI_SETMARGINBACKN, AF_MARGIN_SYMBOLS, RGB(marginBack));
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_SYMBOLS, fontSize + 4);
    s(SCI_SETMARGINMASKN, AF_MARGIN_SYMBOLS,
      (1 << AF_MARKER_BREAKPOINT) | (1 << AF_MARKER_BREAKPOINT_OFF) |
      (1 << AF_MARKER_CURRENT_LINE) | (1 << AF_MARKER_FRAME));
    s(SCI_SETMARGINSENSITIVEN, AF_MARGIN_SYMBOLS, 1);
    s(SCI_SETMARGINTYPEN, AF_MARGIN_FOLD, SC_MARGIN_SYMBOL);
    s(SCI_SETMARGINMASKN, AF_MARGIN_FOLD, SC_MASK_FOLDERS);
    s(SCI_SETMARGINWIDTHN, AF_MARGIN_FOLD, lang == Lang::cpp ? fontSize : 0);
    s(SCI_SETMARGINSENSITIVEN, AF_MARGIN_FOLD, 1);
    s(SCI_SETMARGINLEFT, 0, 4);
    s(SCI_SETFOLDMARGINCOLOUR, 1, RGB(0xF7F7F7));
    s(SCI_SETFOLDMARGINHICOLOUR, 1, RGB(0xF7F7F7));
    s(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    s(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);

    s.Marker(AF_MARKER_BREAKPOINT, SC_MARK_CIRCLE, 0x9A1010, 0xE02828);
    s.Marker(AF_MARKER_BREAKPOINT_OFF, SC_MARK_CIRCLE, 0xC03030, marginBack);
    s.Marker(AF_MARKER_CURRENT_LINE, SC_MARK_SHORTARROW, 0x8A6A00, 0xFFD400);
    s.Marker(AF_MARKER_CURRENT_BG, SC_MARK_BACKGROUND, textFore, 0xFFF3A8);
    s.Marker(AF_MARKER_ERROR, SC_MARK_BACKGROUND, textFore, 0xFFD8D8);
    s.Marker(AF_MARKER_WARNING, SC_MARK_BACKGROUND, textFore, 0xFFEACC);
    s.Marker(AF_MARKER_FRAME, SC_MARK_SHORTARROW, 0x1E6E1E, 0x48C048);

    const struct { int marker, symbol; } folders[] = {
        { SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS }, { SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS },
        { SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE }, { SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER },
        { SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED },
        { SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED },
        { SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER },
    };
    for (const auto &f : folders)
        s.Marker(f.marker, f.symbol, 0xFFFFFF, 0x909090);

    switch (lang) {
    case Lang::cpp: SetupCpp(s); break;
    case Lang::asm_: SetupAsm(s); break;
    case Lang::make: SetupMake(s); break;
    case Lang::props: SetupProps(s); break;
    case Lang::diff: SetupDiff(s); break;
    case Lang::text: s(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(lmNull.Create())); break;
    }
    s(SCI_COLOURISE, 0, -1);

    s(AF_SCI_SETAUTOINDENT, 1);
    s(AF_SCI_SETBRACEMATCH, 1);
}

} // namespace af
