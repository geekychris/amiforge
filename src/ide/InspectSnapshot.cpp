// The OS side of the Intuition inspector (see InspectSnapshot.h).
#include "InspectSnapshot.h"

#include <map>
#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuitionbase.h>
#include <intuition/classes.h>
#include <intuition/screens.h>

namespace af {

// The decoders in InspectModel.cpp use the SDK's values: check they still are.
static_assert(IDCMP_CLOSEWINDOW == 0x200 && IDCMP_GADGETUP == 0x40 && IDCMP_INTUITICKS == 0x00400000 &&
              IDCMP_EXTENDEDMOUSE == 0x08000000, "IDCMP values");
static_assert(WFLG_SIMPLE_REFRESH == 0x40 && WFLG_ACTIVATE == 0x1000 && WFLG_RMBTRAP == 0x00010000 &&
              WFLG_HASICONIFY == 0x40000000, "WFLG values");
static_assert(GTYP_CUSTOMGADGET == 5 && GTYP_SYSGADGET == 0x8000 && GTYP_CLOSE == 0x80 && GTYP_GZZGADGET == 0x2000,
              "GTYP values");
static_assert(GFLG_DISABLED == 0x100 && GFLG_LABELSTRING == 0x1000 && GACT_RELVERIFY == 1 && GACT_LONGINT == 0x800,
              "GFLG/GACT values");

static std::string copyText(const char *s, size_t max = 80)
{
    if (!s) return "";
    size_t n = 0;
    while (n < max && s[n]) n++;
    return std::string(s, n);
}

static void readGadgets(struct Window *w, WindowInfo &wi)
{
    int count = 0;
    for (struct Gadget *g = w->FirstGadget; g && count < 200; g = g->NextGadget, count++) {
        GadgetInfo gi;
        gi.addr = (uint32_t)g;
        gi.id = g->GadgetID;
        gi.type = g->GadgetType;
        gi.flags = g->Flags;
        gi.activation = g->Activation;
        gi.left = g->LeftEdge; gi.top = g->TopEdge; gi.width = g->Width; gi.height = g->Height;
        if ((g->GadgetType & GTYP_GTYPEMASK) == GTYP_CUSTOMGADGET) {
            Class *cl = OCLASS(g);
            if (cl && cl->cl_ID) gi.className = copyText((const char *)cl->cl_ID, 40);
            else gi.className = "private class";
        } else if (g->GadgetText) {
            uint16 label = g->Flags & GFLG_LABELMASK;
            if (label == GFLG_LABELITEXT) gi.text = copyText((const char *)g->GadgetText->IText, 40);
            else if (label == GFLG_LABELSTRING) gi.text = copyText((const char *)g->GadgetText, 40);
        }
        wi.gadgets.push_back(gi);
    }
}

std::set<uint32_t> currentScreens()
{
    std::set<uint32_t> out;
    ULONG lock = IIntuition->LockIBase(0);
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    for (struct Screen *s = ib->FirstScreen; s; s = s->NextScreen) out.insert((uint32_t)s);
    IIntuition->UnlockIBase(lock);
    return out;
}

void takeInspectSnapshot(struct Task *task, const std::set<uint32_t> &screensAtStart, InspectSnapshot &inspectSnap)
{
    inspectSnap = InspectSnapshot();
    // public screen names first (that list has its own lock: don't nest it inside IBase's)
    std::map<uint32_t, std::string> pubNames;
    if (struct List *pl = IIntuition->LockPubScreenList()) {
        for (struct Node *n = pl->lh_Head; n->ln_Succ; n = n->ln_Succ) {
            struct PubScreenNode *psn = (struct PubScreenNode *)n;
            pubNames[(uint32_t)psn->psn_Screen] = copyText(n->ln_Name, 60);
        }
        IIntuition->UnlockPubScreenList();
    }
    if (task) {
        TaskInfo &ti = inspectSnap.task;
        ti.valid = true;
        ti.name = copyText(task->tc_Node.ln_Name, 60);
        ti.state = task->tc_State;
        ti.sigAlloc = task->tc_SigAlloc;
        ti.sigWait = task->tc_SigWait;
        ti.sigRecvd = task->tc_SigRecvd;
        ULONG lock = IIntuition->LockIBase(0);
        struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
        for (struct Screen *s = ib->FirstScreen; s; s = s->NextScreen) {
            bool any = false;
            for (struct Window *w = s->FirstWindow; w; w = w->NextWindow) {
                if (!w->UserPort || w->UserPort->mp_SigTask != task) continue;
                any = true;
                WindowInfo wi;
                wi.addr = (uint32_t)w;
                wi.title = w->Title ? copyText((const char *)w->Title) : "(untitled)";
                wi.left = w->LeftEdge; wi.top = w->TopEdge; wi.width = w->Width; wi.height = w->Height;
                wi.idcmp = w->IDCMPFlags;
                wi.flags = w->Flags;
                wi.sigBit = w->UserPort->mp_SigBit;
                wi.screen = (uint32_t)s;
                wi.active = ib->ActiveWindow == w;
                readGadgets(w, wi);
                inspectSnap.windows.push_back(wi);
            }
            bool fresh = !screensAtStart.count((uint32_t)s);
            if (any || fresh) {
                ScreenInfo si;
                si.addr = (uint32_t)s;
                si.title = copyText((const char *)s->Title);
                si.width = s->Width; si.height = s->Height;
                si.depth = s->RastPort.BitMap ? (int)IGraphics->GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH) : 0;
                si.front = s == ib->FirstScreen;
                si.newSinceStart = fresh;
                inspectSnap.screens.push_back(si);
            }
        }
        IIntuition->UnlockIBase(lock);
        for (auto &si : inspectSnap.screens) {
            auto it = pubNames.find(si.addr);
            if (it != pubNames.end()) si.pubName = it->second;
        }
    }
}

bool findWindowBox(uint32_t addr, int gadget, struct Screen **scrOut, struct Window **winOut, int box[4])
{
    struct Screen *scr = nullptr;
    struct Window *win = nullptr;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    ULONG lock = IIntuition->LockIBase(0);
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    for (struct Screen *s = ib->FirstScreen; s && !win; s = s->NextScreen)
        for (struct Window *w = s->FirstWindow; w; w = w->NextWindow)
            if ((uint32_t)w == addr) { win = w; scr = s; break; }
    if (win) {
        x0 = win->LeftEdge; y0 = win->TopEdge;
        x1 = x0 + win->Width - 1; y1 = y0 + win->Height - 1;
        if (gadget >= 0) {
            struct Gadget *g = win->FirstGadget;
            for (int i = 0; g && i < gadget; i++) g = g->NextGadget;
            if (g) {
                int l = g->LeftEdge, t = g->TopEdge, w = g->Width, h = g->Height;
                if (g->Flags & GFLG_RELRIGHT) l += win->Width - 1;
                if (g->Flags & GFLG_RELBOTTOM) t += win->Height - 1;
                if (g->Flags & GFLG_RELWIDTH) w += win->Width;
                if (g->Flags & GFLG_RELHEIGHT) h += win->Height;
                if ((g->GadgetType & GTYP_GZZGADGET) == 0 && (win->Flags & WFLG_GIMMEZEROZERO)) {
                    l += win->BorderLeft;
                    t += win->BorderTop;
                }
                x0 = win->LeftEdge + l; y0 = win->TopEdge + t;
                x1 = x0 + (w > 0 ? w : 1) - 1; y1 = y0 + (h > 0 ? h : 1) - 1;
            }
        }
    }
    IIntuition->UnlockIBase(lock);
    *scrOut = scr;
    *winOut = win;
    box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1;
    return win != nullptr;
}

} // namespace af
