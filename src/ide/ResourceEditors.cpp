// Resource editors: Workbench icons (.info) and pictures.
//
//  - The icon editor shows the icon's image and lets you change its default tool, stack
//    size, tool types and position; Save writes the icon back with icon.library (the image
//    is kept as it is).
//  - The image viewer shows any picture the installed datatypes understand (IFF ILBM, PNG,
//    JPEG, GIF...) in a window of its own, with its size and depth in the title.
//
// Both are windows of their own next to the main window; the event loop waits on their
// signals (resourceSignals / resourceHandle).
#include "App.h"
#include "Integrations.h"

#include <string.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <proto/datatypes.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/checkbox.h>
#include <proto/space.h>
#include <proto/listbrowser.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/space.h>
#include <gadgets/integer.h>
#include <gadgets/checkbox.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>

namespace af {

enum {
    IG_IMAGE = 1, IG_TYPE, IG_TOOL, IG_STACK, IG_TTLIST, IG_TTSTR, IG_TTADD, IG_TTSET, IG_TTDEL, IG_X, IG_Y, IG_NOPOS,
    IG_SAVE, IG_CANCEL, IG_N
};

struct App::ResourceWindow {
    bool icon = false;
    std::string path;          // the .info file, or the picture
    // icon editor
    Object *winObj = nullptr;
    struct Window *win = nullptr;
    Object *g[IG_N] = {};
    struct DiskObject *dobj = nullptr;
    std::vector<std::string> toolTypes;
    ListView *ttList = nullptr;
    struct Hook renderHook = {};
    std::string typeText;
    // image viewer
    Object *dto = nullptr;
    struct Window *imgWin = nullptr;
    std::string title;
};

static App::ResourceWindow *renderTarget;   // the icon editor whose space gadget is drawing

static uint32 renderIcon(struct Hook *h, Object *space, struct gpRender *msg)
{
    App::ResourceWindow *rw = (App::ResourceWindow *)h->h_Data;
    if (!rw || !rw->dobj || !msg->gpr_RPort) return 0;
    struct IBox *box = nullptr;
    IIntuition->GetAttr(SPACE_AreaBox, space, (uint32 *)&box);
    if (!box) return 0;
    struct RastPort *rp = msg->gpr_RPort;
    uint32 w = rw->dobj->do_Gadget.Width, ht = rw->dobj->do_Gadget.Height;
    int x = box->Left + std::max(0, ((int)box->Width - (int)w) / 2);
    int y = box->Top + std::max(0, ((int)box->Height - (int)ht) / 2);
    struct DrawInfo *dri = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : nullptr;
    IIcon->DrawIconState(rp, rw->dobj, nullptr, x, y, IDS_NORMAL,
                         ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE,
                         dri ? ICONDRAWA_DrawInfo : TAG_IGNORE, (uint32)dri, TAG_DONE);
    (void)renderTarget;
    return 0;
}

static const char *iconType(int t)
{
    switch (t) {
    case WBDISK: return "Disk";
    case WBDRAWER: return "Drawer";
    case WBTOOL: return "Tool (a program)";
    case WBPROJECT: return "Project (a document)";
    case WBGARBAGE: return "Trashcan";
    case 6: return "Device";
    case 7: return "Kickstart";
    case 8: return "AppIcon";
    }
    return "Unknown";
}

static void fillToolTypes(App::ResourceWindow *rw)
{
    rw->ttList->detach();
    struct Node *n;
    while ((n = IExec->RemHead(rw->ttList->list))) IListBrowser->FreeListBrowserNode(n);
    for (size_t i = 0; i < rw->toolTypes.size(); i++) rw->ttList->add({ rw->toolTypes[i] }, (void *)(intptr_t)(i + 1));
    rw->ttList->attach();
}

void App::openIconEditor(const std::string &pathIn)
{
    std::string path = pathIn;
    if (extension(path) != "info") path += ".info";
    for (auto *w : resourceWindows)
        if (w->icon && sameFile(w->path, path)) { if (w->win) IIntuition->WindowToFront(w->win); return; }
    std::string base = path.substr(0, path.size() - 5);
    struct DiskObject *dobj = IIcon->GetIconTags(base.c_str(), ICONGETA_FailIfUnavailable, TRUE,
                                                 ICONGETA_Screen, (uint32)win->WScreen, TAG_DONE);
    if (!dobj) { info("Can't read the icon " + path); return; }
    auto *rw = new ResourceWindow;
    rw->icon = true;
    rw->path = path;
    rw->dobj = dobj;
    if (dobj->do_ToolTypes)
        for (STRPTR *t = dobj->do_ToolTypes; *t; t++) rw->toolTypes.push_back((const char *)*t);
    rw->renderHook.h_Entry = (HOOKFUNC)renderIcon;
    rw->renderHook.h_Data = rw;
    rw->typeText = iconType(dobj->do_Type);
    rw->ttList = new ListView(1);
    Class *layout = ILayout->LAYOUT_GetClass();
    auto label = [](const char *t) { return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE); };
    auto button = [&](int id, const char *t) {
        return rw->g[id] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (uint32)t, TAG_DONE);
    };
    auto integer = [&](int id, int32 v, int32 min, int32 max) {
        return rw->g[id] = IIntuition->NewObject(IInteger->INTEGER_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE,
                                                 INTEGER_Number, v, INTEGER_Minimum, min, INTEGER_Maximum, max, INTEGER_MaxChars, 10, TAG_DONE);
    };
    bool noPos = dobj->do_CurrentX == (LONG)NO_ICON_POSITION;
    int w = std::max<int>(dobj->do_Gadget.Width + 16, 80), h = std::max<int>(dobj->do_Gadget.Height + 16, 72);
    static std::string winTitle;
    winTitle = "Icon: " + fileName(path);
    rw->winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)winTitle.c_str(), WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, rw->g[IG_IMAGE] = IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr,
                    GA_ID, IG_IMAGE, SPACE_MinWidth, w, SPACE_MinHeight, h, SPACE_BevelStyle, BVS_FIELD,
                    SPACE_RenderHook, (uint32)&rw->renderHook, TAG_DONE),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_AddChild, rw->g[IG_TYPE] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, IG_TYPE,
                        GA_ReadOnly, TRUE, GA_Text, (uint32)rw->typeText.c_str(), BUTTON_Justification, BCJ_LEFT, TAG_DONE),
                    CHILD_Label, label("Type"),
                    LAYOUT_AddChild, rw->g[IG_TOOL] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr, GA_ID, IG_TOOL,
                        GA_TabCycle, TRUE, STRINGA_MaxChars, 255,
                        STRINGA_TextVal, (uint32)(dobj->do_DefaultTool ? (const char *)dobj->do_DefaultTool : ""), TAG_DONE),
                    CHILD_Label, label("Default tool"), CHILD_MinWidth, 260,
                    LAYOUT_AddChild, integer(IG_STACK, dobj->do_StackSize, 0, 0x7FFFFFFF),
                    CHILD_Label, label("Stack"),
                    LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                        LAYOUT_AddChild, integer(IG_X, noPos ? 0 : dobj->do_CurrentX, -32768, 32767),
                        CHILD_Label, label("X"),
                        LAYOUT_AddChild, integer(IG_Y, noPos ? 0 : dobj->do_CurrentY, -32768, 32767),
                        CHILD_Label, label("Y"),
                        LAYOUT_AddChild, rw->g[IG_NOPOS] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr,
                            GA_ID, IG_NOPOS, GA_RelVerify, TRUE, GA_Text, (uint32)"No position", GA_Selected, noPos, TAG_DONE),
                        TAG_DONE),
                    CHILD_Label, label("Position"),
                    TAG_DONE),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (uint32)"Tool types",
                LAYOUT_AddChild, rw->g[IG_TTLIST] = rw->ttList->create(IG_TTLIST),
                CHILD_MinHeight, 100,
                LAYOUT_AddChild, rw->g[IG_TTSTR] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr, GA_ID, IG_TTSTR,
                    GA_RelVerify, TRUE, STRINGA_MaxChars, 255, TAG_DONE),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild, button(IG_TTADD, "Add"),
                    LAYOUT_AddChild, button(IG_TTSET, "Change"),
                    LAYOUT_AddChild, button(IG_TTDEL, "Remove"),
                    TAG_DONE),
                CHILD_WeightedHeight, 0,
                TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, button(IG_SAVE, "_Save"),
                LAYOUT_AddChild, button(IG_CANCEL, "_Cancel"),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!rw->winObj || !(rw->win = (struct Window *)IIntuition->IDoMethod(rw->winObj, WM_OPEN))) {
        info("Can't open the icon editor window");
        closeResourceWindow(rw);
        return;
    }
    rw->ttList->win = rw->win;
    fillToolTypes(rw);
    resourceWindows.push_back(rw);
}

static bool saveIcon(App *a, App::ResourceWindow *rw)
{
    struct DiskObject *d = rw->dobj;
    auto str = [&](int id) {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, rw->g[id], &v);
        return std::string(v ? (const char *)v : "");
    };
    auto num = [&](int id) {
        uint32 v = 0;
        IIntuition->GetAttr(INTEGER_Number, rw->g[id], &v);
        return (int32)v;
    };
    std::string tool = trim(str(IG_TOOL));
    std::vector<STRPTR> tts;
    for (auto &t : rw->toolTypes) tts.push_back((STRPTR)t.c_str());
    tts.push_back(nullptr);
    uint32 noPos = 0;
    IIntuition->GetAttr(GA_Selected, rw->g[IG_NOPOS], &noPos);
    // our strings for the save, icon.library's own back afterwards (it frees them)
    STRPTR oldTool = d->do_DefaultTool;
    STRPTR *oldTT = d->do_ToolTypes;
    LONG oldStack = d->do_StackSize, oldX = d->do_CurrentX, oldY = d->do_CurrentY;
    d->do_DefaultTool = (STRPTR)tool.c_str();
    d->do_ToolTypes = tts.data();
    d->do_StackSize = num(IG_STACK);
    d->do_CurrentX = noPos ? (LONG)NO_ICON_POSITION : num(IG_X);
    d->do_CurrentY = noPos ? (LONG)NO_ICON_POSITION : num(IG_Y);
    std::string base = rw->path.substr(0, rw->path.size() - 5);
    int32 err = 0;
    BOOL ok = IIcon->PutIconTags(base.c_str(), d, ICONPUTA_NotifyWorkbench, TRUE, ICONA_ErrorCode, (uint32)&err, TAG_DONE);
    d->do_DefaultTool = oldTool;
    d->do_ToolTypes = oldTT;
    d->do_StackSize = oldStack;
    d->do_CurrentX = oldX;
    d->do_CurrentY = oldY;
    if (!ok) {
        char msg[128];
        IDOS->Fault(err, nullptr, msg, sizeof msg);
        a->info("Can't save " + rw->path + ": " + msg);
        return false;
    }
    a->setStatus("Saved " + rw->path);
    return true;
}

static bool handleIconWindow(App *a, App::ResourceWindow *rw)
{
    uint32 result;
    uint16 code;
    bool close = false;
    auto ttStr = [&]() {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, rw->g[IG_TTSTR], &v);
        return std::string(v ? (const char *)v : "");
    };
    while ((result = IIntuition->IDoMethod(rw->winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW: close = true; break;
        case WMHI_GADGETUP: {
            int id = result & WMHI_GADGETMASK;
            struct Node *sel = rw->ttList->selected();
            intptr_t idx = (intptr_t)rw->ttList->userData(sel) - 1;
            if (id == IG_SAVE) { if (saveIcon(a, rw)) close = true; }
            else if (id == IG_CANCEL) close = true;
            else if (id == IG_TTLIST && idx >= 0 && idx < (intptr_t)rw->toolTypes.size())
                IIntuition->SetGadgetAttrs((struct Gadget *)rw->g[IG_TTSTR], rw->win, nullptr,
                                           STRINGA_TextVal, (uint32)rw->toolTypes[idx].c_str(), TAG_DONE);
            else if (id == IG_TTADD || (id == IG_TTSTR && idx < 0)) {
                std::string t = ttStr();
                if (!t.empty()) { rw->toolTypes.push_back(t); fillToolTypes(rw); rw->ttList->select((int)rw->toolTypes.size() - 1); }
            } else if ((id == IG_TTSET || id == IG_TTSTR) && idx >= 0 && idx < (intptr_t)rw->toolTypes.size()) {
                rw->toolTypes[idx] = ttStr();
                fillToolTypes(rw);
                rw->ttList->select((int)idx);
            } else if (id == IG_TTDEL && idx >= 0 && idx < (intptr_t)rw->toolTypes.size()) {
                rw->toolTypes.erase(rw->toolTypes.begin() + idx);
                fillToolTypes(rw);
            }
            break;
        }
        }
    }
    return close;
}

// --- the image viewer ---

void App::openImageViewer(const std::string &path)
{
    struct Screen *scr = win->WScreen;
    Object *o = IDataTypes->NewDTObject(path.c_str(), DTA_GroupID, GID_PICTURE, PDTA_Remap, TRUE,
                                        PDTA_Screen, (uint32)scr, PDTA_DestMode, PMODE_V43, TAG_DONE);
    if (!o) {
        int32 err = IDOS->IoErr();
        char msg[128] = "";
        if (err) IDOS->Fault(err, nullptr, msg, sizeof msg);
        info("Can't show " + path + " as a picture" + (msg[0] ? std::string(": ") + msg : std::string(".")) +
             "\nIs there a datatype for it in DEVS:DataTypes? Open it as hex with File > Open as Hex.");
        return;
    }
    struct BitMapHeader *bmh = nullptr;
    struct DataType *dtn = nullptr;
    IDataTypes->GetDTAttrs(o, PDTA_BitMapHeader, (uint32)&bmh, DTA_DataType, (uint32)&dtn, TAG_DONE);
    int w = bmh ? bmh->bmh_Width : 320, h = bmh ? bmh->bmh_Height : 200, depth = bmh ? bmh->bmh_Depth : 0;
    auto *rw = new ResourceWindow;
    rw->path = path;
    rw->dto = o;
    std::string kind = dtn && dtn->dtn_Header && dtn->dtn_Header->dth_Name ? (const char *)dtn->dtn_Header->dth_Name : "picture";
    rw->title = strprintf("%s - %dx%d, %d bit%s (%s)", fileName(path).c_str(), w, h, depth, depth == 1 ? "" : "s", kind.c_str());
    int maxW = scr->Width - 40, maxH = scr->Height - scr->BarHeight - 40;
    rw->imgWin = IIntuition->OpenWindowTags(nullptr,
        WA_Title, (uint32)rw->title.c_str(), WA_PubScreen, (uint32)scr,
        WA_InnerWidth, std::min(std::max(w, 160), maxW), WA_InnerHeight, std::min(std::max(h, 40), maxH),
        WA_Left, 60, WA_Top, scr->BarHeight + 30,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_IDCMPUPDATE | IDCMP_RAWKEY, TAG_DONE);
    if (!rw->imgWin) {
        IDataTypes->DisposeDTObject(o);
        delete rw;
        info("Can't open a window for " + path);
        return;
    }
    struct Window *iw = rw->imgWin;
    IDataTypes->SetDTAttrs(o, nullptr, nullptr, GA_Left, iw->BorderLeft, GA_Top, iw->BorderTop,
                           GA_Width, iw->Width - iw->BorderLeft - iw->BorderRight,
                           GA_Height, iw->Height - iw->BorderTop - iw->BorderBottom, ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    IDataTypes->AddDTObject(iw, nullptr, o, -1);
    IDataTypes->RefreshDTObjects(o, iw, nullptr, nullptr);
    resourceWindows.push_back(rw);
    setStatus(rw->title);
}

static bool handleImageWindow(App::ResourceWindow *rw)
{
    struct IntuiMessage *im;
    bool close = false;
    while ((im = (struct IntuiMessage *)IExec->GetMsg(rw->imgWin->UserPort))) {
        if (im->Class == IDCMP_CLOSEWINDOW) close = true;
        else if (im->Class == IDCMP_RAWKEY && im->Code == 0x45) close = true;   // Esc
        else if (im->Class == IDCMP_IDCMPUPDATE) {
            struct TagItem *tags = (struct TagItem *)im->IAddress;
            if (IUtility->GetTagData(DTA_Sync, 0, tags)) IDataTypes->RefreshDTObjects(rw->dto, rw->imgWin, nullptr, nullptr);
        }
        IExec->ReplyMsg((struct Message *)im);
    }
    return close;
}

void App::closeResourceWindow(ResourceWindow *rw)
{
    auto it = std::find(resourceWindows.begin(), resourceWindows.end(), rw);
    if (it != resourceWindows.end()) resourceWindows.erase(it);
    if (rw->winObj) IIntuition->DisposeObject(rw->winObj);
    delete rw->ttList;
    if (rw->dobj) IIcon->FreeDiskObject(rw->dobj);
    if (rw->imgWin) {
        if (rw->dto) IDataTypes->RemoveDTObject(rw->imgWin, rw->dto);
        IIntuition->CloseWindow(rw->imgWin);
    }
    if (rw->dto) IDataTypes->DisposeDTObject(rw->dto);
    delete rw;
}

uint32_t resourceSignals(App *a)
{
    uint32_t m = 0;
    for (auto *rw : a->resourceWindows) {
        if (rw->winObj) {
            uint32 s = 0;
            IIntuition->GetAttr(WINDOW_SigMask, rw->winObj, &s);
            m |= s;
        }
        if (rw->imgWin) m |= 1UL << rw->imgWin->UserPort->mp_SigBit;
    }
    return m;
}

void resourceHandle(App *a, uint32_t got)
{
    std::vector<App::ResourceWindow *> done;
    for (auto *rw : a->resourceWindows) {
        bool close = false;
        if (rw->winObj) {
            uint32 s = 0;
            IIntuition->GetAttr(WINDOW_SigMask, rw->winObj, &s);
            if (got & s) close = handleIconWindow(a, rw);
        }
        if (rw->imgWin && (got & (1UL << rw->imgWin->UserPort->mp_SigBit))) close = handleImageWindow(rw);
        if (close) done.push_back(rw);
    }
    for (auto *rw : done) a->closeResourceWindow(rw);
}

void resourceCleanup(App *a)
{
    while (!a->resourceWindows.empty()) a->closeResourceWindow(a->resourceWindows.back());
}

} // namespace af
