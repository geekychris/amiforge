// Amiga-aware explanations for build messages and crashes (see Explain.h).
//
// The texts were written against GCC 11.5 / binutils messages from the SDK's toolchain. GCC
// uses typographic quotes in a UTF-8 locale and ASCII ones in the C locale (the Amiga), so
// messages are normalised to ASCII quotes first.
#include "Explain.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <map>

namespace af {

// --- helpers ---

std::string fileNameOf(const std::string &h);

static std::string normalise(const std::string &in)
{
    std::string s;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = in[i];
        // UTF-8 left/right single and double quotes: E2 80 98/99/9C/9D
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80) {
            unsigned char d = in[i + 2];
            if (d == 0x98 || d == 0x99) { s += '\''; i += 2; continue; }
            if (d == 0x9C || d == 0x9D) { s += '"'; i += 2; continue; }
        }
        if (c == '`' || c == 0xB4) { s += '\''; continue; }   // linker `name', Latin-1 acute
        s += (char)c;
    }
    return s;
}

static bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

// The n-th 'quoted' word of a message.
static std::string quoted(const std::string &s, int n = 0)
{
    size_t p = 0;
    for (;;) {
        size_t a = s.find('\'', p);
        if (a == std::string::npos) return "";
        size_t b = s.find('\'', a + 1);
        if (b == std::string::npos) return "";
        if (n-- == 0) return s.substr(a + 1, b - a - 1);
        p = b + 1;
    }
}

static std::string hex32(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "0x%08lX", (unsigned long)v);
    return b;
}

std::string interfaceForLibrary(const std::string &lib)
{
    static const std::map<std::string, std::string> known = {
        { "exec.library", "IExec" }, { "dos.library", "IDOS" }, { "intuition.library", "IIntuition" },
        { "graphics.library", "IGraphics" }, { "gadtools.library", "IGadTools" }, { "utility.library", "IUtility" },
        { "diskfont.library", "IDiskfont" }, { "asl.library", "IAsl" }, { "icon.library", "IIcon" },
        { "layers.library", "ILayers" }, { "workbench.library", "IWorkbench" }, { "locale.library", "ILocale" },
        { "datatypes.library", "IDataTypes" }, { "keymap.library", "IKeymap" }, { "timer.device", "ITimer" },
        { "commodities.library", "ICommodities" }, { "rexxsyslib.library", "IRexxSys" },
        { "iffparse.library", "IIFFParse" }, { "expansion.library", "IExpansion" },
        { "application.library", "IApplication" }, { "elf.library", "IElf" }, { "newlib.library", "INewlib" },
        { "wb.library", "IWorkbench" }, { "amigaguide.library", "IAmigaGuide" }, { "bsdsocket.library", "ISocket" },
    };
    auto it = known.find(lib);
    if (it != known.end()) return it->second;
    std::string base = lib.substr(0, lib.find('.'));
    if (base.empty()) return "";
    base[0] = (char)toupper((unsigned char)base[0]);
    return "I" + base;
}

std::string protoForLibrary(const std::string &lib)
{
    std::string base = lib.substr(0, lib.find('.'));
    if (lib == "wb.library") base = "wb";
    return "proto/" + base + ".h";
}

std::string builtinLibraryOf(const std::string &f)
{
    static const char *const exec[] = { "AllocVec", "AllocVecTags", "AllocVecTagList", "FreeVec", "AllocMem", "FreeMem",
        "OpenLibrary", "CloseLibrary", "GetInterface", "DropInterface", "FindTask", "Wait", "Signal", "AllocSignal",
        "FreeSignal", "GetMsg", "PutMsg", "ReplyMsg", "WaitPort", "Forbid", "Permit", "CopyMem", "CopyMemQuick",
        "OpenDevice", "CloseDevice", "DoIO", "SendIO", "AbortIO", "WaitIO", "CheckIO", "AllocSysObject",
        "AllocSysObjectTags", "FreeSysObject", "NewList", "AddTail", "AddHead", "Remove", "RemHead", "RemTail",
        "Insert", "ObtainSemaphore", "ReleaseSemaphore", "DebugPrintF", "SetSignal", "CreateMsgPort", "DeleteMsgPort",
        "CreateIORequest", "DeleteIORequest", "FindPort", "AddPort", "RemPort", "MutexObtain", "MutexRelease", nullptr };
    static const char *const dos[] = { "Open", "Close", "Read", "Write", "Delay", "Lock", "UnLock", "PutStr", "Printf",
        "FPrintf", "SystemTags", "Execute", "CreateDir", "Delete", "DeleteFile", "GetVar", "SetVar", "ReadArgs",
        "FreeArgs", "Output", "Input", "IoErr", "PrintFault", "FGets", "FPuts", "Seek", "NameFromLock", "CurrentDir",
        "SetCurrentDir", "ParentDir", "LoadSeg", "UnLoadSeg", "RunCommand", "CreateNewProc", "CreateNewProcTags",
        "ExamineObjectTags", "ObtainDirContextTags", "ExamineDir", "MatchFirst", "MatchNext", "FFlush", "Flush",
        "VPrintf", "DateStamp", "AddPart", "FilePart", "PathPart", "Rename", "Examine", "ExNext", nullptr };
    static const char *const intuition[] = { "OpenWindow", "OpenWindowTags", "OpenWindowTagList", "CloseWindow",
        "OpenScreen", "OpenScreenTags", "OpenScreenTagList", "CloseScreen", "LockPubScreen", "UnlockPubScreen",
        "DisplayBeep", "ActivateWindow", "WindowToFront", "WindowToBack", "SetWindowTitles", "ModifyIDCMP",
        "RefreshGList", "NewObject", "NewObjectA", "DisposeObject", "SetAttrs", "SetAttrsA", "GetAttr", "SetGadgetAttrs",
        "EasyRequest", "EasyRequestArgs", "IDoMethod", "IDoMethodA", "DoGadgetMethod", "BeginRefresh", "EndRefresh",
        "SetMenuStrip", "ClearMenuStrip", "ItemAddress", "GetScreenDrawInfo", "FreeScreenDrawInfo", "SizeWindow",
        "MoveWindow", "ChangeWindowBox", "PrintIText", "DrawImage", "SetWindowAttrs", nullptr };
    static const char *const graphics[] = { "SetAPen", "SetBPen", "SetDrMd", "Move", "Draw", "RectFill", "WritePixel",
        "ReadPixel", "Text", "TextLength", "SetFont", "OpenFont", "CloseFont", "WaitTOF", "ObtainBestPen",
        "ObtainBestPenA", "ReleasePen", "SetRGB4", "SetRGB32", "LoadRGB32", "BltBitMap", "BltBitMapRastPort",
        "AllocBitMap", "FreeBitMap", "InitRastPort", "ScrollRaster", "DrawEllipse", "AreaMove", "AreaDraw", "AreaEnd",
        "WritePixelArray8", "SetRPAttrs", "GetRPAttrs", "BltPattern", "Flood", "GetBitMapAttr", "WritePixelColor",
        "RectFillColor", "CompositeTags", nullptr };
    static const char *const gadtools[] = { "CreateMenus", "CreateMenusA", "LayoutMenus", "LayoutMenusA", "FreeMenus",
        "GT_GetIMsg", "GT_ReplyIMsg", "CreateGadget", "CreateGadgetA", "CreateContext", "FreeGadgets", "GetVisualInfo",
        "GetVisualInfoA", "FreeVisualInfo", "GT_SetGadgetAttrs", "GT_RefreshWindow", nullptr };
    static const char *const utility[] = { "GetTagData", "FindTagItem", "NextTagItem", "Stricmp", "Strnicmp", "ToUpper",
        "ToLower", "SNPrintf", "Amiga2Date", "Date2Amiga", "CallHookPkt", nullptr };
    static const char *const asl[] = { "AllocAslRequest", "AllocAslRequestTags", "AslRequest", "AslRequestTags",
        "FreeAslRequest", nullptr };
    static const char *const icon[] = { "GetDiskObject", "GetDiskObjectNew", "PutDiskObject", "FreeDiskObject",
        "FindToolType", "MatchToolValue", "GetIconTags", "GetIconTagList", "PutIconTags", "PutIconTagList",
        "DrawIconState", "DrawIconStateA", nullptr };
    static const char *const timer[] = { "GetSysTime", "AddTime", "SubTime", "CmpTime", "GetUpTime", nullptr };
    static const struct { const char *lib; const char *const *names; } all[] = {
        { "exec.library", exec }, { "dos.library", dos }, { "intuition.library", intuition },
        { "graphics.library", graphics }, { "gadtools.library", gadtools }, { "utility.library", utility },
        { "asl.library", asl }, { "icon.library", icon }, { "timer.device", timer },
    };
    for (auto &l : all)
        for (const char *const *n = l.names; *n; n++)
            if (f == *n) return l.lib;
    if (f == "OpenDiskFont") return "diskfont.library";
    if (f == "MapRawKey") return "keymap.library";
    if (f == "NewDTObject" || f == "NewDTObjectA" || f == "DisposeDTObject") return "datatypes.library";
    if (f == "OpenCatalog" || f == "GetCatalogStr" || f == "CloseCatalog") return "locale.library";
    return "";
}

// Standard C functions and their headers.
static const char *stdHeaderFor(const std::string &f)
{
    static const std::map<std::string, const char *> m = {
        { "printf", "stdio.h" }, { "fprintf", "stdio.h" }, { "sprintf", "stdio.h" }, { "snprintf", "stdio.h" },
        { "puts", "stdio.h" }, { "fopen", "stdio.h" }, { "fclose", "stdio.h" }, { "fgets", "stdio.h" },
        { "fread", "stdio.h" }, { "fwrite", "stdio.h" }, { "getchar", "stdio.h" }, { "putchar", "stdio.h" },
        { "malloc", "stdlib.h" }, { "free", "stdlib.h" }, { "calloc", "stdlib.h" }, { "realloc", "stdlib.h" },
        { "exit", "stdlib.h" }, { "atoi", "stdlib.h" }, { "strtol", "stdlib.h" }, { "rand", "stdlib.h" },
        { "srand", "stdlib.h" }, { "abs", "stdlib.h" }, { "qsort", "stdlib.h" }, { "getenv", "stdlib.h" },
        { "strlen", "string.h" }, { "strcpy", "string.h" }, { "strncpy", "string.h" }, { "strcmp", "string.h" },
        { "strncmp", "string.h" }, { "strcat", "string.h" }, { "strchr", "string.h" }, { "strstr", "string.h" },
        { "memcpy", "string.h" }, { "memset", "string.h" }, { "memmove", "string.h" }, { "memcmp", "string.h" },
        { "strdup", "string.h" }, { "strcasecmp", "strings.h" }, { "sin", "math.h" }, { "cos", "math.h" },
        { "sqrt", "math.h" }, { "pow", "math.h" }, { "floor", "math.h" }, { "fabs", "math.h" }, { "atan2", "math.h" },
        { "time", "time.h" }, { "clock", "time.h" }, { "isdigit", "ctype.h" }, { "isalpha", "ctype.h" },
        { "toupper", "ctype.h" }, { "tolower", "ctype.h" }, { "isspace", "ctype.h" },
    };
    auto it = m.find(f);
    return it == m.end() ? nullptr : it->second;
}

// Headers for OS constants, by prefix.
static const char *headerForConstant(const std::string &name)
{
    static const struct { const char *prefix; const char *header; } m[] = {
        { "MEMF_", "exec/memory.h" }, { "AVT_", "exec/exectags.h" }, { "ASOT_", "exec/exectags.h" },
        { "ASO_", "exec/exectags.h" }, { "SIGBREAKF_", "dos/dos.h" }, { "MODE_", "dos/dos.h" },
        { "SHARED_LOCK", "dos/dos.h" }, { "EXCLUSIVE_LOCK", "dos/dos.h" }, { "SYS_", "dos/dostags.h" },
        { "NP_", "dos/dostags.h" }, { "EX_", "dos/obsolete.h or dos/dos.h" }, { "IDCMP_", "intuition/intuition.h" },
        { "WA_", "intuition/intuition.h" }, { "WFLG_", "intuition/intuition.h" }, { "SA_", "intuition/screens.h" },
        { "GA_", "intuition/gadgetclass.h" }, { "ICA_", "intuition/icclass.h" }, { "IA_", "intuition/imageclass.h" },
        { "TAG_", "utility/tagitem.h" }, { "JAM1", "graphics/rastport.h" }, { "JAM2", "graphics/rastport.h" },
        { "RPTAG_", "graphics/rpattr.h" }, { "BMF_", "graphics/gfx.h" }, { "LAYOUT_", "gadgets/layout.h" },
        { "WINDOW_", "classes/window.h" }, { "WM_", "classes/window.h" }, { "WMHI_", "classes/window.h" },
        { "BUTTON_", "gadgets/button.h" }, { "LISTBROWSER_", "gadgets/listbrowser.h" }, { "STRINGA_", "intuition/gadgetclass.h" },
        { "CHILD_", "gadgets/layout.h" }, { "CLICKTAB_", "gadgets/clicktab.h" }, { "CHOOSER_", "gadgets/chooser.h" },
        { "ASLFR_", "libraries/asl.h" }, { "NM_", "libraries/gadtools.h" }, { "GTMN_", "libraries/gadtools.h" },
        { "IECODE_", "devices/inputevent.h" }, { "IEQUALIFIER_", "devices/inputevent.h" }, { "TR_", "devices/timer.h" },
        { "UNIT_", "devices/timer.h" }, { "DTA_", "datatypes/datatypesclass.h" }, { "PDTA_", "datatypes/pictureclass.h" },
        { "ICONGETA_", "workbench/icon.h" }, { "WBTOOL", "workbench/workbench.h" },
    };
    for (auto &e : m)
        if (name.compare(0, strlen(e.prefix), e.prefix) == 0) return e.header;
    return nullptr;
}

static std::string osCallAdvice(const std::string &func, const std::string &lib)
{
    std::string iface = interfaceForLibrary(lib);
    return func + "() is a " + lib + " function. On AmigaOS 4 call it through the library's interface:\n"
           "    #include <" + protoForLibrary(lib) + ">\n"
           "    " + iface + "->" + func + "(...);\n"
           "AmiForge doesn't define __USE_INLINE__ (its macros clash with C++ names like Remove and Text), so "
           "the plain name only works if you add -D__USE_INLINE__ to the compiler flags yourself. "
           "Link with -lauto so " + iface + " is opened for you.";
}

static bool isOsInterfaceName(const std::string &n)
{
    // IExec, IDOS, IIntuition, IGraphics... (and *Base symbols)
    if (n.size() > 1 && n[0] == 'I' && isupper((unsigned char)n[1])) return true;
    return n.size() > 4 && n.compare(n.size() - 4, 4, "Base") == 0 && isupper((unsigned char)n[0]);
}

static std::string libraryForInterface(const std::string &iface)
{
    static const char *libs[] = { "exec.library", "dos.library", "intuition.library", "graphics.library",
        "gadtools.library", "utility.library", "diskfont.library", "asl.library", "icon.library", "layers.library",
        "workbench.library", "locale.library", "datatypes.library", "keymap.library", "timer.device",
        "commodities.library", "rexxsyslib.library", "iffparse.library", "application.library", nullptr };
    for (const char **l = libs; *l; l++) {
        if (interfaceForLibrary(*l) == iface) return *l;
        std::string base = std::string(*l).substr(0, std::string(*l).find('.'));
        std::string b = base;
        b[0] = (char)toupper((unsigned char)b[0]);
        if (iface == b + "Base" || (base == "dos" && iface == "DOSBase") || (base == "gadtools" && iface == "GadToolsBase"))
            return *l;
    }
    return "";
}

// --- diagnostics ---

// --- lang --- Python's errors (from Build's syntax check or a traceback after Run)
static Explanation explainPython(const std::string &m, const std::string &sourceLine)
{
    Explanation e;
    auto set = [&](const char *rule, const std::string &title, const std::string &text) {
        e.rule = rule;
        e.title = title;
        e.text = text;
        return e;
    };
    // AmiForge's own checker or runner failing to compile: the interpreter is Python 2
    if (has(m, "AmiForge-pycheck.py") || has(m, "AmiForge-pyrun.py") || has(m, "will become a reserved keyword in Python 2.6"))
        return set("py-version2", "The interpreter is Python 2",
                   "AmiForge needs Python 3, but this interpreter can't read Python 3 code (AmigaOS 4.1's own "
                   "C:python is Python 2.5). Set the Python 3 interpreter in Project > Settings, or for every "
                   "project with  setenv SAVE AmiForge/Python <path of python3>.");
    if (has(m, "TabError:"))
        return set("py-tab", "Tabs and spaces are mixed in the indentation",
                   "Python compares indentation exactly, and this block mixes tab characters with spaces. "
                   "AmiForge indents Python with four spaces: View > Show Whitespace shows the tabs (as arrows); "
                   "replace them with spaces.");
    if (has(m, "IndentationError:")) {
        if (has(m, "expected an indented block"))
            return set("py-indent-expected", "The block after ':' is empty or not indented",
                       "A line ending in ':' (if, for, while, def, class, with, try...) must be followed by at least "
                       "one line indented one level deeper. If the block is meant to be empty, write 'pass' in it.");
        if (has(m, "unexpected indent"))
            return set("py-indent-unexpected", "This line is indented more than the code around it",
                       "Only the lines of a block (after a line ending in ':') are indented deeper. Unindent this line "
                       "to the level of the lines before it (Shift-Tab or Amiga-[).");
        return set("py-indent-mismatch", "The indentation doesn't match any outer block",
                   "When a block ends, the next line must line up exactly with an earlier line. Check the spaces at "
                   "the start of this line against the 'if'/'for'/'def' it belongs to; View > Show Whitespace helps.");
    }
    if (has(m, "SyntaxError:")) {
        if (has(m, "expected ':'"))
            return set("py-colon", "A ':' is missing at the end of the line",
                       "if, elif, else, for, while, def, class, with, try, except and finally lines end with a colon, "
                       "and the indented block follows on the next lines.");
        if (has(m, "was never closed") || has(m, "unexpected EOF"))
            return set("py-unclosed", "A bracket is opened but never closed",
                       "A '(', '[' or '{' on this line (or above it) has no closing partner, so Python reads on to "
                       "the end of the file. Put the cursor next to a bracket: AmiForge highlights its partner, or "
                       "red if there is none.");
        if (has(m, "unterminated string") || has(m, "unterminated triple-quoted") || has(m, "EOL while scanning"))
            return set("py-string", "A string isn't closed",
                       "The string starting on this line has no closing quote. Strings in single quotes end on the "
                       "same line; for text over several lines use triple quotes (\"\"\"...\"\"\").");
        if (has(m, "Missing parentheses in call to 'print'") || (has(m, "invalid syntax") && sourceLine.find("print ") != std::string::npos))
            return set("py-print", "print is a function in Python 3",
                       "Write print(\"text\", value) with parentheses. Python 2 code needs this change throughout.");
        if (has(m, "invalid character") || has(m, "non-UTF-8") || has(m, "Non-UTF-8"))
            return set("py-encoding", "A character Python can't read",
                       "Python reads source files as UTF-8, but AmigaOS editors (and AmiForge) save Latin-1. Keep "
                       "accented letters out of the code, or put  # -*- coding: latin-1 -*-  on the first line.");
        if (has(m, "'return' outside function"))
            return set("py-return", "'return' outside a function",
                       "return only works inside a def. To end a script early use sys.exit().");
        return set("py-syntax", "Python can't read this line",
                   "Look at the line (and the one before it) for a missing ':', ')' or quote, a missing comma "
                   "between items, or '=' where '==' was meant in a condition.");
    }
    if (has(m, "NameError:")) {
        std::string name = quoted(m);
        static const char *const modules[] = { "os", "sys", "time", "math", "re", "json", "random", "argparse",
                                               "datetime", "pathlib", "subprocess", "shutil", "string", nullptr };
        for (const char *const *mod = modules; *mod; mod++)
            if (name == *mod)
                return set("py-name-import", "'" + name + "' needs an import",
                           "Add  import " + name + "  at the top of the file.");
        return set("py-name", "'" + name + "' isn't defined here",
                   "Python looks the name up when the line runs: it may be misspelt (names are case-sensitive), "
                   "defined further down or inside another function, or need an import. A variable set only in "
                   "one branch of an if is undefined when the other branch ran.");
    }
    if (has(m, "ModuleNotFoundError:") || has(m, "No module named")) {
        std::string mod = quoted(m);
        return set("py-module", "The module '" + mod + "' isn't available",
                   "The AmigaOS 4 port of Python can't load compiled extension modules (.so files): only modules "
                   "built into the interpreter and pure-Python modules work. Pure-Python packages go in the "
                   "standard library drawer (the lib drawer next to the interpreter) or a drawer on PYTHONPATH; "
                   "the port's amiga.pip installs pure-Python wheels. Packages with C code (numpy, Pillow...) "
                   "can't be used. A module of the project must be in the project's drawer (the main script's "
                   "drawer is searched first).");
    }
    if (has(m, "ImportError:") && has(m, "cannot import name"))
        return set("py-import-name", "The module has no '" + quoted(m) + "'",
                   "Check the spelling, and whether the name exists in this Python version (3.12). Two modules "
                   "importing each other can also cause this.");
    if (has(m, "RecursionError:"))
        return set("py-recursion", "A function calls itself without end",
                   "Check the condition that should stop the recursion. If the recursion really is that deep, "
                   "sys.setrecursionlimit() raises the limit, but the stack AmiForge gives the script (2 MB) "
                   "limits it too.");
    if (has(m, "UnicodeDecodeError:") || has(m, "UnicodeEncodeError:"))
        return set("py-unicode", "Text in the wrong character set",
                   "Files on AmigaOS are usually Latin-1, while Python 3 reads text as UTF-8. Open them with "
                   "open(name, encoding=\"latin-1\").");
    if (has(m, "FileNotFoundError:"))
        return set("py-file", "The file isn't there",
                   "Amiga paths look like DH1:Drawer/File or Drawer/File (relative to the project's drawer, where "
                   "Run starts the script); there is no leading '/' for the root, and \"/\" alone means the parent drawer.");
    if (has(m, "ZeroDivisionError:"))
        return set("py-zerodiv", "Division by zero", "A value used as a divisor is 0: check it before dividing.");
    if (has(m, "KeyboardInterrupt"))
        return set("py-stopped", "The script was stopped",
                   "Stop Program (Shift-Ctrl-F5) or Ctrl-C ended it; that's not an error in the script.");
    return e;
}

Explanation explainDiagnostic(const std::string &raw, const std::string &sourceLine, const LibraryLookup &lookup)
{
    std::string m = normalise(raw);
    // --- lang ---
    {
        Explanation py = explainPython(m, sourceLine);
        if (!py.empty()) return py;
    }
    auto libOf = [&](const std::string &f) {
        std::string l = lookup ? lookup(f) : "";
        return l.empty() ? builtinLibraryOf(f) : l;
    };
    Explanation e;

    // 1. a macro from inline4/*.h (-D__USE_INLINE__) hit a C++ member or local name
    if (has(m, "macro \"") && (has(m, "passed") || has(m, "requires"))) {
        size_t a = m.find("macro \"") + 7;
        std::string name = m.substr(a, m.find('"', a) - a);
        e.rule = "use-inline-clash";
        e.title = "'" + name + "' is an OS macro here";
        e.text = "With -D__USE_INLINE__ the inline4/*.h headers define every OS function name as a macro, so "
                 "your own '" + name + "' (a method, variable or function) is replaced by a call to the OS. "
                 "Remove -D__USE_INLINE__ from the flags and call OS functions as IExec->" + name +
                 "(...), or #undef " + name + " after the #includes.";
        return e;
    }
    // 2. implicit declaration / not declared: an OS function, a C library function, or unknown
    if (has(m, "implicit declaration of function") || (has(m, "was not declared in this scope") && !has(m, "variable"))) {
        std::string f = quoted(m);
        std::string lib = libOf(f);
        if (!lib.empty()) {
            e.rule = "os-implicit-decl";
            e.title = f + "() needs its interface";
            e.text = osCallAdvice(f, lib);
            return e;
        }
        if (const char *h = stdHeaderFor(f)) {
            e.rule = "std-implicit-decl";
            e.title = f + "() needs <" + std::string(h) + ">";
            e.text = "Add #include <" + std::string(h) + "> at the top of the file.";
            return e;
        }
        if (const char *h = headerForConstant(f)) {
            e.rule = "os-constant-undeclared";
            e.title = f + " is defined in <" + std::string(h) + ">";
            e.text = f + " is an OS constant: #include <" + std::string(h) + ">.";
            return e;
        }
        e.rule = has(m, "implicit declaration") ? "implicit-decl" : "not-declared";
        e.title = "'" + f + "' isn't declared";
        e.text = "Nothing called '" + f + "' is declared before this line. If it's your own function, declare it "
                 "in a header and #include that header, or move the function above its first use. Check the "
                 "spelling (C is case-sensitive). With F12 on the name you can see whether AmiForge finds a definition.";
        return e;
    }
    // 3. undeclared identifier: OS constants by prefix
    if (has(m, "undeclared")) {
        std::string n = quoted(m);
        if (const char *h = headerForConstant(n)) {
            e.rule = "os-constant-undeclared";
            e.title = n + " is defined in <" + std::string(h) + ">";
            e.text = n + " is an OS constant: #include <" + std::string(h) + ">.";
            return e;
        }
        if (n == "NULL") {
            e.rule = "null-undeclared";
            e.title = "NULL needs <stddef.h>";
            e.text = "#include <stddef.h> (or <exec/types.h>, which also defines it).";
            return e;
        }
        e.rule = "undeclared";
        e.title = "'" + n + "' isn't declared";
        e.text = "There's no variable or constant called '" + n + "' here. Declare it (or the header that "
                 "defines it) before this line; check the spelling and whether it's declared inside another "
                 "function or block.";
        return e;
    }
    // 4. Amiga types without <exec/types.h>
    if (has(m, "unknown type name")) {
        std::string t = quoted(m);
        static const char *amigaTypes[] = { "BOOL", "BYTE", "UBYTE", "WORD", "UWORD", "LONG", "ULONG", "APTR", "STRPTR",
            "CONST_STRPTR", "BPTR", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32",
            "TEXT", "VOID", "Tag", nullptr };
        for (const char **a = amigaTypes; *a; a++)
            if (t == *a) {
                e.rule = "amiga-type";
                e.title = t + " is an Amiga type";
                e.text = t + " is defined in <exec/types.h>: #include it (the <proto/...> headers include it too).";
                return e;
            }
        e.rule = "unknown-type";
        e.title = "Unknown type '" + t + "'";
        e.text = "Include the header that defines " + t + ", or check its spelling. For an OS struct, "
                 "'struct Window *' needs the struct keyword in C.";
        return e;
    }
    // 5. printf formats: %d with Amiga 32-bit types (which are long), %ld with int
    if (has(m, "format '%") && has(m, "expects argument of type")) {
        if ((has(m, "'%d'") || has(m, "'%u'") || has(m, "'%x'") || has(m, "'%i'")) &&
            (has(m, "int32") || has(m, "long int") || has(m, "LONG") || has(m, "ULONG"))) {
            e.rule = "format-long";
            e.title = "Amiga 32-bit types are long: use %ld";
            e.text = "int32, uint32, LONG and ULONG are 'long' on AmigaOS, so printf needs %ld / %lu / %lx for them. "
                     "Writing %ld and casting with (long) works everywhere:\n    printf(\"%ld\\n\", (long)value);";
            return e;
        }
        if (has(m, "'%l") && has(m, "has type 'int'")) {
            e.rule = "format-int-as-long";
            e.title = "%ld given an int";
            e.text = "The argument is an int but the format says long. Cast it: (long)value. Amiga code "
                     "conventionally uses %ld with a (long) cast for every integer.";
            return e;
        }
        e.rule = "format";
        e.title = "printf format doesn't match the argument";
        e.text = "Make the % conversion match the argument's type, or cast the argument: %ld + (long), "
                 "%lu + (unsigned long), %s for strings, %p for pointers.";
        return e;
    }
    // 6. redeclaring library bases
    if (has(m, "conflicting types for") || (has(m, "redefinition") || has(m, "redeclaration"))) {
        std::string n = quoted(m);
        if (n == "SysBase" || n == "DOSBase" || n == "IntuitionBase" || n == "GfxBase" || n == "UtilityBase" ||
            (isOsInterfaceName(n) && !libraryForInterface(n).empty())) {
            e.rule = "base-redeclared";
            e.title = n + " is already declared by the OS headers";
            e.text = "On AmigaOS 4 the <proto/...> headers declare " + n + " (as struct Library * or an interface "
                     "pointer). Remove your own declaration, or keep it for OS3 builds only with "
                     "#ifndef __amigaos4__ ... #endif. With -lauto you don't need to open the library at all.";
            return e;
        }
    }
    // 7. linker: OS interfaces / library bases not defined
    if (has(m, "undefined reference to")) {
        std::string n = quoted(m.substr(m.find("undefined reference to")));
        if (isOsInterfaceName(n) || n == "SysBase" || n == "DOSBase") {
            std::string lib = libraryForInterface(n);
            e.rule = "interface-undefined";
            e.title = n + " isn't defined: link with -lauto";
            e.text = n + (lib.empty() ? "" : " (" + lib + ")") + " is the global the OS headers refer to. Link "
                     "with -lauto (Project > Settings > Libraries) and it is defined and opened for you; or define "
                     "it yourself and open it:\n"
                     "    struct Library *Base = IExec->OpenLibrary(\"" + (lib.empty() ? std::string("x.library") : lib) + "\", 53);\n"
                     "    " + n + " = (struct ...IFace *)IExec->GetInterface(Base, \"main\", 1, NULL);";
            return e;
        }
        if (n.compare(0, 9, "__gthread") == 0 || n.compare(0, 8, "pthread_") == 0 || n.find("__gthr") == 0) {
            e.rule = "athread";
            e.title = "C++ needs a thread model: -athread=native";
            e.text = "The C++ runtime (libstdc++) uses threads for its locks. Link with -athread=native (AmiForge "
                     "adds it when the project has C++ files; add it to the linker flags if you link yourself), "
                     "or -athread=pthread to use pthreads.";
            return e;
        }
        if (n == "main") {
            e.rule = "no-main";
            e.title = "There's no main()";
            e.text = "The startup code calls main(), and no file defines it. Check that the file with main() is "
                     "in the project (Project > Add Files) and that it isn't inside #if 0 or a namespace.";
            return e;
        }
        std::string osLib = libOf(n);
        if (!osLib.empty()) {
            e.rule = "os-undefined";
            e.title = n + "() needs its interface";
            e.text = osCallAdvice(n, osLib);
            return e;
        }
        static const char *mathFns[] = { "sin", "cos", "tan", "sqrt", "pow", "exp", "log", "floor", "ceil", "atan2",
            "fmod", "sinf", "cosf", "sqrtf", "powf", nullptr };
        for (const char **f = mathFns; *f; f++)
            if (n == *f) {
                e.rule = "libm";
                e.title = n + "() is in the maths library";
                e.text = "Add -lm to the libraries (Project > Settings). Floating-point code should also be built "
                         "with -mcpu=440fp on the sam460ex, not plain -mcpu=440.";
                return e;
            }
        e.rule = "undefined-reference";
        e.title = "'" + n + "' is used but never defined";
        e.text = "The linker can't find " + n + ". If it's yours, the file that defines it may not be in the "
                 "project (Project > Add Files), or it's declared but has no body, or C++ code calls a C "
                 "function without extern \"C\". If it's from a library, add -l<name> to the libraries.";
        return e;
    }
    if (has(m, "multiple definition of")) {
        std::string n = quoted(m.substr(m.find("multiple definition of")));
        e.rule = "multiple-definition";
        e.title = "'" + n + "' is defined more than once";
        e.text = "Two files define " + n + ". Usually a header defines a variable or function instead of only "
                 "declaring it: in the header write 'extern int " + n + ";' (or make the function static inline) "
                 "and define it in exactly one .c file.";
        return e;
    }
    if (has(m, "relocation truncated to fit")) {
        e.rule = "reloc-truncated";
        e.title = "The program is too big for short addressing";
        if (has(m, "SDA") || has(m, "sdata")) {
            e.text = "Too much small data (globals addressed relative to r2/r13). Compile with -msdata=none or "
                     "-G 0 to put globals in normal data.";
        } else {
            e.text = "A branch or address doesn't fit in the instruction (REL24 branches reach +/-32 MB). Build with "
                     "-mlongcall, or split very large static arrays out of the code.";
        }
        return e;
    }
    // 8. C++ needs casts from APTR (void *)
    if (has(m, "invalid conversion from 'APTR'") || (has(m, "invalid conversion from 'void*'"))) {
        e.rule = "cxx-aptr-cast";
        e.title = "C++ needs a cast from APTR";
        e.text = "OS functions return APTR (void *). C converts it silently; C++ needs an explicit cast:\n"
                 "    char *buf = (char *)IExec->AllocVecTags(100, TAG_END);";
        return e;
    }
    // 9. string literals and STRPTR
    if ((has(m, "STRPTR") && has(m, "string constant")) || (has(m, "discards 'const' qualifier") && has(sourceLine, "\""))) {
        e.rule = "strptr-const";
        e.title = "A string literal is const";
        e.text = "STRPTR is 'char *' but a string literal is 'const char *'. Use CONST_STRPTR for parameters you "
                 "don't change, or cast: (STRPTR)\"text\". OS4's own prototypes mostly take CONST_STRPTR.";
        return e;
    }
    if (has(m, "discards 'const' qualifier")) {
        e.rule = "const";
        e.title = "Passing a const pointer to a non-const parameter";
        e.text = "The function may modify what it's given. Make the parameter const (CONST_STRPTR, const char *) "
                 "if it doesn't, or pass a writable copy.";
        return e;
    }
    // 10. missing headers
    if (has(m, "No such file or directory") && (has(m, "fatal error") || has(m, ".h"))) {
        std::string h = m.substr(0, m.find(':'));
        size_t fe = m.find("fatal error: ");
        if (fe != std::string::npos) {
            std::string rest = m.substr(fe + 13);
            h = rest.substr(0, rest.find(':'));
        }
        e.rule = "missing-header";
        e.title = h + " not found";
        if (h.compare(0, 5, "clib/") == 0 || h.compare(0, 7, "pragmas") == 0 || h.compare(0, 7, "inline/") == 0)
            e.text = "That's an AmigaOS 3 header. On AmigaOS 4 include <proto/" + fileNameOf(h) + "> instead.";
        else if (h.compare(0, 6, "proto/") == 0 || h.compare(0, 5, "exec/") == 0 || h.compare(0, 4, "dos/") == 0 ||
                 h.compare(0, 10, "intuition/") == 0 || h.compare(0, 9, "graphics/") == 0)
            e.text = "The SDK's headers should have it. Check that the SDK is installed (SDK:include/include_h) and "
                     "that the name is right; not every library has a proto header (third-party ones need their "
                     "own SDK).";
        else
            e.text = "Check the name, and where the header is: \"quoted\" headers are looked for next to the "
                     "source file; for another drawer add -I<drawer> to the compiler flags.";
        return e;
    }
    // 11. struct has no member: OS3 names
    if (has(m, "has no member named")) {
        std::string member = quoted(m, 1);
        if (member == "tv_secs" || member == "tv_micro" || member == "tr_time" || member == "tr_node") {
            e.rule = "old-timeval";
            e.title = "OS4 renamed the timer structures";
            e.text = "OS4's struct TimeVal has Seconds/Microseconds and struct TimeRequest has Request/Time. Use "
                     "those, or compile with -D__USE_OLD_TIMEVAL__ to keep the OS3 names (struct timeval, tv_secs, "
                     "tv_micro, struct timerequest, tr_time).";
            return e;
        }
        e.rule = "no-member";
        e.title = "No member '" + member + "'";
        e.text = "Check the struct's definition (F12 on the type opens it). Some OS3 fields are private or renamed "
                 "on OS4 - use the accessor functions or tags the AutoDocs describe.";
        return e;
    }
    if (has(m, "storage size of") && has(m, "isn't known")) {
        e.rule = "incomplete-type";
        e.title = "The struct's definition isn't included";
        e.text = "The type is only declared here. Include its header, e.g. <devices/timer.h> for struct "
                 "timerequest (with -D__USE_OLD_TIMEVAL__ for the OS3 names), <intuition/intuition.h> for "
                 "struct Window.";
        return e;
    }
    if (has(m, "makes pointer from integer without a cast")) {
        e.rule = "int-to-pointer";
        e.title = "An int used as a pointer";
        e.text = "Often the result of calling an undeclared function, which C assumes returns int: look for an "
                 "'implicit declaration' warning above and include the right <proto/...> header.";
        return e;
    }
    if (has(m, "frame size of") && has(m, "larger than")) {
        e.rule = "big-frame";
        e.title = "A function uses a lot of stack";
        e.text = "Big local arrays live on the stack, which is small on the Amiga (the shell's default is "
                 "often 16-64 KB). Allocate them with IExec->AllocVecTags() or make them static, or ask for "
                 "a bigger stack with a cookie in one source file:\n"
                 "    static const char __attribute__((used)) stack[] = \"$STACK: 500000\";";
        return e;
    }
    if (has(m, "ISO C++ forbids converting a string constant")) {
        e.rule = "strptr-const";
        e.title = "A string literal is const";
        e.text = "STRPTR is 'char *' but a string literal is 'const char *'. Use CONST_STRPTR for parameters you "
                 "don't change, or cast: (STRPTR)\"text\".";
        return e;
    }
    return e;
}

// fileName for "clib/exec_protos.h" -> "exec.h"
std::string fileNameOf(const std::string &h)
{
    std::string n = h.substr(h.find('/') + 1);
    size_t u = n.find("_protos");
    if (u != std::string::npos) n = n.substr(0, u) + ".h";
    return n;
}

// --- crashes ---

bool parseMemOperand(const std::string &insn, int &baseReg, int32_t &disp, bool &isStore)
{
    // "lwz r9,24(r3)", "stw r0,-8(r1)", "lbzu r0,1(r9)"
    size_t sp = insn.find_first_of(" \t");
    if (sp == std::string::npos) return false;
    std::string op = insn.substr(0, sp);
    size_t par = insn.find('(', sp), close = insn.find(')', sp);
    if (par == std::string::npos || close == std::string::npos) return false;
    size_t comma = insn.rfind(',', par);
    if (comma == std::string::npos) return false;
    std::string d = insn.substr(comma + 1, par - comma - 1);
    std::string r = insn.substr(par + 1, close - par - 1);
    if (r.size() < 2 || r[0] != 'r') return false;
    baseReg = atoi(r.c_str() + 1);
    disp = (int32_t)strtol(d.c_str(), nullptr, 0);
    isStore = op[0] == 's' && op.compare(0, 4, "sync") != 0;
    return baseReg >= 0 && baseReg < 32;
}

static bool near(uint32_t v, uint32_t pattern, uint32_t range = 0x10000)
{
    return v >= pattern - range && v <= pattern + range;
}

Explanation explainCrash(const CrashInfo &c)
{
    Explanation e;
    int base = -1;
    int32_t disp = 0;
    bool store = false;
    bool haveOp = parseMemOperand(c.insn, base, disp, store);
    if (c.trapType == 0x300) store = store || (c.dsisr & 0x02000000);
    const char *rw = store ? "writing to" : "reading from";
    std::string where = c.pcInProgram ? "" : " inside " + (c.function.empty() ? std::string("the OS") : c.function + "()") +
                                              (c.module.empty() || c.module == "program" ? "" : " (" + c.module + ")");
    bool ifaceCall = false;
    std::string ifaceName;
    for (size_t p = c.sourceLine.find("->"); p != std::string::npos; p = c.sourceLine.find("->", p + 2)) {
        size_t s = p;
        while (s > 0 && (isalnum((unsigned char)c.sourceLine[s - 1]) || c.sourceLine[s - 1] == '_')) s--;
        std::string n = c.sourceLine.substr(s, p - s);
        if (n.size() > 1 && n[0] == 'I' && isupper((unsigned char)n[1])) { ifaceCall = true; ifaceName = n; break; }
    }

    // stack overflow: a store just below the stack pointer, or very deep recursion
    if ((c.trapType == 0x300 && c.dar < c.sp && c.sp - c.dar < 0x10000 && (base == 1 || c.dar + 0x1000 > c.sp - 0x2000)) ||
        c.deepestRepeat > 200 || c.frames > 1000) {
        e.rule = "stack-overflow";
        e.title = "Stack overflow";
        e.text = std::string("The program ran out of stack") +
                 (c.deepestRepeat > 200 ? " (a function calls itself " + std::to_string(c.deepestRepeat) +
                                           " levels deep - check the recursion's end condition)" : "") +
                 ". AmigaOS doesn't grow stacks: big local arrays and deep recursion overflow them silently. "
                 "Make big arrays static or allocate them, or ask for a bigger stack with a cookie in one file:\n"
                 "    static const char __attribute__((used)) stack[] = \"$STACK: 500000\";\n"
                 "or run the program with a bigger Stack in the shell / its icon.";
        return e;
    }
    if (c.trapType == 0x300) {
        uint32_t a = c.dar;
        if (a < 0x10000) {
            e.rule = "null-pointer";
            e.title = std::string("NULL pointer: ") + rw + " address " + hex32(a);
            std::string field = a ? " The offset " + std::to_string(a) + " (" + hex32(a) + ") is the field being accessed: "
                                    "the pointer to the struct is NULL." : "";
            e.text = "The program dereferenced a NULL pointer" + where + "." + field;
            if (haveOp && base >= 0) e.text += " The instruction (" + c.insn + ") used r" + std::to_string(base) + " = " + hex32(c.gpr[base]) + " as the pointer.";
            if (ifaceCall)
                e.text += "\nThis line calls " + ifaceName + "->...: if " + ifaceName + " is NULL, the library wasn't opened. "
                          "Link with -lauto, or check OpenLibrary()/GetInterface() succeeded before using it.";
            else if (!c.pcInProgram)
                e.text += "\nThe crash is in the OS, so one of the arguments you passed" +
                          (c.callerFunction.empty() ? std::string("") : " from " + c.callerFunction + "()") +
                          " is probably NULL: a window, screen, RastPort or file handle that failed to open.";
            else
                e.text += "\nCheck the result of every allocation and Open/OpenWindow/Lock call before using it.";
            return e;
        }
        struct { uint32_t pat; const char *what; } pats[] = {
            { 0xDEADBEEF, "memory that has been freed (MungWall fills freed memory with 0xDEADBEEF)" },
            { 0xABADCAFE, "memory that was allocated but never initialised (MungWall fills new memory with 0xABADCAFE)" },
            { 0xCCCCCCCC, "an uninitialised variable" },
            { 0xBAADF00D, "uninitialised heap memory" },
            { 0xCAFEBABE, "freed or uninitialised memory" },
        };
        for (auto &p : pats)
            if (near(a, p.pat)) {
                e.rule = "bad-pattern";
                e.title = "Use of " + std::string(p.what).substr(0, std::string(p.what).find(" (")) ;
                e.text = std::string("The address ") + hex32(a) + " comes from " + p.what + ". A pointer was used after "
                         "FreeVec()/free()/delete, or before it was set. Look at where the pointer was last "
                         "assigned; set pointers to NULL after freeing them.";
                return e;
            }
        if (a >= 0xFFFF0000) {
            e.rule = "negative-pointer";
            e.title = "A pointer of -1 (or close to it)";
            e.text = "The address " + hex32(a) + " is a small negative number used as a pointer: usually an error "
                     "result (-1) stored in a pointer, or an index of -1 into an array.";
            return e;
        }
        if (!c.pcInProgram) {
            e.rule = "os-bad-argument";
            e.title = "Crash inside the OS" + (c.function.empty() ? std::string("") : " (" + c.function + ")");
            e.text = "The OS crashed " + std::string(rw) + " " + hex32(a) + " while running code your program called" +
                     (c.callerFunction.empty() ? std::string("") : " from " + c.callerFunction + "()" +
                      (c.callerLine ? " at line " + std::to_string(c.callerLine) : "")) +
                     ". The usual cause is a bad argument: a freed or uninitialised pointer, a tag list without "
                     "TAG_END, a buffer that's too small, or a structure from another program's memory.";
            return e;
        }
        e.rule = "bad-pointer";
        e.title = std::string("Bad pointer: ") + rw + " " + hex32(a);
        e.text = "There's no memory at " + hex32(a) + ". The pointer is uninitialised, freed, or was "
                 "overwritten (a buffer overflow on a neighbouring variable can do that).";
        if (haveOp && base >= 0) e.text += " The instruction (" + c.insn + ") used r" + std::to_string(base) + " = " + hex32(c.gpr[base]) + ".";
        return e;
    }
    if (c.trapType == 0x400) {
        e.rule = c.pc < 0x10000 ? "null-function-pointer" : "jump-to-data";
        e.title = c.pc < 0x10000 ? "Called a NULL function pointer" : "Jumped to memory that isn't code";
        e.text = c.pc < 0x10000
                     ? "The program called through a function pointer that is NULL (a callback or hook that was "
                       "never set). LR = " + hex32(c.lr) + " is just after the call."
                     : "The program jumped to " + hex32(c.pc) + ", which isn't code: a corrupted function pointer, or a "
                       "return address overwritten by a buffer overflow on the stack (writing past the end of a "
                       "local array).";
        return e;
    }
    if (c.trapType == 0x600) {
        e.rule = "alignment";
        e.title = "Misaligned access";
        e.text = "A load or store of a double/float (or a multi-word access) at a misaligned address (" + hex32(c.dar) +
                 "). This happens when a byte buffer is cast to a struct or double pointer. Copy the data with "
                 "memcpy() into a properly aligned variable instead.";
        return e;
    }
    if (c.trapType == 0x700) {
        e.rule = "program-check";
        e.title = "Illegal instruction or trap";
        e.text = c.pcInProgram
                     ? "The program executed a trap: a failed assert(), abort(), or a __builtin_trap() GCC inserts "
                       "where the code can't continue (undefined behaviour such as falling off the end of a "
                       "function that returns a value). Look at the source line."
                     : "An illegal instruction outside the program: usually a jump through a corrupted pointer. Check "
                       "function pointers and buffers on the stack.";
        return e;
    }
    if (c.trapType == 0x800) {
        e.rule = "fpu-unavailable";
        e.title = "Floating point unavailable";
        e.text = "Build floating-point code with -mcpu=440fp (hard float) consistently; mixing soft-float and "
                 "hard-float objects or libraries causes this.";
        return e;
    }
    return e;
}

} // namespace af
