// Getting around the code: open the header of an #include, go to a definition, the editor's
// context menu (right mouse button) and Ctrl-click.
#include "App.h"
#include "Commands.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/popupmenu.h>
#include <classes/popupmenu.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

// Where <...> includes are found with the SDK's GCC 11.
static const char *systemIncludes[] = {
    "SDK:Include/include_h",
    "SDK:Include/netinclude",
    "SDK:newlib/include",
    "SDK:local/newlib/include",
    "SDK:local/common/include",
    "SDK:gcc/ppc-amigaos/bin/include/c++/11.5.0",
    "SDK:gcc/ppc-amigaos/bin/include/c++/11.5.0/ppc-amigaos",
    "SDK:gcc/ppc-amigaos/bin/lib/gcc/ppc-amigaos/11.5.0/include",
    nullptr
};

static bool sdkPresent()
{
    struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
    bool found = IDOS->FindDosEntry(dl, "SDK", LDF_ALL) != nullptr;
    IDOS->UnLockDosList(LDF_ALL | LDF_READ);
    return found;
}

std::string App::resolveInclude(const std::string &name, bool quoted, const std::string &fromFile)
{
    std::vector<std::string> dirs;
    if (quoted) {
        if (!fromFile.empty()) dirs.push_back(dirName(fromFile));
        if (!project.dir.empty()) dirs.push_back(project.dir);
    }
    // project headers can also be included with <...>
    if (!project.dir.empty() && !quoted) dirs.push_back(project.dir);
    if (sdkPresent())
        for (int i = 0; systemIncludes[i]; i++) dirs.push_back(systemIncludes[i]);
    for (auto &d : dirs) {
        std::string p = joinPath(d, name);
        if (fileExists(p)) return p;
    }
    return "";
}

// The text of a line, without its line end.
static std::string lineText(SciView *v, intptr_t line)
{
    intptr_t a = S(v, SCI_POSITIONFROMLINE, line), b = S(v, SCI_GETLINEENDPOSITION, line);
    if (b <= a) return "";
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr = { { a, b }, buf.data() };
    S(v, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return buf.data();
}

// "#include <proto/exec.h>" on this line? Returns the name and whether it was "quoted".
static bool includeOnLine(const std::string &line, std::string &name, bool &quoted)
{
    size_t p = line.find_first_not_of(" \t");
    if (p == std::string::npos || line[p] != '#') return false;
    p = line.find_first_not_of(" \t", p + 1);
    if (p == std::string::npos || line.compare(p, 7, "include") != 0) return false;
    p = line.find_first_of("<\"", p + 7);
    if (p == std::string::npos) return false;
    char close = line[p] == '<' ? '>' : '"';
    size_t e = line.find(close, p + 1);
    if (e == std::string::npos) return false;
    name = line.substr(p + 1, e - p - 1);
    quoted = close == '"';
    return !name.empty();
}

std::string App::wordAt(intptr_t pos)
{
    if (!editor) return "";
    intptr_t a = S(editor, SCI_WORDSTARTPOSITION, pos, 1), b = S(editor, SCI_WORDENDPOSITION, pos, 1);
    if (b <= a) return "";
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr = { { a, b }, buf.data() };
    S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return buf.data();
}

bool App::openIncludeAt(intptr_t pos)
{
    if (!editor || !cur) return false;
    std::string name;
    bool quoted;
    if (!includeOnLine(lineText(editor, S(editor, SCI_LINEFROMPOSITION, pos)), name, quoted)) return false;
    std::string path = resolveInclude(name, quoted, cur->path);
    if (path.empty()) {
        setStatus("Can't find " + name + (sdkPresent() ? "" : " (the SDK isn't installed)"));
        IIntuition->DisplayBeep(nullptr);
        return true;
    }
    Document *d = openFile(path);
    // the SDK's headers are for reading
    if (d && strncasecmp(path.c_str(), "SDK:", 4) == 0) S(editor, SCI_SETREADONLY, 1);
    setStatus("Opened " + path);
    return true;
}

// Is `line` where `name` is defined? A function body ("type name(...)" not ending in ';'),
// struct/class/union/enum name, #define name, or a typedef ending in name.
static bool definesName(const std::string &line, const std::string &name, bool typesOnly = false)
{
    size_t p = 0;
    auto whole = [&](size_t at) {
        return (at == 0 || !(isalnum((unsigned char)line[at - 1]) || line[at - 1] == '_')) &&
               (at + name.size() >= line.size() || !(isalnum((unsigned char)line[at + name.size()]) || line[at + name.size()] == '_'));
    };
    std::string t = trim(line);
    if (t.empty() || t.compare(0, 2, "//") == 0 || t[0] == '*' || t.compare(0, 2, "/*") == 0) return false;
    while ((p = line.find(name, p)) != std::string::npos) {
        if (whole(p)) {
            std::string before = trim(line.substr(0, p)), after = trim(line.substr(p + name.size()));
            if (before.compare(0, 7, "#define") == 0 && trim(before.substr(7)).empty()) return true;
            for (const char *kw : { "struct", "class", "union", "enum", "namespace" }) {
                size_t n = strlen(kw);
                if (before.size() >= n && before.compare(before.size() - n, n, kw) == 0 &&
                    (after.empty() || after[0] == '{' || after[0] == ':'))
                    return true;
            }
            if (before.compare(0, 7, "typedef") == 0 && !after.empty() && after[0] == ';') return true;
            if (typesOnly) { p += name.size(); continue; }
            // a function definition: something before the name (its type), "(" after it,
            // the line not indented, and no ';' at the end
            bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            if (!indented && !before.empty() && !after.empty() && after[0] == '(' &&
                t.back() != ';' && before.find('=') == std::string::npos && before.find("return") == std::string::npos)
                return true;
        }
        p += name.size();
    }
    return false;
}

// A declaration of a variable or parameter: "Type name", "Type *name", "Type &name" followed by
// one of = ( ; , [ ) : or the end of the line, with a type (not a keyword or an expression) before.
static bool declaresName(const std::string &line, const std::string &name)
{
    static const char *notTypes[] = { "return", "if", "else", "case", "sizeof", "delete", "throw", "goto", "while", "new", nullptr };
    size_t p = 0;
    while ((p = line.find(name, p)) != std::string::npos) {
        size_t e = p + name.size();
        bool whole = (p == 0 || !(isalnum((unsigned char)line[p - 1]) || line[p - 1] == '_')) &&
                     (e >= line.size() || !(isalnum((unsigned char)line[e]) || line[e] == '_'));
        if (whole) {
            size_t a = e;
            while (a < line.size() && line[a] == ' ') a++;
            bool endOk = a >= line.size() || strchr("=(;,[):", line[a]);
            // what comes before: optional * or &, then a type name
            size_t b = p;
            while (b > 0 && (line[b - 1] == ' ' || line[b - 1] == '*' || line[b - 1] == '&')) b--;
            size_t te = b;
            while (b > 0 && (isalnum((unsigned char)line[b - 1]) || line[b - 1] == '_' || line[b - 1] == ':' || line[b - 1] == '>' || line[b - 1] == '<')) b--;
            std::string type = line.substr(b, te - b);
            bool typeOk = !type.empty() && (isalpha((unsigned char)type[0]) || type[0] == '_') && te < p;
            for (int k = 0; typeOk && notTypes[k]; k++) if (type == notTypes[k]) typeOk = false;
            // not a member access or call on something
            if (typeOk && b > 0) {
                char c = line[b - 1];
                if (c == '.' || c == '>' || c == '-') typeOk = false;
            }
            if (endOk && typeOk) return true;
        }
        p = e;
    }
    return false;
}

bool App::gotoDefinition(const std::string &name)
{
    if (name.empty()) return false;
    // a local variable or parameter: the nearest declaration above the cursor
    if (cur && editor) {
        intptr_t from = S(editor, SCI_LINEFROMPOSITION, S(editor, SCI_GETCURRENTPOS));
        for (intptr_t l = from - 1; l >= 0 && l > from - 400; l--) {
            std::string t = lineText(editor, l);
            if (t.find(name) == std::string::npos) continue;
            if (declaresName(t, name) && !definesName(t, name)) {
                S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, l);
                S(editor, SCI_GOTOLINE, l);
                setStatus(name + " is declared here");
                return true;
            }
        }
    }
    // the current document first (as edited), then the project's files, sources before headers
    if (cur && editor) {
        intptr_t lines = S(editor, SCI_GETLINECOUNT);
        for (intptr_t l = 0; l < lines; l++)
            if (definesName(lineText(editor, l), name, !isSourceFile(cur->path) ? false : true)) {
                gotoLocation(cur->path.empty() ? std::string() : cur->path, (int)l + 1);
                if (cur->path.empty()) S(editor, SCI_GOTOLINE, l);
                setStatus(name + " is defined here");
                return true;
            }
    }
    std::vector<std::string> files;
    for (int pass = 0; pass < 2; pass++)
        for (auto &f : project.files) {
            bool src = isSourceFile(f);
            if ((pass == 0) == src) files.push_back(project.abs(f));
        }
    if (cur && !cur->path.empty() && !project.contains(cur->path)) files.insert(files.begin(), cur->path);
    // types (struct/class/enum/#define/typedef) anywhere first, then functions
    for (int kind = 0; kind < 2; kind++)
        for (auto &f : files) {
            std::string text;
            if (!readFile(f, text)) continue;
            int lineNo = 1;
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t nl = text.find('\n', pos);
                std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (line.find(name) != std::string::npos && definesName(line, name, kind == 0)) {
                    gotoLocation(f, lineNo);
                    setStatus(name + " is defined in " + project.rel(f));
                    return true;
                }
                if (nl == std::string::npos) break;
                pos = nl + 1;
                lineNo++;
            }
        }
    // an OS function: its AutoDoc
    if (findDoc(name)) {
        lookupDoc(name);
        return true;
    }
    setStatus("No definition of " + name + " in the project");
    IIntuition->DisplayBeep(nullptr);
    return false;
}

// Ctrl-click: a header on an #include line, otherwise the definition of the word.
void App::ctrlClick(intptr_t pos)
{
    if (openIncludeAt(pos)) return;
    gotoDefinition(wordAt(pos));
}

// --- the editor's context menu ---

enum { PM_NONE, PM_INCLUDE, PM_DEFINITION, PM_AUTODOC, PM_COMPLETE, PM_BREAKPOINT, PM_RUNTO,
       PM_WATCH, PM_CUT, PM_COPY, PM_PASTE, PM_SHORTCUTS , PM_REFS, PM_RENAME, PM_NEXT_OCC, PM_SNIPPET};

void App::sciContextMenu(SciView *v, int, int)
{
    if (v == editor) queueCommand(CMD_CONTEXT_MENU);   // not from inside the input hook
}

void App::showContextMenu()
{
    if (!editor || !cur || !win) return;
    intptr_t pos = S(editor, SCI_GETCURRENTPOS);
    std::string word = wordAt(pos), inc;
    bool quoted;
    bool onInclude = includeOnLine(lineText(editor, S(editor, SCI_LINEFROMPOSITION, pos)), inc, quoted);
    bool hasDoc = !word.empty() && findDoc(word) != nullptr;

    static std::string tInclude, tDef, tDoc, tWatch;   // the menu keeps pointers to its titles
    tInclude = "Open " + (onInclude ? inc : std::string("Include File"));
    tDef = "Go to Definition" + (word.empty() ? std::string() : " of " + word) + "  (Ctrl-Click)";
    tDoc = "AutoDoc" + (hasDoc ? " for " + word : std::string()) + "  (F1)";
    tWatch = "Add Watch" + (word.empty() ? std::string() : " for " + word);
    static std::string tRefs, tRename;   // --- editor features ---
    tRefs = "Find All References" + (word.empty() ? std::string() : " to " + word) + "  (Shift-F12)";
    tRename = "Rename" + (word.empty() ? std::string() : " " + word) + "...  (F2)";

    Class *item = IPopupMenu->POPUPMENU_GetItemClass();
    auto mk = [&](const char *title, uint32 id, bool disabled = false) {
        return (uint32)IIntuition->NewObject(item, nullptr, PMIA_Title, (uint32)title, PMIA_ID, id,
                                             PMIA_Disabled, disabled, TAG_DONE);
    };
    auto bar = [&]() { return (uint32)IIntuition->NewObject(item, nullptr, PMIA_Title, (uint32)~0, TAG_DONE); };
    Object *menu = IIntuition->NewObject(IPopupMenu->POPUPMENU_GetClass(), nullptr,
        PMA_AddItem, mk(tInclude.c_str(), PM_INCLUDE, !onInclude),
        PMA_AddItem, mk(tDef.c_str(), PM_DEFINITION, word.empty()),
        PMA_AddItem, mk(tDoc.c_str(), PM_AUTODOC),
        PMA_AddItem, mk("Complete Word  (Ctrl-Space)", PM_COMPLETE),
        // --- editor features ---
        PMA_AddItem, mk(tRefs.c_str(), PM_REFS, word.empty()),
        PMA_AddItem, mk(tRename.c_str(), PM_RENAME, word.empty()),
        PMA_AddItem, mk("Add Next Occurrence  (Amiga-T)", PM_NEXT_OCC),
        PMA_AddItem, mk("Insert Snippet...", PM_SNIPPET),
        PMA_AddItem, bar(),
        PMA_AddItem, mk("Toggle Breakpoint  (F9)", PM_BREAKPOINT),
        PMA_AddItem, mk("Run to Cursor  (Ctrl-F10)", PM_RUNTO, !stopped()),
        PMA_AddItem, mk(tWatch.c_str(), PM_WATCH, word.empty()),
        PMA_AddItem, bar(),
        PMA_AddItem, mk("Cut", PM_CUT),
        PMA_AddItem, mk("Copy", PM_COPY),
        PMA_AddItem, mk("Paste", PM_PASTE),
        PMA_AddItem, bar(),
        PMA_AddItem, mk("Keyboard Shortcuts...", PM_SHORTCUTS),
        TAG_DONE);
    if (!menu) return;
    struct pmOpen msg = { PM_OPEN, win };
    Object *chosen = (Object *)IIntuition->IDoMethodA(menu, (Msg)&msg);
    uint32 id = PM_NONE;
    if (chosen) IIntuition->GetAttr(PMIA_ID, chosen, &id);
    IIntuition->DisposeObject(menu);
    switch (id) {
    case PM_INCLUDE: openIncludeAt(pos); break;
    case PM_DEFINITION: gotoDefinition(word); break;
    case PM_AUTODOC: if (hasDoc) lookupDoc(word); else openDocWindow(); break;
    case PM_COMPLETE: showCompletion(true); break;
    case PM_BREAKPOINT: toggleBreakpointAtCaret(); break;
    case PM_RUNTO: debugRunToCursor(); break;
    case PM_WATCH: addWatch(word); break;
    case PM_CUT: S(editor, SCI_CUT); break;
    case PM_COPY: S(editor, SCI_COPY); break;
    case PM_PASTE: S(editor, SCI_PASTE); break;
    case PM_SHORTCUTS: showShortcuts(); break;
    // --- editor features ---
    case PM_REFS: findReferences(); break;
    case PM_RENAME: renameSymbol(); break;
    case PM_NEXT_OCC: addNextOccurrence(false); break;
    case PM_SNIPPET: insertSnippetMenu(); break;
    }
}

void App::showShortcuts()
{
    ask("Keyboard Shortcuts",
        "Editing\n"
        "  Ctrl-Space      complete the word (also pops up by itself after 3 letters)\n"
        "  IExec->  etc.   lists that library's functions as you type\n"
        "  (               after an OS function: shows its prototype\n"
        "  F1 / Help       AutoDoc for the word under the cursor\n"
        "  Ctrl-click      open an #include / go to a definition\n"
        "  Right button    menu with all of these\n"
        "  Amiga-F/R/H/L   find / replace / find in files / go to line\n"
        "  Amiga-/         comment or uncomment lines\n"
        "  Shift-F12       find all references   F2   rename everywhere\n"
        "  Amiga-T         add the next occurrence (multiple cursors); Alt-click adds a cursor\n"
        "  name + Tab      insert a snippet (main, openlib, window, timer, rexx, tags, for...)\n"
        "\n"
        "Building\n"
        "  F7              build            Ctrl-F5 / F6   build and run\n"
        "  F4 / Shift-F4   next / previous error\n"
        "\n"
        "Debugging\n"
        "  F5              start / continue  Shift-F5      stop\n"
        "  F9              breakpoint        F10 / F11     step over / into\n"
        "  Shift-F11       step out          Ctrl-F10      run to cursor\n"
        "  F8              step one instruction",
        "_OK");
}

} // namespace af
