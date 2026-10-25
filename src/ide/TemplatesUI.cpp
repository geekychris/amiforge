// Project > New Project from Template: the template chooser and creating the project
// (TemplateEngine.cpp works out what to write; this writes it, runs the template's commands,
// generates GUI code and opens the result).
#include "App.h"
#include "GuiModel.h"
#include "TemplateEngine.h"

#include <algorithm>
#include <string.h>
#include <time.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/listbrowser.h>
#include <proto/getfile.h>
#include <proto/space.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <gadgets/getfile.h>
#include <images/label.h>
#include <dos/dostags.h>

namespace af {

// A listbrowser reports a double-click in LISTBROWSER_RelEvent, not in WM_HANDLEINPUT's code.
static bool listDoubleClick(Object *o)
{
    uint32 ev = 0;
    IIntuition->GetAttr(LISTBROWSER_RelEvent, o, &ev);
    return ev == LBRE_DOUBLECLICK;
}

static std::string templatesDir() { return "PROGDIR:Templates"; }

static std::string envVar(const char *name)
{
    char buf[512];
    if (IDOS->GetVar(name, buf, sizeof buf, GVF_GLOBAL_ONLY) < 0) return "";
    return buf;
}

// Every template in PROGDIR:Templates, in their order.
static std::vector<tmpl::Def> loadTemplates(std::vector<std::string> &problems)
{
    std::vector<tmpl::Def> out;
    BPTR lock = IDOS->Lock(templatesDir().c_str(), SHARED_LOCK);
    if (!lock) return out;
    APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, TAG_END);
    if (ctx) {
        struct ExamineData *ed;
        while ((ed = IDOS->ExamineDir(ctx))) {
            if (!EXD_IS_DIRECTORY(ed)) continue;
            std::string dir = joinPath(templatesDir(), (const char *)ed->Name), text, err;
            if (!readFile(joinPath(dir, "template.def"), text)) continue;
            tmpl::Def d;
            if (!tmpl::parseDef(text, d, err)) {
                problems.push_back(std::string((const char *)ed->Name) + ": " + err);
                continue;
            }
            d.dir = dir;
            out.push_back(d);
        }
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->UnLock(lock);
    std::sort(out.begin(), out.end(), [](const tmpl::Def &a, const tmpl::Def &b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return out;
}

static bool makeDir(const std::string &path)
{
    BPTR l = IDOS->Lock(path.c_str(), SHARED_LOCK);
    if (!l) l = IDOS->CreateDir(path.c_str());
    if (!l) return false;
    IDOS->UnLock(l);
    return true;
}

static bool drawerEmpty(const std::string &path)
{
    BPTR lock = IDOS->Lock(path.c_str(), SHARED_LOCK);
    if (!lock) return true;
    bool empty = true;
    APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, TAG_END);
    if (ctx) {
        if (IDOS->ExamineDir(ctx)) empty = false;
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->UnLock(lock);
    return empty;
}

// The GUI code for a designer file: writes <name>_gui.c/.h next to it, keeping the user code
// already in the .c. cFile/hFile are their paths.
bool App::generateGuiFiles(const std::string &afg, std::string &cFile, std::string &hFile, std::string &err)
{
    std::string text;
    if (!readFile(afg, text)) { err = "Can't read " + afg; return false; }
    gui::Model m;
    if (!m.parse(text, err)) { err = fileName(afg) + ": " + err; return false; }
    auto problems = m.validate();
    if (!problems.empty()) { err = fileName(afg) + ": " + problems.front(); return false; }
    gui::Generated g = gui::generate(m);
    cFile = joinPath(dirName(afg), g.sourceName);
    hFile = joinPath(dirName(afg), g.headerName);
    // the code open in the editor: save it (its user code is kept), close it, reopen it after
    std::vector<std::string> reopen;
    for (const std::string &f : { cFile, hFile })
        if (Document *d = findDocument(f)) {
            if (d->modified && !saveDocument(d)) { err = "Save " + f + " first."; return false; }
            if (!closeDocument(d)) { err = "Close " + f + " first."; return false; }
            reopen.push_back(f);
        }
    std::string old;
    if (readFile(cFile, old)) g = gui::generate(m, old);
    bool ok = writeFile(hFile, g.header) && writeFile(cFile, g.source);
    for (auto &f : reopen) openFile(f);
    if (!ok) { err = "Can't write " + cFile; return false; }
    return true;
}

bool App::createFromTemplate(const std::string &templateDir, const std::string &name, const std::string &drawer,
                             std::string &afpOut, std::string &err, std::vector<std::string> *openOut)
{
    std::string defText;
    tmpl::Def def;
    if (!readFile(joinPath(templateDir, "template.def"), defText) || !tmpl::parseDef(defText, def, err)) {
        if (err.empty()) err = "Can't read the template " + templateDir;
        return false;
    }
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    std::string author = envVar("AmiForge/Author");
    if (author.empty()) {
        std::string n = envVar("AmiForge/GitName"), e = envVar("AmiForge/GitEmail");
        author = n.empty() ? "" : e.empty() ? n : n + " <" + e + ">";
    }
    tmpl::Vars vars = tmpl::makeVars(name, author, t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    tmpl::Plan plan = tmpl::plan(tmpl::instantiate(def, vars), vars, [&](const std::string &src, std::string &out) {
        return readFile(joinPath(templateDir, src), out);
    });
    if (!plan.errors.empty()) { err = plan.errors.front(); return false; }

    std::string dir = drawer;
    if (!makeDir(dir)) { err = "Can't make the drawer " + dir; return false; }
    for (auto &d : plan.dirs)
        if (!makeDir(joinPath(dir, d))) { err = "Can't make the drawer " + joinPath(dir, d); return false; }
    for (auto &f : plan.files)
        if (!writeFile(joinPath(dir, f.path), f.content)) { err = "Can't write " + joinPath(dir, f.path); return false; }

    // the template's commands, in the project's drawer
    for (auto &c : plan.commands) {
        setStatus("Running " + c + "...");
        std::string log = "T:AmiForge-template.log";
        BPTR out = IDOS->Open(log.c_str(), MODE_NEWFILE);
        BPTR in = IDOS->Open("NIL:", MODE_OLDFILE);
        BPTR dl = IDOS->Lock(dir.c_str(), SHARED_LOCK);
        BPTR old = dl ? IDOS->SetCurrentDir(dl) : 0;
        int32 rc = IDOS->SystemTags(c.c_str(), SYS_Input, in, SYS_Output, out, SYS_Error, out,
                                    NP_StackSize, 256 * 1024, TAG_END);
        if (dl) { IDOS->SetCurrentDir(old); IDOS->UnLock(dl); }
        if (in) IDOS->Close(in);
        if (out) IDOS->Close(out);
        if (rc != 0) {
            std::string text;
            readFile(log, text);
            err = "\"" + c + "\" failed" + (rc == -1 ? " to start" : " (return code " + std::to_string(rc) + ")") +
                  (text.empty() ? "" : ":\n" + text.substr(0, 600));
            return false;
        }
    }
    std::vector<std::string> files = plan.projectFiles;
    for (auto &f : files)
        if (!fileExists(joinPath(dir, f))) { err = "The template should have made " + f + ", but it isn't there."; return false; }
    for (auto &g : plan.guis) {
        std::string c, h;
        if (!generateGuiFiles(joinPath(dir, g), c, h, err)) return false;
        files.push_back(relativeTo(dir, c));
        files.push_back(relativeTo(dir, h));
        files.push_back(g);
    }

    // the project file: the template's settings over the defaults
    std::string afpText = "[project]\nname=" + name + "\noutput=" + vars["LOWER"] + "\n";
    for (auto &kv : plan.project) afpText += kv.first + "=" + kv.second + "\n";
    afpText += "[files]\n";
    for (auto &f : files) afpText += "file=" + f + "\n";
    std::string afp = joinPath(dir, name + ".afp");
    if (!writeFile(afp, afpText)) { err = "Can't write " + afp; return false; }
    Project p;
    if (!p.load(afp, err)) return false;
    // sources first, as New Project does
    std::stable_sort(p.files.begin(), p.files.end(), [](const std::string &a, const std::string &b) {
        return isSourceFile(a) && !isSourceFile(b);
    });
    if (!p.save(err)) return false;
    afpOut = afp;
    if (openOut)
        for (auto &o : plan.open) openOut->push_back(joinPath(dir, o));
    return true;
}

void App::newProjectFromTemplate()
{
    std::vector<std::string> problems;
    std::vector<tmpl::Def> defs = loadTemplates(problems);
    if (defs.empty()) {
        info("There are no templates in " + templatesDir() + ".\n\nThey come with AmiForge: copy the Templates "
             "drawer from its archive next to the AmiForge program." +
             (problems.empty() ? std::string() : "\n\n" + problems.front()));
        return;
    }
    enum { TG_LIST = 1, TG_NAME, TG_DRAWER, TG_AUTHOR, TG_OK, TG_CANCEL, TG_N };
    Object *g[TG_N] = {};
    static const char *titles[] = { "Template", "What it makes" };
    static const int weights[] = { 30, 70 };
    ListView list(2);
    Class *layout = ILayout->LAYOUT_GetClass();
    auto label = [](const char *t) {
        return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE);
    };
    std::string author = envVar("AmiForge/Author");
    if (author.empty()) {
        std::string n = envVar("AmiForge/GitName"), e = envVar("AmiForge/GitEmail");
        author = n.empty() ? "" : e.empty() ? n : n + " <" + e + ">";
    }
    std::string startDrawer = projectsFolder();   // --- projects ---
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"New Project from Template", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WA_InnerWidth, 640, WA_InnerHeight, 360,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, g[TG_LIST] = list.create(TG_LIST, titles, weights),
            LAYOUT_AddChild, g[TG_NAME] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr,
                GA_ID, TG_NAME, GA_RelVerify, TRUE, GA_TabCycle, TRUE, STRINGA_MaxChars, 24,
                STRINGA_TextVal, (uint32)"MyProgram", TAG_DONE),
            CHILD_Label, label("Project _name"), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, g[TG_DRAWER] = IIntuition->NewObject(IGetFile->GETFILE_GetClass(), nullptr,
                GA_ID, TG_DRAWER, GA_RelVerify, TRUE, GETFILE_TitleText, (uint32)"Make the project's drawer in...",
                GETFILE_DrawersOnly, TRUE, GETFILE_Drawer, (uint32)startDrawer.c_str(), TAG_DONE),
            CHILD_Label, label("_In drawer"), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, g[TG_AUTHOR] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr,
                GA_ID, TG_AUTHOR, GA_TabCycle, TRUE, STRINGA_MaxChars, 200, STRINGA_TextVal, (uint32)author.c_str(), TAG_DONE),
            CHILD_Label, label("_Author"), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, g[TG_OK] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, TG_OK, GA_RelVerify, TRUE, GA_Text, (uint32)"C_reate", TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, TAG_DONE),
                LAYOUT_AddChild, g[TG_CANCEL] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, TG_CANCEL, GA_RelVerify, TRUE, GA_Text, (uint32)"_Cancel", TAG_DONE),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!wo) return;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        return;
    }
    list.win = w;
    list.detach();
    for (size_t i = 0; i < defs.size(); i++) list.add({ defs[i].name, defs[i].description }, (void *)(intptr_t)i);
    list.attach(0);

    busyPointer(true);
    uint32 sig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, wo, &sig);
    auto getStr = [&](int id) {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, g[id], &v);
        return trim(v ? (const char *)v : "");
    };
    std::string afp;
    std::vector<std::string> toOpen;
    bool done = false;
    while (!done) {
        IExec->Wait(sig);
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = true;
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            uint32 id = result & WMHI_GADGETMASK;
            if (id == TG_CANCEL) done = true;
            else if (id == TG_DRAWER) IIntuition->IDoMethod(g[TG_DRAWER], GFILE_REQUEST, w);
            else if (id == TG_OK || (id == TG_LIST && listDoubleClick(list.obj))) {
                struct Node *n = list.selected();
                if (!n) continue;
                const tmpl::Def &d = defs[(intptr_t)list.userData(n)];
                std::string name = getStr(TG_NAME), why;
                if (!tmpl::validName(name, why)) { ask("New Project", why, "_OK"); continue; }
                uint32 dv = 0;
                IIntuition->GetAttr(GETFILE_Drawer, g[TG_DRAWER], &dv);
                std::string parent = dv ? (const char *)dv : "";
                if (parent.empty()) { ask("New Project", "Choose the drawer to put the project in.", "_OK"); continue; }
                std::string target = joinPath(parent, name);
                if (!drawerEmpty(target) &&
                    ask("New Project", target + " already has files in it. Put the project there anyway?\n"
                        "(Files with the same names are replaced.)", "_Yes|_No") != 1)
                    continue;
                std::string a = getStr(TG_AUTHOR);
                if (!a.empty()) {
                    IDOS->SetVar("AmiForge/Author", a.c_str(), -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
                }
                std::string err;
                IIntuition->SetAttrs(wo, WA_BusyPointer, TRUE, TAG_DONE);
                bool ok = createFromTemplate(d.dir, name, target, afp, err, &toOpen);
                IIntuition->SetAttrs(wo, WA_BusyPointer, FALSE, TAG_DONE);
                if (!ok) { ask("New Project", "The project couldn't be made:\n" + err, "_OK"); afp.clear(); continue; }
                done = true;
            }
        }
    }
    list.win = nullptr;
    IIntuition->DisposeObject(wo);
    list.obj = nullptr;
    busyPointer(false);
    if (afp.empty()) return;
    openProject(afp);
    for (auto &f : toOpen)
        if (fileExists(f)) openFile(f);
    lastDir = dirName(afp);
    setStatus("Created " + afp + " - F7 builds it, Ctrl-F5 runs it");
}

} // namespace af
