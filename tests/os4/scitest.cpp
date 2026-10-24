// scitest — a SciView in a ReAction window on Workbench, wired up the way the IDE does it:
// window.class feeds the view through WINDOW_IDCMPHook, and a read-only space.gadget holds
// the view's place in the layout (GA_ReadOnly, so clicks reach the window as MOUSEBUTTONS).
//
//   scitest [file]      edit a file (default: a built-in C sample with every syntax kind)
//
// The status line shows the caret position, the scroll info the view reports and the last
// event. Keys: Amiga-C/X/V/Z/A copy, cut, paste, undo, select all; Amiga-Q quits;
// F1 autocomplete list, F2 calltip, F3 cycles the debugger markers on the caret line.
// A click in the symbol margin toggles a breakpoint. Ctrl-C (break) quits too.
#include <cstdio>
#include <cstring>
#include <string>

#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "Scintilla.h"
#include "sci/SciView.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/button.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/space.h>
#include <gadgets/button.h>
#include <dos/dos.h>

using namespace af;

namespace {

const char sampleC[] =
    "/* scitest.c -- every kind of C syntax for the highlighter.\n"
    " * Block comments, line comments, doc comments, preprocessor lines,\n"
    " * keywords, types, strings, characters, numbers and operators.\n"
    " */\n"
    "#include <stdio.h>\n"
    "#include <proto/exec.h>\n"
    "#define COUNT 16    // a line comment after a define\n"
    "\n"
    "/** A doc comment for the structure. */\n"
    "struct Point {\n"
    "    int32 x, y;\n"
    "    float scale;\n"
    "};\n"
    "\n"
    "static const char *names[] = { \"alpha\", \"beta\", \"gamma\\n\" };\n"
    "\n"
    "static uint32 checksum(const UBYTE *data, ULONG len)\n"
    "{\n"
    "    uint32 sum = 0xDEADBEEF;\n"
    "    for (ULONG i = 0; i < len; i++) {\n"
    "        sum = (sum << 5) ^ (sum >> 27) ^ data[i];\n"
    "        if (data[i] == '\\0' && (i & 3) != 0)\n"
    "            break;\n"
    "    }\n"
    "    return sum + 42 * 3.5e2f;\n"
    "}\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    struct Point p = { 10, -20, 1.5f };\n"
    "    // TODO: something with the point\n"
    "    if (argc > 1 && argv[1][0] != '-') {\n"
    "        printf(\"%s: %ld\\n\", argv[1], (long)checksum((const UBYTE *)argv[1], strlen(argv[1])));\n"
    "    } else {\n"
    "        for (int i = 0; i < COUNT; i++)\n"
    "            printf(\"%ld %s\\n\", (long)i, names[i % 3]);\n"
    "    }\n"
    "    p.x += p.y * 2;\n"
    "    return p.x > 0 ? 0 : 20;\n"
    "}\n";

struct Host : SciViewHost {
    struct Window *win = nullptr;
    SciView *view = nullptr;
    bool quit = false;
    std::string scroll[2];
    std::string event = "ready";
    int markerCycle = 0;

    void sciNotify(SciView *v, const Scintilla::NotificationData &n) override {
        switch (static_cast<int>(n.nmhdr.code)) {
        case SCN_MARGINCLICK:
            if (n.margin == AF_MARGIN_SYMBOLS) {
                const intptr_t line = v->send(SCI_LINEFROMPOSITION, n.position);
                const int mask = 1 << AF_MARKER_BREAKPOINT;
                if (v->send(SCI_MARKERGET, line) & mask)
                    v->send(SCI_MARKERDELETE, line, AF_MARKER_BREAKPOINT);
                else
                    v->send(SCI_MARKERADD, line, AF_MARKER_BREAKPOINT);
                event = "margin click line " + std::to_string(line + 1);
            }
            break;
        case SCN_SAVEPOINTLEFT:
            event = "modified";
            break;
        case SCN_AUTOCSELECTION:
            event = std::string("autocomplete: ") + (n.text ? n.text : "");
            break;
        case SCN_CALLTIPCLICK:
            event = "calltip click " + std::to_string(n.position);
            break;
        default:
            break;
        }
    }
    void sciScrollInfo(SciView *, bool vertical, int pos, int visible, int total) override {
        scroll[vertical ? 0 : 1] = std::to_string(pos) + "/" + std::to_string(visible) + "/" + std::to_string(total);
    }
    void sciWantFocus(SciView *v) override {
        v->setFocus(true);
    }
    void sciUnhandledKey(SciView *v, uint16_t rawkey, uint16_t qual) override {
        if (rawkey & IECODE_UP_PREFIX)
            return;
        if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) {
            switch (rawkey) {
            case 0x33: v->send(SCI_COPY); event = "copy"; return;
            case 0x32: v->send(SCI_CUT); event = "cut"; return;
            case 0x34: v->send(SCI_PASTE); event = "paste"; return;
            case 0x31: v->send(SCI_UNDO); event = "undo"; return;
            case 0x20: v->send(SCI_SELECTALL); event = "select all"; return;
            case 0x10: quit = true; return;
            default: break;
            }
        }
        const intptr_t pos = v->send(SCI_GETCURRENTPOS);
        switch (rawkey) {
        case 0x50:      // F1
            v->send(SCI_AUTOCSHOW, 0, reinterpret_cast<intptr_t>(
                "argc argv break case char checksum const continue float for int32 main names "
                "printf puts return sizeof static strlen struct uint32 while"));
            event = "autocomplete";
            return;
        case 0x51:      // F2
            v->send(SCI_CALLTIPSHOW, pos, reinterpret_cast<intptr_t>(
                "int printf(const char *format, ...)\nWrites formatted output to stdout."));
            event = "calltip";
            return;
        case 0x52: {    // F3
            const intptr_t line = v->send(SCI_LINEFROMPOSITION, pos);
            static const int cycle[][2] = {
                { AF_MARKER_BREAKPOINT, -1 }, { AF_MARKER_BREAKPOINT_OFF, -1 },
                { AF_MARKER_CURRENT_LINE, AF_MARKER_CURRENT_BG }, { AF_MARKER_ERROR, -1 },
                { AF_MARKER_WARNING, -1 }, { AF_MARKER_FRAME, -1 }, { -1, -1 },
            };
            for (int m = 1; m <= 7; m++)
                v->send(SCI_MARKERDELETE, line, m);
            const int *c = cycle[markerCycle++ % 7];
            for (int i = 0; i < 2; i++)
                if (c[i] >= 0)
                    v->send(SCI_MARKERADD, line, c[i]);
            event = "marker " + std::to_string(c[0]);
            return;
        }
        default:
            event = "unhandled key " + std::to_string(rawkey);
            return;
        }
    }
    void sciContextMenu(SciView *, int x, int y) override {
        event = "context menu at " + std::to_string(x) + "," + std::to_string(y);
    }

    Object *space = nullptr;      // the editor's place in the layout
    Object *status = nullptr;     // read-only button showing the status line
    std::string statusText;

    // Put the view on the space gadget's render box (window coordinates), or just re-blit it
    // when the box has not moved.
    void Layout() {
        struct IBox box = {};
        IIntuition->GetAttr(SPACE_RenderBox, space, reinterpret_cast<uint32 *>(&box));
        int l, t, w, h;
        view->getBounds(&l, &t, &w, &h);
        if (box.Width <= 0 || box.Height <= 0)
            return;
        if (box.Left != l || box.Top != t || box.Width != w || box.Height != h)
            view->setBounds(box.Left, box.Top, box.Width, box.Height);
        else
            view->refresh();
    }

    void UpdateStatus() {
        const intptr_t pos = view->send(SCI_GETCURRENTPOS);
        const intptr_t line = view->send(SCI_LINEFROMPOSITION, pos);
        const intptr_t col = view->send(SCI_GETCOLUMN, pos);
        char text[200];
        snprintf(text, sizeof(text), "Ln %ld, Col %ld   V %s   H %s   %s%s", (long)line + 1, (long)col + 1,
                 scroll[0].c_str(), scroll[1].c_str(), event.c_str(), view->hasFocus() ? "" : "   (inactive)");
        if (statusText == text || !win)
            return;
        statusText = text;
        IIntuition->SetGadgetAttrs(reinterpret_cast<struct Gadget *>(status), win, nullptr,
                                   GA_Text, statusText.c_str(), TAG_END);
    }
};

// window.class calls this from WM_HANDLEINPUT, on our task, for the IDCMP classes in
// WINDOW_IDCMPHookBits. The IntuiMessage is only valid during the call.
uint32 IdcmpHook(struct Hook *hook, APTR, struct IntuiMessage *im) {
    Host *host = static_cast<Host *>(hook->h_Data);
    if (!host->view)
        return 0;
    host->view->handleMessage(im);
    if (im->Class == IDCMP_EXTENDEDMOUSE && im->Code == IMSGCODE_INTUIWHEELDATA) {
        const struct IntuiWheelData *wd = static_cast<const struct IntuiWheelData *>(im->IAddress);
        host->event = "wheel " + std::to_string(wd->WheelX) + "," + std::to_string(wd->WheelY);
    }
    if (im->Class == IDCMP_INTUITICKS)
        host->view->tick();
    return 0;
}

// The space gadget renders after every layout (the render box is only up to date then) and
// refresh: re-place or re-blit the view. GM_RENDER can also come from input.device's task,
// where the view must not be touched.
struct Task *mainTask;
uint32 RenderHook(struct Hook *hook, APTR, struct gpRender *) {
    Host *host = static_cast<Host *>(hook->h_Data);
    if (host->view && IExec->FindTask(nullptr) == mainTask)
        host->Layout();
    return 0;
}

bool LoadFile(SciView *v, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    v->send(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>(text.c_str()));
    return true;
}

} // namespace

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : nullptr;
    mainTask = IExec->FindTask(nullptr);

    Host host;
    struct Hook idcmpHook = {}, renderHook = {};
    idcmpHook.h_Entry = reinterpret_cast<HOOKFUNC>(IdcmpHook);
    idcmpHook.h_Data = &host;
    renderHook.h_Entry = reinterpret_cast<HOOKFUNC>(RenderHook);
    renderHook.h_Data = &host;

    Object *winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, path ? path : "scitest",
        WA_Left, 60, WA_Top, 40, WA_InnerWidth, 880, WA_InnerHeight, 560,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE,
        // WA_IDCMP replaces window.class's own set, so it lists what the class needs too.
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
                  IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_EXTENDEDMOUSE |
                  IDCMP_INTUITICKS,
        WINDOW_IDCMPHook, &idcmpHook,
        WINDOW_IDCMPHookBits, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY |
                              IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS,
        WINDOW_Layout, IIntuition->NewObject(ILayout->LAYOUT_GetClass(), nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_AddChild, host.space = IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr,
                GA_ReadOnly, TRUE,
                SPACE_Transparent, TRUE,
                SPACE_RenderHook, &renderHook,
                SPACE_MinWidth, 160, SPACE_MinHeight, 80,
                TAG_END),
            LAYOUT_AddChild, host.status = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ReadOnly, TRUE,
                GA_Text, "",
                BUTTON_Justification, BCJ_LEFT,
                BUTTON_BevelStyle, BVS_NONE,
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END),
        TAG_END);
    if (!winObj)
        return 20;
    struct Window *win = reinterpret_cast<struct Window *>(IIntuition->IDoMethod(winObj, WM_OPEN));
    if (!win) {
        IIntuition->DisposeObject(winObj);
        return 20;
    }
    host.win = win;
    SciView *view = SciView::create(win, &host);
    if (!view) {
        IIntuition->DisposeObject(winObj);
        return 20;
    }
    host.view = view;
    sciSetupSourceEditor(view, path ? path : "sample.c");
    if (!path || !LoadFile(view, path)) {
        view->send(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>(sampleC));
        if (path)
            host.event = std::string("cannot read ") + path;
        // Show off the markers on the sample.
        view->send(SCI_MARKERADD, 19, AF_MARKER_BREAKPOINT);
        view->send(SCI_MARKERADD, 20, AF_MARKER_CURRENT_LINE);
        view->send(SCI_MARKERADD, 20, AF_MARKER_CURRENT_BG);
        view->send(SCI_MARKERADD, 23, AF_MARKER_BREAKPOINT_OFF);
        view->send(SCI_MARKERADD, 32, AF_MARKER_ERROR);
        view->send(SCI_MARKERADD, 35, AF_MARKER_WARNING);
        view->send(SCI_MARKERADD, 38, AF_MARKER_FRAME);
    }
    view->send(SCI_EMPTYUNDOBUFFER);
    view->send(SCI_SETSAVEPOINT);
    host.Layout();
    host.UpdateStatus();
    view->setFocus(true);

    uint32 winSig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, winObj, &winSig);
    while (!host.quit) {
        if (IExec->Wait(winSig | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW: host.quit = true; break;
            case WMHI_NEWSIZE: host.Layout(); break;
            case WMHI_ACTIVE: view->setFocus(true); break;
            case WMHI_INACTIVE: view->setFocus(false); break;
            default: break;
            }
        }
        view->tick();
        host.UpdateStatus();
    }

    delete view;
    host.view = nullptr;
    IIntuition->DisposeObject(winObj);
    return 0;
}
