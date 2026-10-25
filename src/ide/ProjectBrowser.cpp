// --- projects ---
// The projects folder (ENVARC:AmiForge/ProjectsFolder, default PROGDIR:Examples) and
// Project > Browse Projects: a window listing the recent projects and every project in the
// folder (ProjectIndex.cpp finds and describes them; this is the dos and ReAction side).
#include "App.h"
#include "ProjectIndex.h"

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

void App::browseProjects()
{
    enum { PB_FOLDER = 1, PB_LIST, PB_SHOW, PB_OPEN, PB_CHANGE, PB_REFRESH, PB_CLOSE, PB_N };
    Object *g[PB_N] = {};
    static const char *titles[] = { "Project", "Description", "Drawer" };
    static const int weights[] = { 22, 53, 25 };
    ListView list(3);
    Class *layout = ILayout->LAYOUT_GetClass();
    auto button = [&](int id, const char *text) {
        return g[id] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, id, GA_RelVerify, TRUE, GA_Text, (uint32)text, TAG_DONE);
    };
    std::string folder = projectsFolder(), folderText;
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"Projects", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WA_InnerWidth, 720, WA_InnerHeight, 380,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, g[PB_FOLDER] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, PB_FOLDER, GA_ReadOnly, TRUE, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
                BUTTON_Justification, BCJ_LEFT, GA_Text, (uint32)"", TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, list.create(PB_LIST, titles, weights),
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
    g[PB_LIST] = list.obj;
    if (!wo) return;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        return;
    }
    list.win = w;

    std::vector<pidx::Row> rows;
    auto fill = [&]() {
        IIntuition->SetAttrs(wo, WA_BusyPointer, TRUE, TAG_DONE);
        folder = projectsFolder();
        folderText = "Projects folder: " + folder;
        IIntuition->SetGadgetAttrs((struct Gadget *)g[PB_FOLDER], w, nullptr, GA_Text, (uint32)folderText.c_str(), TAG_DONE);
        auto found = pidx::scan(folder, 2, listDir, readSmall);
        auto rec = pidx::recent(recentProjects, listDir, readSmall);
        rows = pidx::rows(rec, found, folder);
        list.clear();
        list.detach();
        int first = -1;
        for (size_t i = 0; i < rows.size(); i++) {
            const auto &r = rows[i];
            struct Node *n = list.add({ r.cols[0], r.cols[1], r.cols[2] }, (void *)(intptr_t)(i + 1), 0,
                                      r.heading ? LBFLG_READONLY : 0);
            if (r.heading) list.setPens(n, penNote);
            else if (first < 0) first = (int)i;
        }
        list.attach(first);
        IIntuition->SetAttrs(wo, WA_BusyPointer, FALSE, TAG_DONE);
    };
    fill();

    busyPointer(true);
    uint32 sig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, wo, &sig);
    std::string afp;
    bool done = false;
    while (!done) {
        IExec->Wait(sig);
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = true;
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
            case PB_CLOSE: done = true; break;
            case PB_REFRESH: fill(); break;
            case PB_SHOW: {
                uint32 on = 0;
                IIntuition->GetAttr(GA_Selected, g[PB_SHOW], &on);
                setEnv(showVar, on ? "1" : "0");
                break;
            }
            case PB_CHANGE: {
                std::string d = askDrawer(w, "Projects folder", folder);
                if (!d.empty()) {
                    setProjectsFolder(absolutePath(d));
                    fill();
                }
                break;
            }
            case PB_LIST:
                if (code != LBRE_DOUBLECLICK) break;
                /* fall through */
            case PB_OPEN: {
                struct Node *n = list.selected();
                size_t i = n ? (size_t)(intptr_t)list.userData(n) : 0;
                if (i == 0 || i > rows.size() || rows[i - 1].afp.empty()) break;
                afp = rows[i - 1].afp;
                done = true;
                break;
            }
            }
        }
    }
    list.win = nullptr;
    IIntuition->DisposeObject(wo);
    list.obj = nullptr;
    busyPointer(false);
    if (!afp.empty()) openProject(afp);
}

} // namespace af
// --- end projects ---
