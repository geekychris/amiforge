// The main window: ReAction layout, menus, toolbar, event loop and key bindings.
//
//  +-------------------------------------------------------------------------+
//  | New Open Save | Build Run | Debug Continue Over Into Out Stop | Find     |
//  +-----------+------------------------------------------+------------------+
//  | Project   | [main.c x][util.c x]                     | Locals|Watch|Regs |
//  |  main.c   |  editor (SciView in a placeholder)     |^ |                  |
//  |  util.c   |                                        |v |                  |
//  +-----------+------------------------------------------+------------------+
//  | Build | Find | Call Stack | Breakpoints | Memory | Disassembly | Log    |
//  +-------------------------------------------------------------------------+
//  | Ln 12, Col 5                                     | Stopped at main.c:12 |
//  +-------------------------------------------------------------------------+
#include "App.h"
#include "Commands.h"

#include <string.h>
#include <algorithm>
#include <stdio.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <proto/clicktab.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/string.h>
#include <proto/space.h>
#include <gadgets/space.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/clicktab.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <libraries/gadtools.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <devices/inputevent.h>

#include "Scintilla.h"
#include "Integrations.h"   // --- integrations ---

namespace af {

// ---------------------------------------------------------------------------------------
// The editor placeholder: a gadget that takes part in the layout but never claims a mouse
// hit, so clicks inside it reach the window (and the IDCMP hook) as plain MOUSEBUTTONS.
// ---------------------------------------------------------------------------------------
static Class *areaClass;
static struct Task *mainTask;

static uint32 areaDispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case GM_HITTEST:
        return 0;
    case GM_HELPTEST:
        return GMR_NOHELPHIT;
    case GM_DOMAIN: {
        struct gpDomain *d = (struct gpDomain *)msg;
        d->gpd_Domain.Left = d->gpd_Domain.Top = 0;
        switch (d->gpd_Which) {
        case GDOMAIN_MINIMUM: d->gpd_Domain.Width = 120; d->gpd_Domain.Height = 60; break;
        case GDOMAIN_NOMINAL: d->gpd_Domain.Width = 640; d->gpd_Domain.Height = 400; break;
        default:              d->gpd_Domain.Width = 16000; d->gpd_Domain.Height = 16000; break;
        }
        return 1;
    }
    case GM_RENDER:
        // Layout and refreshes on our own task: re-place and re-blit the editor now.
        if (IExec->FindTask(nullptr) == mainTask && gApp) {
            if (o == gApp->gad[GID_EDITOR]) gApp->layoutEditor();
            else gApp->layoutDocView();
        }
        return 0;
    default:
        return IIntuition->IDoSuperMethodA(cl, o, msg);
    }
}

static Class *makeAreaClass();

// An editor placeholder for other windows (the AutoDocs viewer).
Object *newEditorArea(uint32_t gid)
{
    if (!areaClass) areaClass = makeAreaClass();
    return areaClass ? IIntuition->NewObject(areaClass, nullptr, GA_ID, gid, TAG_DONE) : nullptr;
}

static Class *makeAreaClass()
{
    Class *cl = IIntuition->MakeClass(nullptr, "gadgetclass", nullptr, 0, 0);
    if (cl) cl->cl_Dispatcher.h_Entry = (HOOKFUNC)areaDispatch;
    return cl;
}

// ---------------------------------------------------------------------------------------
// Menus (gadtools NewMenu; nm_UserData carries the command)
// ---------------------------------------------------------------------------------------
#define ITEM(text, key, cmd) { NM_ITEM, (STRPTR)text, (STRPTR)key, 0, 0, (APTR)cmd }
#define SUB(text, key, cmd) { NM_SUB, (STRPTR)text, (STRPTR)key, 0, 0, (APTR)cmd }
#define BAR { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
#define TITLE(text) { NM_TITLE, (STRPTR)text, 0, 0, 0, 0 }
static struct NewMenu menus[] = {
    TITLE("Project"),
    ITEM("New Project...", 0, CMD_NEW_PROJECT),
    ITEM("Open Project...", "P", CMD_OPEN_PROJECT),
    ITEM("Open Recent", 0, 0),               // its submenu is filled in by rebuildMenus
    ITEM("Save Project", 0, CMD_SAVE_PROJECT),
    ITEM("Close Project", 0, CMD_CLOSE_PROJECT),
    BAR,
    ITEM("Add Files...", 0, CMD_ADD_FILES),
    ITEM("Remove File", 0, CMD_REMOVE_FILE),
    ITEM("Settings...", 0, CMD_PROJECT_SETTINGS),
    BAR,
    ITEM("About AmiForge...", "?", CMD_ABOUT),
    ITEM("Quit", "Q", CMD_QUIT),

    TITLE("File"),
    ITEM("New", "N", CMD_NEW_FILE),
    ITEM("Open...", "O", CMD_OPEN_FILE),
    ITEM("Open as Hex...", 0, CMD_OPEN_HEX),   // --- integrations ---
    ITEM("Save", "S", CMD_SAVE),
    ITEM("Save As...", "A", CMD_SAVE_AS),
    ITEM("Save All", 0, CMD_SAVE_ALL),
    BAR,
    ITEM("Close", "W", CMD_CLOSE),

    TITLE("Edit"),
    ITEM("Undo", "Z", CMD_UNDO),
    ITEM("Redo", "Y", CMD_REDO),
    BAR,
    ITEM("Cut", "X", CMD_CUT),
    ITEM("Copy", "C", CMD_COPY),
    ITEM("Paste", "V", CMD_PASTE),
    ITEM("Select All", 0, CMD_SELECT_ALL),
    BAR,
    ITEM("Find...", "F", CMD_FIND),
    ITEM("Find Next  (F3)", "G", CMD_FIND_NEXT),
    ITEM("Find Previous  (Shift F3)", 0, CMD_FIND_PREV),
    ITEM("Replace...", "R", CMD_REPLACE),
    ITEM("Find in Files...", "H", CMD_FIND_IN_FILES),
    ITEM("Go to Line...", "L", CMD_GOTO_LINE),
    ITEM("Open Include File  (Ctrl-Click)", "D", CMD_OPEN_INCLUDE),
    ITEM("Go to Definition  (F12, Ctrl-Click)", "J", CMD_GOTO_DEF),
    BAR,
    ITEM("Complete Word  (Ctrl Space)", 0, CMD_COMPLETE),
    ITEM("Comment/Uncomment Lines", "/", CMD_COMMENT),
    ITEM("Indent", "]", CMD_INDENT),
    ITEM("Unindent", "[", CMD_UNINDENT),

    TITLE("View"),
    ITEM("Bigger Text", "+", CMD_ZOOM_IN),
    ITEM("Smaller Text", "-", CMD_ZOOM_OUT),
    ITEM("Normal Text Size", "0", CMD_ZOOM_RESET),
    BAR,
    ITEM("Wrap Long Lines", 0, CMD_WRAP),
    ITEM("Show Whitespace", 0, CMD_WHITESPACE),
    ITEM("Fold All", 0, CMD_FOLD_ALL),
    ITEM("Unfold All", 0, CMD_UNFOLD_ALL),

    TITLE("Build"),
    ITEM("Compile File", "K", CMD_COMPILE),
    ITEM("Build  (F7)", "B", CMD_BUILD),
    ITEM("Rebuild All", 0, CMD_REBUILD),
    ITEM("Clean", 0, CMD_CLEAN),
    ITEM("Stop Build", 0, CMD_STOP_BUILD),
    BAR,
    ITEM("Run  (Ctrl F5)", "E", CMD_RUN),
    BAR,
    ITEM("Next Error  (F4)", 0, CMD_NEXT_ERROR),
    ITEM("Previous Error  (Shift F4)", 0, CMD_PREV_ERROR),
    ITEM("Explain Error", 0, CMD_EXPLAIN),                         // --- integrations ---
    BAR,
    ITEM("Distributed Builds (distcc)...", 0, CMD_DISTCC_SETTINGS),  // --- integrations ---

    TITLE("Debug"),
    ITEM("Start / Continue  (F5)", "D", CMD_DEBUG_START),
    ITEM("Stop Debugging  (Shift F5)", 0, CMD_DEBUG_STOP),
    ITEM("Pause", 0, CMD_DEBUG_PAUSE),
    BAR,
    ITEM("Step Over  (F10)", 0, CMD_STEP_OVER),
    ITEM("Step Into  (F11)", 0, CMD_STEP_INTO),
    ITEM("Step Out  (Shift F11)", 0, CMD_STEP_OUT),
    ITEM("Step Instruction  (F8)", 0, CMD_STEP_INSN),
    ITEM("Run to Cursor  (Ctrl F10)", 0, CMD_RUN_TO_CURSOR),
    BAR,
    ITEM("Toggle Breakpoint  (F9)", 0, CMD_TOGGLE_BP),
    ITEM("Delete All Breakpoints", 0, CMD_CLEAR_BPS),
    ITEM("Add Watch...", 0, CMD_ADD_WATCH),
    BAR,
    ITEM("Explain Crash", 0, CMD_EXPLAIN),   // --- integrations ---

    TITLE("Git"),
    ITEM("Git Panel", 0, CMD_GIT_PANEL),        // --- integrations ---
    ITEM("Blame (toggle)", 0, CMD_GIT_BLAME),
    ITEM("New Branch...", 0, CMD_GIT_NEW_BRANCH),
    BAR,
    ITEM("Status", 0, CMD_GIT_STATUS),
    ITEM("Diff", 0, CMD_GIT_DIFF),
    ITEM("Log", 0, CMD_GIT_LOG),
    BAR,
    ITEM("Commit...", 0, CMD_GIT_COMMIT),
    ITEM("Pull", 0, CMD_GIT_PULL),
    ITEM("Push", 0, CMD_GIT_PUSH),
    BAR,
    ITEM("Clone Repository...", 0, CMD_GIT_CLONE),
    ITEM("GitHub Setup...", 0, CMD_GIT_SETUP),

    TITLE("Help"),
    ITEM("AutoDoc for Word at Cursor  (F1)", 0, CMD_DOC_LOOKUP),
    ITEM("AutoDocs Browser...", 0, CMD_DOC_BROWSER),
    ITEM("SDK Documentation", 0, CMD_SDK_DOCS),
    ITEM("Keyboard Shortcuts...", 0, CMD_SHORTCUTS),
    BAR,
    ITEM("About AmiForge...", 0, CMD_ABOUT),
    { NM_END, 0, 0, 0, 0, 0 }
};

// The menus with the Open Recent submenu filled in from recentProjects.
void App::rebuildMenus()
{
    menuData.clear();
    menuLabels.clear();
    menuLabels.reserve(recentProjects.size());
    for (auto &p : recentProjects) menuLabels.push_back(fileName(p) + "  (" + dirName(p) + ")");
    for (struct NewMenu *m = menus; ; m++) {
        // --- integrations --- the Macros menu goes before Help
        if (m->nm_Type == NM_TITLE && strcmp((const char *)m->nm_Label, "Help") == 0) appendMacroMenu(this, menuData);
        menuData.push_back(*m);
        if (m->nm_Type == NM_END) break;
        if (m->nm_Type == NM_ITEM && m->nm_Label != NM_BARLABEL && strcmp((const char *)m->nm_Label, "Open Recent") == 0) {
            for (size_t i = 0; i < recentProjects.size() && i < 10; i++)
                menuData.push_back({ NM_SUB, (STRPTR)menuLabels[i].c_str(), 0, 0, 0, (APTR)(uint32)(CMD_RECENT_FIRST + i) });
            if (recentProjects.empty())
                menuData.push_back({ NM_SUB, (STRPTR)"(none yet)", 0, NM_ITEMDISABLED, 0, 0 });
            else {
                menuData.push_back({ NM_SUB, NM_BARLABEL, 0, 0, 0, 0 });
                menuData.push_back({ NM_SUB, (STRPTR)"Clear List", 0, 0, 0, (APTR)CMD_CLEAR_RECENT });
            }
        }
    }
    if (winObj) IIntuition->SetAttrs(winObj, WINDOW_NewMenu, (uint32)menuData.data(), TAG_DONE);
}

// ---------------------------------------------------------------------------------------

static uint32 idcmpHookFunc(struct Hook *, APTR, struct IntuiMessage *im)
{
    return gApp ? gApp->idcmpHook(im) : 0;
}
static struct Hook gIdcmpHook = { {nullptr, nullptr}, (HOOKFUNC)idcmpHookFunc, nullptr, nullptr };

static std::vector<uint32_t> pendingCommands;   // queued from inside the IDCMP hook

void App::queueCommand(uint32_t cmd) { pendingCommands.push_back(cmd); }

static Object *gap()
{
    return IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, SPACE_MinWidth, 12, TAG_DONE);
}

static Object *button(uint32_t gid, const char *text, const char *hint = nullptr)
{
    return IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
        GA_ID, gid, GA_RelVerify, TRUE, GA_Text, (uint32)text,
        hint ? GA_HintInfo : TAG_IGNORE, (uint32)hint, TAG_DONE);
}

static struct Node *tabNode(const char *text, int num)
{
    return IClickTab->AllocClickTabNode(TNA_Text, (uint32)text, TNA_Number, num, TAG_DONE);
}

static struct List *tabList(const char *const *names)
{
    struct List *l = new struct List;
    IExec->NewList(l);
    for (int i = 0; names[i]; i++) IExec->AddTail(l, tabNode(names[i], i));
    return l;
}

bool App::openMainWindow()
{
    mainTask = IExec->FindTask(nullptr);
    if (!areaClass) areaClass = makeAreaClass();
    if (!areaClass) return false;

    monoAttr = { (STRPTR)"DejaVu Sans Mono.font", 12, 0, 0 };
    monoFont = IDiskfont->OpenDiskFont(&monoAttr);
    if (!monoFont) {
        monoAttr = { (STRPTR)"topaz.font", 8, 0, 0 };
        monoFont = IDiskfont->OpenDiskFont(&monoAttr);
    }

    appPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);

    static const char *sideNames[] = { "Locals", "Watches", "Registers", nullptr };
    static const char *bottomNames[] = { "Build", "Find", "Call Stack", "Breakpoints", "Memory", "Disassembly", "Debug Log",
                                         "Git",   // --- integrations ---
                                         nullptr };
    sideLabels = tabList(sideNames);
    bottomLabels = tabList(bottomNames);
    tabLabels = new struct List;
    IExec->NewList(tabLabels);

    static const char *varTitles[] = { "Name", "Value", "Type" };
    static const int varWeights[] = { 35, 40, 25 };
    static const char *regTitles[] = { "Register", "Value" };
    static const int regWeights[] = { 35, 65 };
    static const char *stackTitles[] = { "#", "Function", "Location" };
    static const int stackWeights[] = { 6, 44, 50 };
    static const char *bpTitles[] = { "On", "Location", "Hits / condition" };
    static const int bpWeights[] = { 8, 60, 32 };
    static const char *findTitles[] = { "Location", "Text" };
    static const int findWeights[] = { 30, 70 };

    // The file tab row: an empty clicktab has no height, so reserve a row of the screen font.
    // The close gadget clicktab shows on the active tab: the system close image.
    int tabHeight = 22;
    if (struct ::Screen *scr = IIntuition->LockPubScreen(nullptr)) {
        tabHeight = scr->Font->ta_YSize + 10;
        drawInfo = IIntuition->GetScreenDrawInfo(scr);
        drawInfoScreen = scr;
        if (drawInfo)
            tabCloseImage = IIntuition->NewObject(nullptr, "sysiclass",
                SYSIA_Which, CLOSEIMAGE, SYSIA_DrawInfo, (uint32)drawInfo, SYSIA_Size, SYSISIZE_LOWRES, TAG_DONE);
        IIntuition->UnlockPubScreen(nullptr, scr);
    }
    if (tabCloseImage) {
        uint32 h = 0;
        IIntuition->GetAttr(IA_Height, tabCloseImage, &h);
        tabHeight = std::max(tabHeight + 4, (int)h + 10);
    }

    Class *page = ILayout->PAGE_GetClass();
    Class *layout = ILayout->LAYOUT_GetClass();
    Class *clicktab = IClickTab->CLICKTAB_GetClass();
    Class *scroller = IScroller->SCROLLER_GetClass();
    Class *string = IString->STRING_GetClass();

    Object *sidePages = IIntuition->NewObject(page, nullptr,
        PAGE_Add, locals.create(GID_LOCALS, varTitles, varWeights, true),
        PAGE_Add, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, watches.create(GID_WATCHES, varTitles, varWeights, true),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, gad[GID_WATCH_STR] = IIntuition->NewObject(string, nullptr,
                    GA_ID, GID_WATCH_STR, GA_RelVerify, TRUE, STRINGA_MaxChars, 255, TAG_DONE),
                LAYOUT_AddChild, gad[GID_WATCH_ADD] = button(GID_WATCH_ADD, "Add"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, gad[GID_WATCH_DEL] = button(GID_WATCH_DEL, "Remove"),
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        PAGE_Add, regs.create(GID_REGS, regTitles, regWeights, false, false, &monoAttr),
        TAG_DONE);
    gad[GID_SIDE_PAGES] = sidePages;

    Object *bottomPages = IIntuition->NewObject(page, nullptr,
        PAGE_Add, buildLog.create(GID_BUILDLOG, nullptr, nullptr, false, false, &monoAttr),
        PAGE_Add, findLog.create(GID_FINDLOG, findTitles, findWeights),
        PAGE_Add, stack.create(GID_STACK, stackTitles, stackWeights),
        PAGE_Add, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, bpList.create(GID_BPS, bpTitles, bpWeights, false, true),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, gad[GID_BP_DEL] = button(GID_BP_DEL, "Delete"),
                LAYOUT_AddChild, gad[GID_BP_DELALL] = button(GID_BP_DELALL, "Delete All"),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        PAGE_Add, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, gad[GID_MEM_ADDR] = IIntuition->NewObject(string, nullptr,
                GA_ID, GID_MEM_ADDR, GA_RelVerify, TRUE, STRINGA_MaxChars, 255, TAG_DONE),
            CHILD_WeightedHeight, 0,
            CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr,
                LABEL_Text, (uint32)"Address or expression", TAG_DONE),
            LAYOUT_AddChild, memView.create(GID_MEM_VIEW, nullptr, nullptr, false, false, &monoAttr),
            TAG_DONE),
        PAGE_Add, disasm.create(GID_DISASM, nullptr, nullptr, false, false, &monoAttr),
        PAGE_Add, debugLogView.create(GID_DEBUGLOG, nullptr, nullptr, false, false, &monoAttr),
        PAGE_Add, createGitPage(),   // --- integrations ---
        TAG_DONE);
    gad[GID_BOTTOM_PAGES] = bottomPages;

    winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"AmiForge",
        WA_ScreenTitle, (uint32)"AmiForge - AmigaOS 4 IDE",
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, TRUE, WA_ReportMouse, TRUE, WA_SmartRefresh, TRUE,
        WA_Left, 20, WA_Top, 30, WA_InnerWidth, 1180, WA_InnerHeight, 620,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_CLOSEWINDOW |
                  IDCMP_NEWSIZE | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS | IDCMP_IDCMPUPDATE | IDCMP_REFRESHWINDOW |
                  IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        WINDOW_NewMenu, (uint32)(rebuildMenus(), menuData.data()),
        WINDOW_AppPort, (uint32)appPort,
        WINDOW_IDCMPHook, (uint32)&gIdcmpHook,
        WINDOW_IDCMPHookBits, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                              IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS | IDCMP_IDCMPUPDATE,
        WINDOW_Layout, gad[GID_ROOT] = IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            // toolbar
            LAYOUT_AddChild, gad[GID_TOOLBAR] = IIntuition->NewObject(layout, nullptr,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, FALSE,
                LAYOUT_AddChild, gad[GID_TB_NEW] = button(GID_TB_NEW, "New", "New file (Amiga-N)"),
                LAYOUT_AddChild, gad[GID_TB_OPEN] = button(GID_TB_OPEN, "Open", "Open a file (Amiga-O)"),
                LAYOUT_AddChild, gad[GID_TB_SAVE] = button(GID_TB_SAVE, "Save", "Save (Amiga-S)"),
                LAYOUT_AddChild, gap(),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, gad[GID_TB_BUILD] = button(GID_TB_BUILD, "Build", "Build the project (F7)"),
                LAYOUT_AddChild, gad[GID_TB_RUN] = button(GID_TB_RUN, "Run", "Build and run (Ctrl-F5)"),
                LAYOUT_AddChild, gap(),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, gad[GID_TB_DEBUG] = button(GID_TB_DEBUG, "Debug", "Start debugging / continue (F5)"),
                LAYOUT_AddChild, gad[GID_TB_OVER] = button(GID_TB_OVER, "Step Over", "F10"),
                LAYOUT_AddChild, gad[GID_TB_INTO] = button(GID_TB_INTO, "Step Into", "F11"),
                LAYOUT_AddChild, gad[GID_TB_OUT] = button(GID_TB_OUT, "Step Out", "Shift-F11"),
                LAYOUT_AddChild, gad[GID_TB_STOP] = button(GID_TB_STOP, "Stop", "Stop debugging (Shift-F5)"),
                LAYOUT_AddChild, gap(),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, gad[GID_TB_FIND] = button(GID_TB_FIND, "Find", "Find / replace (Amiga-F)"),
                LAYOUT_AddChild, gad[GID_TB_DOCS] = button(GID_TB_DOCS, "Docs", "AutoDoc for the word at the cursor (F1)"),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            // everything that can be resized with weight bars
            // LAYOUT_WeightBar places a draggable bar at that point among the children
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    // project files
                    LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                        LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (uint32)"Project",
                        LAYOUT_AddChild, projectList.create(GID_PROJECT),
                        TAG_DONE),
                    CHILD_WeightedWidth, 16,
                    LAYOUT_WeightBar, TRUE,
                    // tabs + editor
                    LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild, gad[GID_TABS] = IIntuition->NewObject(clicktab, nullptr,
                            GA_ID, GID_TABS, GA_RelVerify, TRUE,
                            CLICKTAB_Labels, (uint32)tabLabels,
                            CLICKTAB_LabelTruncate, TRUE,
                            tabCloseImage ? CLICKTAB_CloseImage : TAG_IGNORE, (uint32)tabCloseImage,
                            CLICKTAB_ClosePlacement, PLACECLOSE_RIGHT,
                            TAG_DONE),
                        CHILD_WeightedHeight, 0,
                        CHILD_MinHeight, tabHeight,
                        LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild, gad[GID_EDITOR] = IIntuition->NewObject(areaClass, nullptr,
                                GA_ID, GID_EDITOR, TAG_DONE),
                            LAYOUT_AddChild, gad[GID_VSCROLL] = IIntuition->NewObject(scroller, nullptr,
                                GA_ID, GID_VSCROLL, GA_RelVerify, TRUE,
                                SCROLLER_Orientation, SORIENT_VERT, SCROLLER_Arrows, TRUE,
                                ICA_TARGET, ICTARGET_IDCMP, TAG_DONE),
                            CHILD_WeightedWidth, 0,
                            TAG_DONE),
                        LAYOUT_AddChild, gad[GID_HSCROLL] = IIntuition->NewObject(scroller, nullptr,
                            GA_ID, GID_HSCROLL, GA_RelVerify, TRUE,
                            SCROLLER_Orientation, SORIENT_HORIZ, SCROLLER_Arrows, TRUE,
                            ICA_TARGET, ICTARGET_IDCMP, TAG_DONE),
                        CHILD_WeightedHeight, 0,
                        TAG_DONE),
                    CHILD_WeightedWidth, 62,
                    LAYOUT_WeightBar, TRUE,
                    // debugger side panel
                    LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                        LAYOUT_AddChild, gad[GID_SIDE_TABS] = IIntuition->NewObject(clicktab, nullptr,
                            GA_ID, GID_SIDE_TABS, GA_RelVerify, TRUE,
                            CLICKTAB_Labels, (uint32)sideLabels,
                            CLICKTAB_PageGroup, (uint32)sidePages,
                            TAG_DONE),
                        TAG_DONE),
                    CHILD_WeightedWidth, 22,
                    TAG_DONE),
                CHILD_WeightedHeight, 72,
                LAYOUT_WeightBar, TRUE,
                LAYOUT_AddChild, gad[GID_BOTTOM_TABS] = IIntuition->NewObject(clicktab, nullptr,
                    GA_ID, GID_BOTTOM_TABS, GA_RelVerify, TRUE,
                    CLICKTAB_Labels, (uint32)bottomLabels,
                    CLICKTAB_PageGroup, (uint32)bottomPages,
                    TAG_DONE),
                CHILD_WeightedHeight, 28,
                TAG_DONE),
            // status bar
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, gad[GID_STATUS] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, GID_STATUS, GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (uint32)"",
                    BUTTON_Justification, BCJ_LEFT, TAG_DONE),
                CHILD_WeightedWidth, 30,
                LAYOUT_AddChild, gad[GID_STATUS2] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, GID_STATUS2, GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (uint32)"Ready",
                    BUTTON_Justification, BCJ_LEFT, TAG_DONE),
                CHILD_WeightedWidth, 70,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!winObj) return false;

    win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
    if (!win) return false;
    for (ListView *lv : { &projectList, &buildLog, &findLog, &locals, &watches, &regs, &stack,
                          &bpList, &memView, &disasm, &debugLogView })
        lv->win = win;

    editor = SciView::create(win, this);
    if (!editor) return false;
    layoutEditor();
    editor->setFocus(true);
    updateToolbar();
    return true;
}

void App::closeMainWindow()
{
    if (editor) {
        editor->detach();
        delete editor;
        editor = nullptr;
    }
    if (winObj) IIntuition->DisposeObject(winObj);   // closes the window and frees the gadgets
    winObj = nullptr;
    win = nullptr;
    if (tabCloseImage) IIntuition->DisposeObject(tabCloseImage);   // clicktab doesn't free it
    tabCloseImage = nullptr;
    if (drawInfo) IIntuition->FreeScreenDrawInfo(drawInfoScreen, drawInfo);
    drawInfo = nullptr;
    for (struct List *l : { tabLabels, sideLabels, bottomLabels }) {
        if (!l) continue;
        struct Node *n;
        while ((n = IExec->RemHead(l))) IClickTab->FreeClickTabNode(n);
        delete l;
    }
    tabLabels = sideLabels = bottomLabels = nullptr;
    if (monoFont) IGraphics->CloseFont(monoFont);
    monoFont = nullptr;
    if (appPort) IExec->FreeSysObject(ASOT_PORT, appPort);
    appPort = nullptr;
    if (areaClass) IIntuition->FreeClass(areaClass);
    areaClass = nullptr;
}

void App::layoutEditor(bool reblit)
{
    if (!editor || !win) return;
    struct Gadget *g = (struct Gadget *)gad[GID_EDITOR];
    // The clicktab above draws its bottom bar 2 pixels into our box.
    const int inset = 2;
    const int left = g->LeftEdge, top = g->TopEdge + inset, width = g->Width, height = g->Height - inset;
    int l, t, w, h;
    editor->getBounds(&l, &t, &w, &h);
    if (l == left && t == top && w == width && h == height) {
        if (reblit) editor->refresh();
        return;
    }
    editor->setBounds(left, top, width, height);
    // While the window is resized the editor can be blitted at an intermediate, larger size
    // after the gadgets around it were drawn: when it shrinks or moves, draw them again.
    // (Our placeholder's GM_RENDER comes back here with unchanged bounds and just re-blits.)
    if (width < w || height < h || left != l || top != t)
        IIntuition->RefreshGList((struct Gadget *)gad[GID_ROOT], win, nullptr, 1);
}

void App::setGadget(uint32_t gid, uint32_t tag, uint32_t value)
{
    if (gad[gid] && win)
        IIntuition->SetGadgetAttrs((struct Gadget *)gad[gid], win, nullptr, tag, value, TAG_DONE);
}

std::string App::getString(uint32_t gid)
{
    uint32 s = 0;
    if (gad[gid]) IIntuition->GetAttr(STRINGA_TextVal, gad[gid], &s);
    return s ? (const char *)s : "";
}

void App::setStatus(const std::string &s)
{
    statusLeft = s;
    static std::string shown;   // the gadget keeps a pointer to the text
    shown = s;
    setGadget(GID_STATUS2, GA_Text, (uint32)shown.c_str());
}

// Switch a clicktab's page. Setting the page that is already showing, or setting it without a
// refresh, left the page's gadget (the build log) ignoring clicks until it was re-rendered.
static void showPage(struct Window *win, Object *tabs, int p)
{
    if (!tabs || !win) return;
    uint32 current = ~0U;
    IIntuition->GetAttr(CLICKTAB_Current, tabs, &current);
    if ((int)current != p)
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)tabs, win, nullptr, CLICKTAB_Current, p, TAG_DONE);
}

void App::showBottomPage(int p) { showPage(win, gad[GID_BOTTOM_TABS], p); }
void App::showSidePage(int p) { showPage(win, gad[GID_SIDE_TABS], p); }

void App::updateTitle()
{
    static std::string title;
    title = "AmiForge";
    if (!project.path.empty()) title += " - " + project.name;
    if (cur) title += " - " + (cur->path.empty() ? cur->title : cur->path) + (cur->modified ? " *" : "");
    if (winObj) IIntuition->SetAttrs(winObj, WA_Title, (uint32)title.c_str(), TAG_DONE);
}

void App::updateToolbar()
{
    bool dbgOn = debugging();
    bool st = stopped();
    setGadget(GID_TB_DEBUG, GA_Text, (uint32)(st ? "Continue" : "Debug"));
    setGadget(GID_TB_DEBUG, GA_Disabled, dbgOn && !st);
    setGadget(GID_TB_OVER, GA_Disabled, !st);
    setGadget(GID_TB_INTO, GA_Disabled, !st);
    setGadget(GID_TB_OUT, GA_Disabled, !st);
    setGadget(GID_TB_STOP, GA_Disabled, !dbgOn);
    setGadget(GID_TB_BUILD, GA_Disabled, builder.busy() || dbgOn);
    setGadget(GID_TB_RUN, GA_Disabled, builder.busy() || dbgOn);
}

void App::busyPointer(bool busy)
{
    if (winObj) IIntuition->SetAttrs(winObj, WA_BusyPointer, busy, TAG_DONE);
}

// ---------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------

uint32_t App::idcmpHook(struct IntuiMessage *im)
{
    switch (im->Class) {
    case IDCMP_IDCMPUPDATE: {
        // live scroller drags
        struct TagItem *tags = (struct TagItem *)im->IAddress;
        uint32 id = IUtility->GetTagData(GA_ID, 0, tags);
        if (editor && (id == GID_VSCROLL || id == GID_HSCROLL)) {
            uint32 top = IUtility->GetTagData(SCROLLER_Top, ~0UL, tags);
            if (top != ~0UL) editor->scrollTo(id == GID_VSCROLL, (int)top);
        }
        return 0;
    }
    case IDCMP_RAWKEY: {
        if (im->Code & IECODE_UP_PREFIX) {
            if (editor && editorFocus) editor->handleMessage(im);
            return 0;
        }
        // IDE-wide keys first (function keys); then the editor
        uint16 code = im->Code, qual = im->Qualifier;
        bool fkey = (code >= 0x50 && code <= 0x59) || code == 0x4B || code == 0x6F || code == 0x5F;
        if (fkey) {
            handleKey(code, qual);
            return 0;
        }
        if (code == 0x40 && (qual & IEQUALIFIER_CONTROL) && editor && editorFocus) {   // Ctrl-Space
            pendingCommands.push_back(CMD_COMPLETE);
            return 0;
        }
        if (cur && editorFocus && integrationsKey(im)) return 0;   // --- integrations --- (hex editor)
        if (editor && editorFocus) editor->handleMessage(im);
        return 0;
    }
    case IDCMP_MOUSEBUTTONS: {
        int l = 0, t = 0, w = 0, h = 0;
        if (editor) editor->getBounds(&l, &t, &w, &h);
        bool inside = editor && im->MouseX >= l && im->MouseX < l + w && im->MouseY >= t && im->MouseY < t + h;
        // Ctrl-click: open the #include / go to the definition (not a Scintilla multi-selection)
        if (inside && im->Code == SELECTDOWN && (im->Qualifier & IEQUALIFIER_CONTROL)) {
            pendingClickPos = editor->send(SCI_POSITIONFROMPOINT, im->MouseX - l, im->MouseY - t);
            swallowSelectUp = true;
            pendingCommands.push_back(CMD_CTRL_CLICK);
            return 0;
        }
        if (im->Code == SELECTUP && swallowSelectUp) {
            swallowSelectUp = false;
            return 0;
        }
        if (editor && editor->handleMessage(im)) {
            if (!editorFocus) { editorFocus = true; editor->setFocus(true); }
        }
        return 0;
    }
    case IDCMP_MOUSEMOVE: {
        // over the editor the right button opens its context menu, elsewhere the menu bar
        if (editor && win) {
            int l, t, w, h;
            editor->getBounds(&l, &t, &w, &h);
            bool inside = im->MouseX >= l && im->MouseX < l + w && im->MouseY >= t && im->MouseY < t + h;
            if (inside) win->Flags |= WFLG_RMBTRAP;
            else win->Flags &= ~WFLG_RMBTRAP;
            editor->handleMessage(im);
        }
        return 0;
    }
    case IDCMP_INTUITICKS:
        if (editor) editor->tick();
        return 0;
    default:
        if (editor) editor->handleMessage(im);
        return 0;
    }
}

bool App::handleKey(uint16_t code, uint16_t qual)
{
    bool shift = qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT);
    bool ctrl = qual & IEQUALIFIER_CONTROL;
    uint32_t cmd = 0;
    switch (code) {
    case 0x50: cmd = CMD_DOC_LOOKUP; break;                                    // F1
    case 0x6F: cmd = CMD_GOTO_DEF; break;                                      // F12
    case 0x5F: cmd = CMD_DOC_LOOKUP; break;                                    // Help
    case 0x52: cmd = shift ? CMD_FIND_PREV : CMD_FIND_NEXT; break;           // F3
    case 0x53: cmd = shift ? CMD_PREV_ERROR : CMD_NEXT_ERROR; break;         // F4
    case 0x54:                                                                // F5
        cmd = ctrl ? CMD_RUN : shift ? CMD_DEBUG_STOP : CMD_DEBUG_START; break;
    case 0x55: cmd = CMD_RUN; break;                                          // F6
    case 0x56: cmd = CMD_BUILD; break;                                        // F7
    case 0x57: cmd = CMD_STEP_INSN; break;                                    // F8
    case 0x58: cmd = CMD_TOGGLE_BP; break;                                    // F9
    case 0x59: cmd = ctrl ? CMD_RUN_TO_CURSOR : CMD_STEP_OVER; break;         // F10
    case 0x4B: cmd = shift ? CMD_STEP_OUT : CMD_STEP_INTO; break;             // F11
    default: return false;
    }
    pendingCommands.push_back(cmd);
    return true;
}

void App::sciUnhandledKey(SciView *, uint16_t rawkey, uint16_t qual)
{
    handleKey(rawkey, qual);
}

void App::sciWantFocus(SciView *v)
{
    editorFocus = true;
    v->setFocus(true);
}

void App::sciScrollInfo(SciView *, bool vertical, int pos, int visible, int total)
{
    uint32_t gid = vertical ? GID_VSCROLL : GID_HSCROLL;
    if (gad[gid] && win)
        IIntuition->SetGadgetAttrs((struct Gadget *)gad[gid], win, nullptr,
            SCROLLER_Total, total, SCROLLER_Visible, visible, SCROLLER_Top, pos, TAG_DONE);
}

void App::eventLoop()
{
    uint32 winSig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, winObj, &winSig);
    while (!quit) {
        uint32 findSig = 0, docSig = 0;
        if (findWinObj) IIntuition->GetAttr(WINDOW_SigMask, findWinObj, &findSig);
        if (docWinObj) IIntuition->GetAttr(WINDOW_SigMask, docWinObj, &docSig);
        uint32 dbgSig = dbg ? dbg_signal_mask(dbg) : 0;
        uint32 integSig = integrationsSignals();   // --- integrations ---
        uint32 got = IExec->Wait(winSig | findSig | docSig | builder.signalMask() | dbgSig | integSig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) quit = true;

        if (got & winSig) {
            uint32 result;
            uint16 code;
            while ((result = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    pendingCommands.push_back(CMD_QUIT);
                    break;
                case WMHI_GADGETUP:
                    handleGadget(result & WMHI_GADGETMASK, code);
                    break;
                case WMHI_MENUPICK: {
                    struct Menu *strip = nullptr;
                    IIntuition->GetAttr(WINDOW_MenuStrip, winObj, (uint32 *)&strip);
                    uint16 m = code;
                    while (m != MENUNULL && strip) {
                        struct MenuItem *item = IIntuition->ItemAddress(strip, m);
                        if (!item) break;
                        pendingCommands.push_back((uint32)GTMENUITEM_USERDATA(item));
                        m = item->NextSelect;
                    }
                    break;
                }
                case WMHI_NEWSIZE:
                    layoutEditor();
                    break;
                case WMHI_ACTIVE:
                    if (editor && editorFocus) editor->setFocus(true);
                    break;
                case WMHI_INACTIVE:
                    if (editor) editor->setFocus(false);
                    break;
                }
            }
        }
        if (findWinObj && (got & findSig)) handleFindWindow();
        if (docWinObj && (got & docSig)) handleDocWindow();
        if (got & builder.signalMask()) pollBuild();
        if (dbg && (got & dbgSig)) pollDebugger();
        if (got & integSig) integrationsHandle(got);   // --- integrations ---

        while (!pendingCommands.empty()) {
            uint32_t c = pendingCommands.front();
            pendingCommands.erase(pendingCommands.begin());
            handleMenu(c);
        }
        integrationsIdle();   // --- integrations ---
        // Weight-bar drags lay the window out on input.device's task, where the placeholder's
        // GM_RENDER can't touch the editor: follow its box from here.
        layoutEditor(false);
        // SciView batches its repaints: let it paint now the loop is idle
        if (editor) editor->tick();
    }
}

void App::handleGadget(uint32_t gid, uint16_t code)
{
    if (integrationsGadget(gid, code)) return;   // --- integrations ---
    switch (gid) {
    case GID_TB_NEW: handleMenu(CMD_NEW_FILE); break;
    case GID_TB_OPEN: handleMenu(CMD_OPEN_FILE); break;
    case GID_TB_SAVE: handleMenu(CMD_SAVE); break;
    case GID_TB_BUILD: handleMenu(CMD_BUILD); break;
    case GID_TB_RUN: handleMenu(CMD_RUN); break;
    case GID_TB_DEBUG: handleMenu(CMD_DEBUG_START); break;
    case GID_TB_OVER: handleMenu(CMD_STEP_OVER); break;
    case GID_TB_INTO: handleMenu(CMD_STEP_INTO); break;
    case GID_TB_OUT: handleMenu(CMD_STEP_OUT); break;
    case GID_TB_STOP: handleMenu(CMD_DEBUG_STOP); break;
    case GID_TB_FIND: handleMenu(CMD_FIND); break;
    case GID_TB_DOCS: handleMenu(CMD_DOC_LOOKUP); break;
    case GID_VSCROLL:
    case GID_HSCROLL: {
        uint32 top = 0;
        IIntuition->GetAttr(SCROLLER_Top, gad[gid], &top);
        if (editor) editor->scrollTo(gid == GID_VSCROLL, (int)top);
        break;
    }
    case GID_TABS: {
        struct Node *closed = nullptr;
        IIntuition->GetAttr(CLICKTAB_NodeClosed, gad[GID_TABS], (uint32 *)&closed);
        Document *target = nullptr;
        for (auto &d : docs) {
            if (closed && d->tabNode == closed) { closeDocument(d.get()); return; }
        }
        uint32 n = 0;
        IIntuition->GetAttr(CLICKTAB_Current, gad[GID_TABS], &n);
        if (n < docs.size()) target = docs[n].get();
        if (target && target != cur) activate(target);
        editorFocus = true;
        if (editor) editor->setFocus(true);
        break;
    }
    case GID_PROJECT: {
        uint32 ev = 0;
        IIntuition->GetAttr(LISTBROWSER_RelEvent, gad[GID_PROJECT] ? gad[GID_PROJECT] : projectList.obj, &ev);
        struct Node *n = projectList.selected();
        void *ud = projectList.userData(n);
        if (ud) {
            int idx = (int)(intptr_t)ud - 1;
            if (idx >= 0 && idx < (int)project.files.size()) openFile(project.abs(project.files[idx]));
        }
        break;
    }
    case GID_BUILDLOG: {
        struct Node *n = buildLog.selected();
        intptr_t idx = (intptr_t)buildLog.userData(n) - 1;
        if (idx >= 0 && idx < (intptr_t)diagnostics.size() && !diagnostics[idx].file.empty())
            gotoLocation(diagnostics[idx].file, diagnostics[idx].line, diagnostics[idx].col);
        break;
    }
    case GID_FINDLOG: {
        struct Node *n = findLog.selected();
        intptr_t idx = (intptr_t)findLog.userData(n) - 1;
        if (idx >= 0 && idx < (intptr_t)findResults.size())
            gotoLocation(findResults[idx].file, findResults[idx].line, findResults[idx].col);
        break;
    }
    case GID_STACK: {
        struct Node *n = stack.selected();
        if (n) selectFrame((int)(intptr_t)stack.userData(n));
        break;
    }
    case GID_BPS: {
        uint32 ev = 0;
        IIntuition->GetAttr(LISTBROWSER_RelEvent, bpList.obj, &ev);
        struct Node *n = bpList.selected();
        int id = (int)(intptr_t)bpList.userData(n);
        if (ev == LBRE_CHECKED || ev == LBRE_UNCHECKED) {
            setBreakpointEnabled(id - 1, ev == LBRE_CHECKED);
        } else if (id > 0 && id <= (int)project.breakpoints.size()) {
            auto &b = project.breakpoints[id - 1];
            gotoLocation(project.abs(b.file), b.line);
        }
        break;
    }
    case GID_BP_DEL: {
        struct Node *n = bpList.selected();
        int id = (int)(intptr_t)bpList.userData(n);
        if (id > 0 && id <= (int)project.breakpoints.size()) {
            auto b = project.breakpoints[id - 1];
            Document *d = findDocument(project.abs(b.file));
            if (d) toggleBreakpoint(d, b.line);
            else {
                project.breakpoints.erase(project.breakpoints.begin() + (id - 1));
                refreshBreakpointList();
            }
        }
        break;
    }
    case GID_BP_DELALL: handleMenu(CMD_CLEAR_BPS); break;
    case GID_LOCALS:
    case GID_WATCHES: {
        ListView &lv = gid == GID_LOCALS ? locals : watches;
        uint32 ev = 0;
        IIntuition->GetAttr(LISTBROWSER_RelEvent, lv.obj, &ev);
        if (ev == LBRE_SHOWCHILDREN) {
            uint32 n = 0;
            IIntuition->GetAttr(LISTBROWSER_SelectedNode, lv.obj, &n);
            expandVar(lv, (struct Node *)n);
        }
        break;
    }
    case GID_WATCH_STR:
    case GID_WATCH_ADD: {
        std::string e = trim(getString(GID_WATCH_STR));
        if (!e.empty()) {
            addWatch(e);
            setGadget(GID_WATCH_STR, STRINGA_TextVal, (uint32)"");
        }
        break;
    }
    case GID_WATCH_DEL: removeWatch(); break;
    case GID_MEM_ADDR: refreshMemory(); break;
    default:
        break;
    }
    (void)code;
}

} // namespace af
