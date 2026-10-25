// --- projects ---
// The projects folder (ENVARC:AmiForge/ProjectsFolder, default PROGDIR:Examples) and
// Project > Browse Projects: a window listing the recent projects and every project in the
// folder (ProjectIndex.cpp finds and describes them; this is the dos and ReAction side).
#include "App.h"
#include "ProjectIndex.h"
#include "Robust.h"   // --- robust ---

#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <libraries/asl.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/listbrowser.h>

namespace af {

static const char *folderVar = "AmiForge/ProjectsFolder";
static const char *showVar = "AmiForge/ShowBrowser";
static const char *defaultFolder = "PROGDIR:Examples";

static std::string getEnv(const char *name)
{
    char buf[1024];
    if (IDOS->GetVar(name, buf, sizeof buf, GVF_GLOBAL_ONLY) < 0) return "";
    return trim(buf);
}

// ENV: now and ENVARC: for the next boot
static void setEnv(const char *name, const std::string &value)
{
    IDOS->SetVar(name, value.c_str(), value.size(), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

// Lock without "insert volume" requesters (the folder may be on a disk that isn't there).
static BPTR quietLock(const std::string &path)
{
    APTR old = IDOS->SetProcWindow((APTR)-1);
    BPTR l = IDOS->Lock(path.c_str(), SHARED_LOCK);
    IDOS->SetProcWindow(old);
    return l;
}

std::string App::projectsFolder()
{
    std::string f = getEnv(folderVar);
    if (f.empty()) f = defaultFolder;
    // PROGDIR: only means something to us: show and pass on the real path
    BPTR l = quietLock(f);
    if (l) {
        char buf[1024];
        if (IDOS->NameFromLock(l, buf, sizeof buf)) f = buf;
        IDOS->UnLock(l);
    }
    return f;
}

void App::setProjectsFolder(const std::string &dir)
{
    setEnv(folderVar, dir);
    setStatus("Projects folder: " + dir);
}

bool App::showBrowserAtStartup()
{
    return pidx::parseOnOff(getEnv(showVar), true);
}

std::string App::openProjectStart()
{
    return pidx::openStart(projectsFolder(), project.path.empty() ? "" : project.dir, lastDir);
}

static std::string askDrawer(struct Window *w, const char *title, const std::string &initial)
{
    struct FileRequester *fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (uint32)title,
        ASLFR_Window, (uint32)w, ASLFR_SleepWindow, TRUE,
        ASLFR_DrawersOnly, TRUE,
        ASLFR_InitialDrawer, (uint32)initial.c_str(),
        TAG_END);
    if (!fr) return "";
    std::string result;
    if (IAsl->AslRequestTags(fr, TAG_END) && fr->fr_Drawer && fr->fr_Drawer[0]) result = fr->fr_Drawer;
    IAsl->FreeAslRequest(fr);
    return result;
}

void App::chooseProjectsFolder()
{
    std::string d = askDrawer(win, "Projects folder (Project > Browse Projects lists it)", projectsFolder());
    if (!d.empty()) setProjectsFolder(absolutePath(d));
}

// The file system for the model
static bool listDir(const std::string &path, std::vector<pidx::DirItem> &out)
{
    BPTR lock = quietLock(path);
    if (!lock) return false;
    APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, TAG_END);
    if (ctx) {
        struct ExamineData *ed;
        while ((ed = IDOS->ExamineDir(ctx))) {
            if (!EXD_IS_FILE(ed) && !EXD_IS_DIRECTORY(ed)) continue;   // links, sockets
            out.push_back({ (const char *)ed->Name, (bool)EXD_IS_DIRECTORY(ed) });
        }
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->UnLock(lock);
    return ctx != nullptr;
}

static bool readSmall(const std::string &path, std::string &out)
{
    BPTR l = quietLock(path);
    if (!l) return false;
    IDOS->UnLock(l);
    if (!readFile(path, out)) return false;
    if (out.size() > 16384) out.resize(16384);
    return true;
}

// --- robust --- the browser is a normal window (it used to be modal): it stays open next to
// the main window and is handled by the event loop (browserSignal/handleBrowser), so clicking
// the main window can't leave the IDE waiting for a hidden dialog.
enum { PB_FOLDER = 1, PB_LIST, PB_SHOW, PB_OPEN, PB_CHANGE, PB_REFRESH, PB_CLOSE, PB_N };

struct BrowserState {
    Object *wo = nullptr;
    struct Window *w = nullptr;
    Object *g[PB_N] = {};
    ListView list{3};
    std::string folder, folderText;
    std::vector<pidx::Row> rows;
};

static void fillBrowser(App *app, BrowserState *b)
{
    IIntuition->SetAttrs(b->wo, WA_BusyPointer, TRUE, TAG_DONE);
    b->folder = app->projectsFolder();
    b->folderText = "Projects folder: " + b->folder;
    IIntuition->SetGadgetAttrs((struct Gadget *)b->g[PB_FOLDER], b->w, nullptr, GA_Text, (uint32)b->folderText.c_str(), TAG_DONE);
    auto found = pidx::scan(b->folder, 2, listDir, readSmall);
    auto rec = pidx::recent(app->recentProjects, listDir, readSmall);
    b->rows = pidx::rows(rec, found, b->folder);
    b->list.clear();
    b->list.detach();
    int first = -1;
    for (size_t i = 0; i < b->rows.size(); i++) {
        const auto &r = b->rows[i];
        struct Node *n = b->list.add({ r.cols[0], r.cols[1], r.cols[2] }, (void *)(intptr_t)(i + 1), 0,
                                     r.heading ? LBFLG_READONLY : 0);
        if (r.heading) b->list.setPens(n, app->penNote);
        else if (first < 0) first = (int)i;
    }
    b->list.attach(first);
    IIntuition->SetAttrs(b->wo, WA_BusyPointer, FALSE, TAG_DONE);
}

void App::browseProjects()
{
    if (browser) {   // already open: bring it to the front, with a fresh list
        IIntuition->WindowToFront(browser->w);
        IIntuition->ActivateWindow(browser->w);
        fillBrowser(this, browser);
        return;
    }
    static const char *titles[] = { "Project", "Description", "Drawer" };
    static const int weights[] = { 16, 60, 24 };
    BrowserState *b = new BrowserState;
    Object **g = b->g;
    Class *layout = ILayout->LAYOUT_GetClass();
    auto button = [&](int id, const char *text) {
        return g[id] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, id, GA_RelVerify, TRUE, GA_Text, (uint32)text, TAG_DONE);
    };
    b->wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"Projects", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WA_InnerWidth, 860, WA_InnerHeight, 400,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, g[PB_FOLDER] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, PB_FOLDER, GA_ReadOnly, TRUE, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
                BUTTON_Justification, BCJ_LEFT, GA_Text, (uint32)"", TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, b->list.create(PB_LIST, titles, weights),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, g[PB_SHOW] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr,
                    GA_ID, PB_SHOW, GA_RelVerify, TRUE, GA_Text, (uint32)"Show at _startup",
                    GA_Selected, showBrowserAtStartup(), TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild, button(PB_OPEN, "_Open"),
                    LAYOUT_AddChild, button(PB_CHANGE, "C_hange Folder..."),
                    LAYOUT_AddChild, button(PB_REFRESH, "_Refresh"),
                    LAYOUT_AddChild, button(PB_CLOSE, "_Close"),
                    TAG_DONE),
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    g[PB_LIST] = b->list.obj;
    // columns by weight: fitted to the text, long descriptions would push Drawer out of sight
    if (b->list.obj) IIntuition->SetAttrs(b->list.obj, LISTBROWSER_AutoFit, FALSE, TAG_DONE);
    if (b->wo) b->w = (struct Window *)IIntuition->IDoMethod(b->wo, WM_OPEN);
    if (!b->w) {
        if (b->wo) IIntuition->DisposeObject(b->wo);
        b->list.obj = nullptr;
        delete b;
        return;
    }
    b->list.win = b->w;
    browser = b;
    fillBrowser(this, b);
}

struct Window *browserWindow(App *app) { return app->browser ? app->browser->w : nullptr; }

uint32_t App::browserSignal()
{
    uint32 s = 0;
    if (browser) IIntuition->GetAttr(WINDOW_SigMask, browser->wo, &s);
    return s;
}

void App::closeBrowser()
{
    if (!browser) return;
    browser->list.win = nullptr;
    IIntuition->DisposeObject(browser->wo);
    browser->list.obj = nullptr;
    delete browser;
    browser = nullptr;
}

void App::handleBrowser()
{
    if (!browser) return;
    BrowserState *b = browser;
    std::string afp;
    bool close = false;
    uint32 result;
    uint16 code;
    while ((result = IIntuition->IDoMethod(b->wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) close = true;
        if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
        switch (result & WMHI_GADGETMASK) {
        case PB_CLOSE: close = true; break;
        case PB_REFRESH: fillBrowser(this, b); break;
        case PB_SHOW: {
            uint32 on = 0;
            IIntuition->GetAttr(GA_Selected, b->g[PB_SHOW], &on);
            setEnv(showVar, on ? "1" : "0");
            break;
        }
        case PB_CHANGE: {
            std::string d = askDrawer(b->w, "Projects folder", b->folder);
            if (!d.empty()) {
                setProjectsFolder(absolutePath(d));
                fillBrowser(this, b);
            }
            break;
        }
        case PB_LIST: {
            uint32 ev = 0;
            IIntuition->GetAttr(LISTBROWSER_RelEvent, b->list.obj, &ev);
            if (ev != LBRE_DOUBLECLICK) break;
        }
            /* fall through */
        case PB_OPEN: {
            struct Node *n = b->list.selected();
            size_t i = n ? (size_t)(intptr_t)b->list.userData(n) : 0;
            if (i == 0 || i > b->rows.size() || b->rows[i - 1].afp.empty()) break;
            afp = b->rows[i - 1].afp;
            close = true;
            break;
        }
        }
    }
    if (!close) return;
    closeBrowser();   // b is gone from here
    if (!afp.empty()) {
        if (win) IIntuition->ActivateWindow(win);
        openProject(afp);
    }
}

// While a modal dialog is open (Modal.cpp): drain the browser's input without acting on it.
// True if the user clicked or typed there (the dialog comes back in front).
bool drainBrowserWhileModal(App *app)
{
    if (!app->browser) return false;
    bool poke = false;
    uint32 result;
    uint16 code;
    while ((result = IIntuition->IDoMethod(app->browser->wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
        if (robust::modalReaction(result & WMHI_CLASSMASK) == robust::ModalReaction::RaiseDialog) poke = true;
    return poke;
}
// --- end robust ---

} // namespace af
// --- end projects ---
