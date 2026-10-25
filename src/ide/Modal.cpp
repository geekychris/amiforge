// --- robust ---
// Modal dialogs that can't get lost. A modal dialog runs its own loop; if the user clicked the
// main window (ClickToFront brings it in front) the dialog was hidden and the IDE looked hung:
// the main window ignored everything while its task waited for the hidden dialog.
//
// beginModal() puts a blocking requester on the main window (no gadget, menu or key input
// reaches it) and the busy pointer. waitModal() waits for the dialog and for the IDE's other
// windows: their input is drained without acting on it (window.class still handles refreshes
// inside WM_HANDLEINPUT, and a resize still lays the editor out), and a click, key or
// activation there brings the dialog back in front. It also brings the dialog back when any
// other window of ours (designer, icon editor, ...) became the active one.
//
// Build output and debugger events are not handled while a dialog is modal (pollBuild can
// start a program, a debug session or packaging when a build ends, which must not happen under
// a dialog that is editing the project). Their signals are not waited for here, so they stay
// pending and the main loop handles them as soon as the dialog closes. Stop Program's death
// messages are handled (they only update the status bar).
//
// WA_StayTop is not used: dialogs open asl and requester.class requesters of their own, which
// would open behind a stay-on-top dialog.
#include "App.h"
#include "Robust.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <classes/window.h>

namespace af {

bool drainBrowserWhileModal(App *app);   // ProjectBrowser.cpp: true if the user clicked or typed there

static_assert(WMHI_ACTIVE == (4u << 16) && WMHI_NEWSIZE == (5u << 16) && WMHI_GADGETUP == (2u << 16) &&
              WMHI_RAWKEY == (11u << 16) && WMHI_INTUITICK == (14u << 16) && WMHI_GADGETDOWN == (20u << 16),
              "Robust.cpp's copy of the WMHI_* classes");

static struct Window *windowOf(Object *wo)
{
    struct Window *w = nullptr;
    if (wo) IIntuition->GetAttr(WINDOW_Window, wo, (uint32 *)&w);
    return w;
}

static uint32 sigOf(Object *wo)
{
    uint32 s = 0;
    if (wo) IIntuition->GetAttr(WINDOW_SigMask, wo, &s);
    return s;
}

struct Window *browserWindow(App *app);   // ProjectBrowser.cpp

// A requester with no size and no gadgets on `w`: Intuition then sends it no gadget, menu or
// key input (the way asl's ASLFR_SleepWindow works)
static void block(App *app, struct Window *w)
{
    if (!w) return;
    struct Requester *r = (struct Requester *)IExec->AllocVecTags(sizeof(struct Requester), AVT_ClearWithValue, 0, TAG_DONE);
    if (!r) return;
    IIntuition->InitRequester(r);
    if (IIntuition->Request(r, w)) app->modalBlocks.push_back({ r, w });
    else IExec->FreeVec(r);
}

void App::beginModal(Object *dlg)
{
    modalStack.push_back(dlg);
    if (modalStack.size() > 1 || !win) return;
    busyPointer(true);
    block(this, win);
    if (findWinObj != dlg) block(this, findWin);
    if (docWinObj != dlg) block(this, docWin);
    block(this, browserWindow(this));
}

void App::endModal()
{
    if (!modalStack.empty()) modalStack.pop_back();
    if (!modalStack.empty()) return;
    for (auto &b : modalBlocks) {
        IIntuition->EndRequest(b.first, b.second);
        IExec->FreeVec(b.first);
    }
    modalBlocks.clear();
    busyPointer(false);
    modalPoke = false;
}

void App::raiseDialog(Object *dlg)
{
    struct Window *w = windowOf(dlg);
    if (!w) return;
    IIntuition->WindowToFront(w);
    IIntuition->ActivateWindow(w);
}

// The active window is one of ours (this task's) but not `dlg`.
static bool otherOfOursActive(struct Window *dlgWin)
{
    struct Task *me = IExec->FindTask(nullptr);
    uint32 lock = IIntuition->LockIBase(0);
    struct Window *aw = ((struct IntuitionBase *)IntuitionBase)->ActiveWindow;
    bool ours = aw && aw != dlgWin && aw->UserPort && aw->UserPort->mp_SigTask == me;
    IIntuition->UnlockIBase(lock);
    return ours;
}

uint32_t App::waitModal(Object *dlg)
{
    uint32 dlgSig = sigOf(dlg);
    struct Window *dlgWin = windowOf(dlg);
    for (;;) {
        // the IDE's other windows (not the dialog, nor a dialog further out: those run their
        // own loops further up the stack and get their input when we return to them)
        Object *others[4] = { winObj, findWinObj, docWinObj, nullptr };
        uint32 browserSig = browserSignal();
        uint32 otherSigs = 0;
        for (Object *o : others)
            if (o && o != dlg) otherSigs |= sigOf(o);
        otherSigs |= browserSig;
        uint32 got = IExec->Wait(dlgSig | otherSigs | runSignals());
        if (got & runSignals()) runHandle();
        bool raise = false;
        for (Object *o : others) {
            if (!o || o == dlg || !(got & sigOf(o))) continue;
            uint32 result;
            uint16 code;
            while ((result = IIntuition->IDoMethod(o, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
                switch (robust::modalReaction(result & WMHI_CLASSMASK)) {
                case robust::ModalReaction::Handle:
                    if (o == winObj) layoutEditor();
                    else if (o == docWinObj) layoutDocView();
                    break;
                case robust::ModalReaction::RaiseDialog:
                    raise = true;
                    break;
                case robust::ModalReaction::Ignore:
                    break;
                }
            }
        }
        if (browserSig && (got & browserSig)) {
            // the project browser: drained the same way (ProjectBrowser.cpp)
            if (drainBrowserWhileModal(this)) raise = true;
        }
        if (modalPoke) {
            raise = true;
            modalPoke = false;
        }
        if (!raise && dlgWin && otherOfOursActive(dlgWin)) raise = true;
        if (raise) raiseDialog(dlg);
        if (got & dlgSig) return got;
    }
}

} // namespace af
// --- end robust ---
