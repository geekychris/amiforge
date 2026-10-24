// scitest — a SciView in a plain Intuition window on Workbench.
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
#include <proto/graphics.h>
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

constexpr int statusHeight = 18;

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

    void Layout() {
        const int w = win->Width - win->BorderLeft - win->BorderRight;
        const int h = win->Height - win->BorderTop - win->BorderBottom - statusHeight;
        view->setBounds(win->BorderLeft, win->BorderTop, w, h);
        DrawStatus();
    }

    void DrawStatus() {
        struct RastPort *rp = win->RPort;
        const int x0 = win->BorderLeft, y0 = win->Height - win->BorderBottom - statusHeight;
        const int x1 = win->Width - win->BorderRight - 1, y1 = win->Height - win->BorderBottom - 1;
        if (x1 <= x0 || y1 <= y0)
            return;
        const intptr_t pos = view->send(SCI_GETCURRENTPOS);
        const intptr_t line = view->send(SCI_LINEFROMPOSITION, pos);
        const intptr_t col = view->send(SCI_GETCOLUMN, pos);
        char text[200];
        snprintf(text, sizeof(text), "Ln %ld, Col %ld   V %s   H %s   %s%s", (long)line + 1, (long)col + 1,
                 scroll[0].c_str(), scroll[1].c_str(), event.c_str(), view->hasFocus() ? "" : "   (inactive)");
        IGraphics->SetAPen(rp, 0);
        IGraphics->RectFill(rp, x0, y0, x1, y1);
        IGraphics->SetAPen(rp, 1);
        IGraphics->SetDrMd(rp, JAM1);
        IGraphics->Move(rp, x0 + 4, y0 + 2 + rp->TxBaseline);
        IGraphics->Text(rp, text, strlen(text));
    }
};

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
    struct Screen *screen = IIntuition->LockPubScreen(nullptr);
    if (!screen)
        return 20;
    struct Window *win = IIntuition->OpenWindowTags(nullptr,
        WA_PubScreen, screen,
        WA_Title, path ? path : "scitest",
        WA_Left, 60, WA_Top, 40, WA_Width, 900, WA_Height, 600,
        WA_MinWidth, 200, WA_MinHeight, 120, WA_MaxWidth, ~0u, WA_MaxHeight, ~0u,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS |
                  IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS |
                  IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        TAG_END);
    IIntuition->UnlockPubScreen(nullptr, screen);
    if (!win)
        return 20;

    Host host;
    host.win = win;
    SciView *view = SciView::create(win, &host);
    if (!view) {
        IIntuition->CloseWindow(win);
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
    view->setFocus(true);

    while (!host.quit) {
        if (IExec->Wait((1u << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        struct IntuiMessage *im;
        while ((im = reinterpret_cast<struct IntuiMessage *>(IExec->GetMsg(win->UserPort)))) {
            view->handleMessage(im);
            if (im->Class == IDCMP_EXTENDEDMOUSE && im->Code == IMSGCODE_INTUIWHEELDATA) {
                const struct IntuiWheelData *wd = static_cast<const struct IntuiWheelData *>(im->IAddress);
                host.event = "wheel " + std::to_string(wd->WheelX) + "," + std::to_string(wd->WheelY);
            }
            switch (im->Class) {
            case IDCMP_CLOSEWINDOW: host.quit = true; break;
            case IDCMP_NEWSIZE: host.Layout(); break;
            case IDCMP_ACTIVEWINDOW: view->setFocus(true); break;
            case IDCMP_INACTIVEWINDOW: view->setFocus(false); break;
            case IDCMP_INTUITICKS: view->tick(); break;
            case IDCMP_REFRESHWINDOW:
                IIntuition->BeginRefresh(win);
                view->refresh();
                host.DrawStatus();
                IIntuition->EndRefresh(win, TRUE);
                break;
            default: break;
            }
            IExec->ReplyMsg(&im->ExecMessage);
        }
        view->tick();
        host.DrawStatus();
    }

    delete view;
    IIntuition->CloseWindow(win);
    return 0;
}
