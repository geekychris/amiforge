// The integrations' hooks into the rest of the IDE (App::integrations*), the distcc
// settings and the explanations of build messages and crashes.
//
// The shared code (MainWindow, Editor, BuildUI, Commands, DebugUI) calls these at a few
// well-defined points, each marked "--- integrations ---" there.
#include "App.h"
#include "Commands.h"
#include "Explain.h"
#include "DistccCmd.h"
#include "HexData.h"
#include "Integrations.h"
#include "RexxCmd.h"

#include <string.h>
#include <strings.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/clicktab.h>
#include <proto/listbrowser.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <proto/string.h>
#include <proto/checkbox.h>
#include <proto/wb.h>
#include <dos/var.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/clicktab.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <gadgets/checkbox.h>
#include <images/label.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }
std::string rexxPortName();

void App::integrationsInit()
{
    gitFiles.win = win;
    if (!openRexxPort()) setStatus("The ARexx port couldn't be opened");
}

void App::integrationsCleanup()
{
    resourceCleanup(this);
    closeRexxPort();
    gitCleanup(this);
}

uint32_t App::integrationsSignals()
{
    return resourceSignals(this) | rexxSignals(this);
}

void App::integrationsHandle(uint32_t got)
{
    uint32_t rs = rexxSignals(this);
    if (rs && (got & rs)) rexxHandle(this);
    resourceHandle(this, got);
}

void App::integrationsIdle()
{
    gitIdle(this);
}

int App::bottomPageIndex(const char *name)
{
    if (!bottomLabels) return -1;
    int i = 0;
    for (struct Node *n = bottomLabels->lh_Head; n->ln_Succ; n = n->ln_Succ, i++) {
        STRPTR t = nullptr;
        IClickTab->GetClickTabNodeAttrs(n, TNA_Text, &t, TAG_DONE);
        if (t && strcmp((const char *)t, name) == 0) return i;
    }
    return -1;
}

static bool gitPageShowing(App *a)
{
    uint32 current = ~0U;
    if (a->gad[GID_BOTTOM_TABS]) IIntuition->GetAttr(CLICKTAB_Current, a->gad[GID_BOTTOM_TABS], &current);
    return (int)current == a->bottomPageIndex("Git");
}

void App::integrationsActivated(Document *d)
{
    if (!d || !editor) return;
    if (!d->lexerName.empty()) sciSetupSourceEditor(editor, d->lexerName.c_str(), fontSize);
    if (d->hex || d->readOnly) {
        S(editor, SCI_SETREADONLY, 1);
        if (d->hex) {
            S(editor, SCI_SETUNDOCOLLECTION, 0);
            S(editor, SCI_SETWRAPMODE, SC_WRAP_NONE);
        }
    }
    gitApplyMargins();
    gitUpdateMarkers();
    if (blameOn) gitUpdateBlame();
    if (d->hex) hexHighlightTwin();
}

void App::integrationsCaretMoved()
{
    if (!cur || !editor) return;
    if (cur->hex) { hexHighlightTwin(); return; }
    if (blameOn) gitBlameStatus((int)S(editor, SCI_LINEFROMPOSITION, S(editor, SCI_GETCURRENTPOS)));
}

void App::integrationsSaved(Document *d)
{
    if (d != cur) return;
    gitApplyMargins();
    gitUpdateMarkers();
    if (blameOn) gitUpdateBlame();
    if (gitPageShowing(this)) gitPanelWanted = true;
}

bool App::integrationsGadget(uint32_t gid, uint16_t code)
{
    if (gid == GID_BOTTOM_TABS) {
        if (gitPageShowing(this)) gitRefreshPanel();
        return false;
    }
    if (gid == GID_BUILDLOG) {
        uint32 ev = 0;
        IIntuition->GetAttr(LISTBROWSER_RelEvent, buildLog.obj, &ev);
        struct Node *n = buildLog.selected();
        intptr_t idx = (intptr_t)buildLog.userData(n) - 1;
        if (idx >= 0 && idx < (intptr_t)explanations.size() && !explanations[idx].empty()) {
            if (ev == LBRE_DOUBLECLICK) { explainRow((int)idx); return true; }
        }
        return false;
    }
    if (gid < GID_INTEG_FIRST || gid >= GID_INTEG_LAST) return false;
    switch (gid) {
    case GID_GIT_BRANCH: gitSwitchBranch(code); break;
    case GID_GIT_FILES: {
        uint32 ev = 0;
        IIntuition->GetAttr(LISTBROWSER_RelEvent, gitFiles.obj, &ev);
        if (ev == LBRE_DOUBLECLICK) gitPanelAction(GID_GIT_DIFF);
        break;
    }
    default: gitPanelAction((int)gid); break;
    }
    return true;
}

bool App::integrationsKey(struct IntuiMessage *im)
{
    return hexKey(im);
}

static bool isPictureName(const std::string &path)
{
    std::string e = extension(path);
    return e == "iff" || e == "ilbm" || e == "lbm" || e == "png" || e == "jpg" || e == "jpeg" || e == "gif" ||
           e == "bmp" || e == "pic" || e == "tga" || e == "webp";
}

static bool looksLikePicture(const std::string &d)
{
    if (d.size() >= 12 && d.compare(0, 4, "FORM") == 0 && (d.compare(8, 4, "ILBM") == 0 || d.compare(8, 4, "PBM ") == 0)) return true;
    if (d.size() >= 8 && d.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return true;
    if (d.size() >= 3 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xD8 && (unsigned char)d[2] == 0xFF) return true;
    if (d.size() >= 6 && (d.compare(0, 6, "GIF87a") == 0 || d.compare(0, 6, "GIF89a") == 0)) return true;
    return false;
}

bool App::integrationsOpen(const std::string &path, const std::string &data, Document *&doc)
{
    doc = nullptr;
    if (extension(path) == "info") { openIconEditor(path); return true; }
    if (isPictureName(path) || looksLikePicture(data)) { openImageViewer(path); return true; }
    if (hex::looksBinary(data)) { doc = openHex(path, data); return true; }
    return false;
}

// --- commands ---

bool App::integrationsCommand(uint32_t cmd)
{
    // a hex document has its own undo, find and go-to; most text editing doesn't apply
    if (cur && cur->hex) {
        switch (cmd) {
        case CMD_UNDO: hexUndoLast(); return true;
        case CMD_GOTO_LINE: hexGoto(); return true;
        case CMD_FIND: case CMD_REPLACE: hexFind(false); return true;
        case CMD_FIND_NEXT: case CMD_FIND_PREV: hexFind(true); return true;
        case CMD_REDO: case CMD_CUT: case CMD_PASTE: case CMD_COMMENT: case CMD_INDENT: case CMD_UNINDENT: case CMD_COMPLETE:
            setStatus("Not in the hex editor: type hex digits, or text in the right-hand column");
            return true;
        }
    }
    switch (cmd) {
    case CMD_OPEN_HEX: {
        for (auto &f : requestFiles("Open as Hex", project.path.empty() ? lastDir : project.dir)) {
            std::string data;
            if (!readFile(f, data)) { info("Can't read " + f); continue; }
            openHex(absolutePath(f), data);
        }
        return true;
    }
    case CMD_GIT_PANEL: {
        int p = bottomPageIndex("Git");
        if (p >= 0) showBottomPage(p);
        gitRefreshPanel();
        return true;
    }
    case CMD_GIT_BLAME:
        blameOn = !blameOn;
        gitApplyMargins();
        gitUpdateBlame();
        if (!blameOn) setStatus("Blame off");
        return true;
    case CMD_GIT_NEW_BRANCH: gitNewBranch(); return true;
    case CMD_DISTCC_SETTINGS: distccSettings(); return true;
    case CMD_EXPLAIN: {
        struct Node *n = buildLog.selected();
        intptr_t idx = (intptr_t)buildLog.userData(n) - 1;
        if (idx >= 0 && idx < (intptr_t)explanations.size() && !explanations[idx].empty()) explainRow((int)idx);
        else if (!lastCrashExplanation.empty()) info(lastCrashExplanation);
        else {
            // the first explained message of the last build
            for (size_t i = 0; i < explanations.size(); i++)
                if (!explanations[i].empty()) { explainRow((int)i); return true; }
            info("Nothing to explain: select an error or warning in the Build tab (or crash a program in the debugger).");
        }
        return true;
    }
    case CMD_MACROS_RESCAN: rebuildMenus(); setStatus(strprintf("%ld macro%s", (long)macroFiles.size(), macroFiles.size() == 1 ? "" : "s")); return true;
    case CMD_MACROS_FOLDER: {
        BPTR l = IDOS->Lock("PROGDIR:Rexx", SHARED_LOCK);
        if (!l) l = IDOS->CreateDir("PROGDIR:Rexx");
        char path[512] = "";
        if (l) { IDOS->NameFromLock(l, path, sizeof path); IDOS->UnLock(l); }
        if (!path[0] || !IWorkbench->OpenWorkbenchObject(path, TAG_DONE)) info(std::string("Macros live in ") + (path[0] ? path : "PROGDIR:Rexx"));
        return true;
    }
    case CMD_REXX_HELP:
        info("AmiForge's ARexx port is " + (rexxObj ? rexxPortName() : std::string("not open")) + ".\n"
             "Macros in PROGDIR:Rexx/ or the project's Rexx/ drawer appear in the Macros menu.\n\n" + rexx::helpText());
        return true;
    default:
        if (cmd >= CMD_MACRO_FIRST && cmd <= CMD_MACRO_LAST) {
            size_t i = cmd - CMD_MACRO_FIRST;
            if (i < macroFiles.size()) runMacro(macroFiles[i]);
            return true;
        }
        return false;
    }
}

// --- explanations ---

void App::integrationsBuildLine(const BuildLine &l, int index)
{
    if ((int)explanations.size() < index + 1) explanations.resize(index + 1);
    if (index == 0) explanations.assign(1, "");
    if (l.kind != MsgKind::Error && l.kind != MsgKind::Warning && l.kind != MsgKind::Output) return;
    std::string src;
    if (!l.file.empty() && l.line > 0) {
        static std::string lastFile, lastText;
        if (lastFile != l.file) { lastFile = l.file; lastText.clear(); readFile(l.file, lastText); }
        size_t p = 0;
        for (int k = 1; k < l.line && p != std::string::npos; k++) { p = lastText.find('\n', p); if (p != std::string::npos) p++; }
        if (p != std::string::npos) src = lastText.substr(p, lastText.find('\n', p) - p);
    }
    Explanation e = explainDiagnostic(l.text, src, [this](const std::string &f) {
        const DocEntry *d = findDoc(f);
        return d && d->func == f ? d->lib : std::string();
    });
    if (e.empty()) return;
    // one hint per explanation and build (four undefined IIntuitions need one)
    static std::vector<std::string> shown;
    if (index <= 1) shown.clear();
    std::string key = e.rule + "|" + e.title;
    if (std::find(shown.begin(), shown.end(), key) != shown.end()) {
        explanations[index] = e.title + "\n\n" + e.text;
        return;
    }
    shown.push_back(key);
    explanations[index] = e.title + "\n\n" + e.text;
    BuildLine hint;
    hint.kind = MsgKind::Note;
    hint.text = "    Explain: " + e.title + "  (double-click)";
    hint.file = l.file;
    hint.line = l.line;
    hint.col = l.col;
    diagnostics.push_back(hint);
    explanations.push_back(e.title + "\n\n" + e.text);
    struct Node *n = buildLog.add({ hint.text }, (void *)(intptr_t)diagnostics.size());
    if (n) buildLog.setPens(n, penNote);
}

void App::explainRow(int index)
{
    if (index < 0 || index >= (int)explanations.size() || explanations[index].empty()) return;
    std::string t = explanations[index];
    size_t nl = t.find("\n\n");
    std::string title = nl == std::string::npos ? "Explanation" : t.substr(0, nl);
    std::string body = nl == std::string::npos ? t : t.substr(nl + 2);
    if (index < (int)diagnostics.size()) body = diagnostics[index].text.compare(0, 13, "    Explain: ") == 0 && index > 0
                                                    ? diagnostics[index - 1].text + "\n\n" + body
                                                    : diagnostics[index].text + "\n\n" + body;
    ask(title.c_str(), body, "_OK");
}

void App::integrationsCrash(const dbg_event &ev)
{
    if (!dbg || ev.reason != DBG_STOP_EXCEPTION) return;
    CrashInfo c;
    c.trapType = ev.trap_type;
    c.pc = ev.pc;
    c.dar = ev.fault_addr;
    dbg_regs r;
    if (dbg_get_regs(dbg, &r)) {
        memcpy(c.gpr, r.gpr, sizeof c.gpr);
        c.sp = r.gpr[1];
        c.lr = r.lr;
        c.dsisr = r.dsisr;
        if (!c.dar) c.dar = r.dar;
    }
    dbg_insn in;
    if (dbg_disassemble(dbg, ev.pc, 1, &in) == 1) c.insn = in.text;
    dbg_frame fr[256];
    int n = dbg_stack(dbg, fr, 256);
    c.frames = n;
    int run = 1;
    for (int i = 1; i < n; i++) {
        if (fr[i].function && fr[i - 1].function && strcmp(fr[i].function, fr[i - 1].function) == 0) run++;
        else run = 1;
        c.deepestRepeat = std::max(c.deepestRepeat, run);
    }
    if (n > 0) {
        c.module = fr[0].module ? fr[0].module : "";
        c.function = fr[0].function ? fr[0].function : "";
        c.pcInProgram = c.module.empty() || c.module == "program";
        for (int i = 0; i < n; i++) {
            if (fr[i].file && (!fr[i].module || strcmp(fr[i].module, "program") == 0)) {
                if (i > 0 || !c.pcInProgram) {
                    c.callerFunction = fr[i].function ? fr[i].function : "";
                    c.callerLine = fr[i].line;
                }
                const char *src = dbg_resolve_source(dbg, fr[i].file);
                std::string text;
                if (src && readFile(src, text)) {
                    size_t p = 0;
                    for (int k = 1; k < fr[i].line && p != std::string::npos; k++) { p = text.find('\n', p); if (p != std::string::npos) p++; }
                    if (p != std::string::npos) c.sourceLine = text.substr(p, text.find('\n', p) - p);
                }
                break;
            }
        }
    }
    Explanation e = explainCrash(c);
    if (e.empty()) { lastCrashExplanation.clear(); return; }
    lastCrashExplanation = e.title + "\n\n" + e.text;
    debugLog("Explanation: " + e.title, penNote);
    size_t p = 0;
    while (p < e.text.size()) {
        size_t nl = e.text.find('\n', p);
        debugLog("    " + e.text.substr(p, nl == std::string::npos ? std::string::npos : nl - p), penNote);
        p = nl == std::string::npos ? e.text.size() : nl + 1;
    }
    setStatus(std::string(ev.text) + " - " + e.title + " (Debug > Explain Crash)");
}

// --- distcc ---

void App::distccSettings()
{
    enum { DG_ON = 1, DG_HOSTS, DG_DEFAULT, DG_OK, DG_CANCEL, DG_N };
    Object *g[DG_N] = {};
    char buf[512] = "";
    IDOS->GetVar("AmiForge/DistccHosts", buf, sizeof buf, GVF_GLOBAL_ONLY);
    std::string hosts = project.distccHosts.empty() ? trim(buf) : project.distccHosts;
    Class *layout = ILayout->LAYOUT_GetClass();
    static const char *help =
        "The compiler runs on other machines with distccd and a PPC AmigaOS cross-compiler\n"
        "(on the Mac or PC running QEMU: tools/distcc-server.sh). Preprocessing and linking\n"
        "stay here. Under QEMU's user networking the host is the guest's gateway address,\n"
        "e.g. 10.0.2.2 or 192.168.101.2. Hosts: name[:port][/jobs], separated by spaces.";
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"Distributed Builds (distcc)", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen, WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddImage, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)help, TAG_DONE),
            LAYOUT_AddChild, g[DG_ON] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr, GA_ID, DG_ON,
                GA_Text, (uint32)"Compile this project's C and C++ files with distcc", GA_Selected, project.distcc, TAG_DONE),
            LAYOUT_AddChild, g[DG_HOSTS] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr, GA_ID, DG_HOSTS,
                STRINGA_MaxChars, 400, STRINGA_TextVal, (uint32)hosts.c_str(), TAG_DONE),
            CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)"Hosts", TAG_DONE),
            LAYOUT_AddChild, g[DG_DEFAULT] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr, GA_ID, DG_DEFAULT,
                GA_Text, (uint32)"Use these hosts for other projects too (ENVARC:AmiForge/DistccHosts)", TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, g[DG_OK] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, DG_OK, GA_RelVerify, TRUE,
                    GA_Text, (uint32)"_Save", TAG_DONE),
                LAYOUT_AddChild, g[DG_CANCEL] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, DG_CANCEL,
                    GA_RelVerify, TRUE, GA_Text, (uint32)"_Cancel", TAG_DONE),
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);
    if (!wo) return;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) { IIntuition->DisposeObject(wo); return; }
    beginModal(wo);   // --- robust ---
    int done = 0;
    while (!done) {
        waitModal(wo);   // --- robust ---
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = -1;
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                if ((result & WMHI_GADGETMASK) == DG_OK) done = 1;
                if ((result & WMHI_GADGETMASK) == DG_CANCEL) done = -1;
            }
        }
        if (done == 1) {
            uint32 v = 0, on = 0, def = 0;
            IIntuition->GetAttr(STRINGA_TextVal, g[DG_HOSTS], &v);
            IIntuition->GetAttr(GA_Selected, g[DG_ON], &on);
            IIntuition->GetAttr(GA_Selected, g[DG_DEFAULT], &def);
            std::string h = trim(v ? (const char *)v : "");
            std::string err;
            for (auto &x : distcc::parseHosts(h))
                if (!distcc::validHost(x, err)) break;
            if (!err.empty()) { ask("Distributed Builds", "That host list has a problem: " + err, "_OK"); done = 0; continue; }
            if (on && h.empty()) { ask("Distributed Builds", "Give at least one host.", "_OK"); done = 0; continue; }
            if (on && !fileExists("SDK:C/distcc")) {
                ask("Distributed Builds", "SDK:C/distcc isn't installed (it comes with the SDK).", "_OK");
                done = 0;
                continue;
            }
            project.distcc = on;
            project.distccHosts = h;
            if (def) IDOS->SetVar("AmiForge/DistccHosts", h.c_str(), h.size(), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            saveProject();
        }
    }
    endModal();   // --- robust ---
    IIntuition->DisposeObject(wo);
    if (done == 1) setStatus(project.distcc ? "Distributed builds on: " + project.distccHosts : "Distributed builds off");
}

// For Builder: the distcc host list to use, or "" (distcc off).
std::string distccHostsFor(const Project &p)
{
    if (!p.distcc) return "";
    if (!p.distccHosts.empty()) return p.distccHosts;
    char buf[512] = "";
    IDOS->GetVar("AmiForge/DistccHosts", buf, sizeof buf, GVF_GLOBAL_ONLY);
    return trim(buf);
}

} // namespace af
