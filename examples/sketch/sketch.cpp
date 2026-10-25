// sketch - draw with the mouse and save the picture as an IFF ILBM. A C++ example using the OS:
//   - Intuition: a window with IDCMP_MOUSEBUTTONS and IDCMP_MOUSEMOVE (WA_ReportMouse) for
//     drawing, and an EasyRequest() before throwing away unsaved work;
//   - GadTools menus (CreateMenus/LayoutMenus) with mutually exclusive check marks for the pen
//     colour and brush size;
//   - graphics.library: RectFill() spans with ARGB colours (SetRPAttrs);
//   - asl.library's save requester, and iffparse.library to write the FORM ILBM (canvas.cpp);
//     the result opens in MultiView or any paint program.
// The C++ classes own the OS resources: the window and its menus (SketchWindow), the IFF
// handle and its file (IffWriter in canvas.cpp).
#include <cstdio>
#include <string>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/rpattr.h>

#include "canvas.h"

enum {
    MENU_CLEAR = 1, MENU_SAVE, MENU_ABOUT, MENU_QUIT,
    MENU_PEN = 100,     // + colour index
    MENU_BRUSH = 200,   // + radius
};

#define EXCL(n, i) (((1 << (n)) - 1) & ~(1 << (i)))
static struct NewMenu menuDef[] = {
    { NM_TITLE, (STRPTR)"Project",     nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"Clear",       (STRPTR)"N", 0, 0, (APTR)MENU_CLEAR },
    { NM_ITEM,  (STRPTR)"Save As...",  (STRPTR)"S", 0, 0, (APTR)MENU_SAVE },
    { NM_ITEM,  NM_BARLABEL,           nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"About...",    (STRPTR)"?", 0, 0, (APTR)MENU_ABOUT },
    { NM_ITEM,  (STRPTR)"Quit",        (STRPTR)"Q", 0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, (STRPTR)"Pen",         nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"Black",       (STRPTR)"1", CHECKIT | CHECKED, EXCL(8, 0), (APTR)(MENU_PEN + 1) },
    { NM_ITEM,  (STRPTR)"Red",         (STRPTR)"2", CHECKIT, EXCL(8, 1), (APTR)(MENU_PEN + 2) },
    { NM_ITEM,  (STRPTR)"Green",       (STRPTR)"3", CHECKIT, EXCL(8, 2), (APTR)(MENU_PEN + 3) },
    { NM_ITEM,  (STRPTR)"Blue",        (STRPTR)"4", CHECKIT, EXCL(8, 3), (APTR)(MENU_PEN + 4) },
    { NM_ITEM,  (STRPTR)"Yellow",      (STRPTR)"5", CHECKIT, EXCL(8, 4), (APTR)(MENU_PEN + 5) },
    { NM_ITEM,  (STRPTR)"Orange",      (STRPTR)"6", CHECKIT, EXCL(8, 5), (APTR)(MENU_PEN + 6) },
    { NM_ITEM,  (STRPTR)"Purple",      (STRPTR)"7", CHECKIT, EXCL(8, 6), (APTR)(MENU_PEN + 7) },
    { NM_ITEM,  (STRPTR)"Eraser",      (STRPTR)"0", CHECKIT, EXCL(8, 7), (APTR)(MENU_PEN + 0) },
    { NM_TITLE, (STRPTR)"Brush",       nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"Fine",        nullptr, CHECKIT, EXCL(3, 0), (APTR)(MENU_BRUSH + 1) },
    { NM_ITEM,  (STRPTR)"Medium",      nullptr, CHECKIT | CHECKED, EXCL(3, 1), (APTR)(MENU_BRUSH + 3) },
    { NM_ITEM,  (STRPTR)"Thick",       nullptr, CHECKIT, EXCL(3, 2), (APTR)(MENU_BRUSH + 7) },
    { NM_END,   nullptr,               nullptr, 0, 0, nullptr },
};

// The window with its GadTools menu strip.
class SketchWindow {
public:
    SketchWindow(int width, int height)
    {
        win = IIntuition->OpenWindowTags(nullptr,
            WA_Title, "Sketch",
            WA_InnerWidth, width, WA_InnerHeight, height,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
            WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE, WA_NewLookMenus, TRUE,
            WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY,
            TAG_DONE);
        if (!win) return;
        visual = IGadTools->GetVisualInfo(win->WScreen, TAG_DONE);
        menu = IGadTools->CreateMenus(menuDef, TAG_DONE);
        if (menu && visual && IGadTools->LayoutMenus(menu, visual, GTMN_NewLookMenus, TRUE, TAG_DONE))
            IIntuition->SetMenuStrip(win, menu);
    }
    ~SketchWindow()
    {
        if (win) {
            if (menu) IIntuition->ClearMenuStrip(win);
            IIntuition->CloseWindow(win);
        }
        if (menu) IGadTools->FreeMenus(menu);
        if (visual) IGadTools->FreeVisualInfo(visual);
    }
    SketchWindow(const SketchWindow &) = delete;
    SketchWindow &operator=(const SketchWindow &) = delete;

    bool ok() const { return win != nullptr; }
    struct Window *get() const { return win; }
    uint32 signal() const { return 1UL << win->UserPort->mp_SigBit; }

    // canvas pixels -> window
    void span(int x0, int x1, int y, uint32 argb)
    {
        IGraphics->SetRPAttrs(win->RPort, RPTAG_APenColor, argb, TAG_DONE);
        IGraphics->RectFill(win->RPort, win->BorderLeft + x0, win->BorderTop + y, win->BorderLeft + x1, win->BorderTop + y);
    }
    void paint(const Canvas &c)   // the whole canvas, a run of one colour at a time
    {
        for (int y = 0; y < c.height(); y++)
            for (int x = 0; x < c.width();) {
                int end = x;
                while (end + 1 < c.width() && c.at(end + 1, y) == c.at(x, y)) end++;
                span(x, end, y, Canvas::palette[c.at(x, y)]);
                x = end + 1;
            }
    }
    void setTitle(const std::string &t)
    {
        title = t;
        IIntuition->SetWindowTitles(win, title.c_str(), (CONST_STRPTR)~0);
    }
    bool askYesNo(const char *text, const char *buttons)
    {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, "Sketch", text, buttons, nullptr, nullptr };
        return IIntuition->EasyRequestArgs(win, &es, nullptr, nullptr) == 1;
    }

private:
    struct Window *win = nullptr;
    struct Menu *menu = nullptr;
    APTR visual = nullptr;
    std::string title;
};

// asl.library's file requester in save mode; "" if cancelled.
static std::string askSaveName(struct Window *win, std::string &lastPath)
{
    std::string drawer = "RAM:", file = "sketch.iff";
    size_t cut = lastPath.find_last_of(":/");
    if (cut != std::string::npos) {
        drawer = lastPath.substr(0, cut + 1);
        file = lastPath.substr(cut + 1);
    }
    struct FileRequester *fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, win, ASLFR_SleepWindow, TRUE, ASLFR_TitleText, "Save the sketch as IFF ILBM",
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialDrawer, drawer.c_str(), ASLFR_InitialFile, file.c_str(), TAG_DONE);
    if (!fr) return "";
    std::string path;
    if (IAsl->AslRequestTags(fr, TAG_DONE) && fr->fr_File && fr->fr_File[0]) {
        char full[1024];
        IUtility->Strlcpy(full, fr->fr_Drawer ? fr->fr_Drawer : "", sizeof full);
        IDOS->AddPart(full, fr->fr_File, sizeof full);
        path = full;
    }
    IAsl->FreeAslRequest(fr);
    return path;
}

int main()
{
    const int width = 512, height = 320;
    SketchWindow win(width, height);
    if (!win.ok()) {
        std::printf("sketch: can't open the window\n");
        return 20;
    }
    Canvas canvas(width, height);
    win.paint(canvas);

    uint8 pen = 1;
    int brush = 3;
    bool drawing = false, modified = false, running = true;
    int lastX = 0, lastY = 0;
    std::string lastPath;
    auto title = [&]() {
        win.setTitle(std::string("Sketch") + (modified ? " *" : "") + (lastPath.empty() ? "" : " - " + lastPath));
    };
    // a line of dots from the last point to (x, y)
    auto stroke = [&](int x, int y) {
        int dx = x - lastX, dy = y - lastY;
        int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        int every = brush > 2 ? brush / 2 : 1;
        for (int i = 0; i <= steps; i += every) {
            int px = steps ? lastX + dx * i / steps : x, py = steps ? lastY + dy * i / steps : y;
            canvas.dot(px, py, brush, pen, [&](int x0, int x1, int row) { win.span(x0, x1, row, Canvas::palette[pen]); });
        }
        canvas.dot(x, y, brush, pen, [&](int x0, int x1, int row) { win.span(x0, x1, row, Canvas::palette[pen]); });
        lastX = x;
        lastY = y;
        if (!modified) {
            modified = true;
            title();
        }
    };
    auto save = [&]() {
        std::string path = askSaveName(win.get(), lastPath);
        if (path.empty()) return false;
        std::string error;
        if (!canvas.saveILBM(path, error)) {
            win.askYesNo(error.c_str(), "OK");
            return false;
        }
        lastPath = path;
        modified = false;
        title();
        return true;
    };

    auto confirmQuit = [&]() { return !modified || win.askYesNo("Quit without saving the sketch?", "Quit|Cancel"); };

    while (running) {
        uint32 got = IExec->Wait(win.signal() | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) break;
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)IExec->GetMsg(win.get()->UserPort))) {
            uint32 cls = msg->Class;
            uint16 code = msg->Code;
            int x = msg->MouseX - win.get()->BorderLeft, y = msg->MouseY - win.get()->BorderTop;
            IExec->ReplyMsg((struct Message *)msg);
            switch (cls) {
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTDOWN && x >= 0 && y >= 0 && x < width && y < height) {
                    drawing = true;
                    lastX = x;
                    lastY = y;
                    stroke(x, y);
                } else if (code == SELECTUP) {
                    drawing = false;
                }
                break;
            case IDCMP_MOUSEMOVE:
                if (drawing) stroke(x, y);
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27 && confirmQuit()) running = false;   // Esc
                break;
            case IDCMP_CLOSEWINDOW:
                if (confirmQuit()) running = false;
                break;
            case IDCMP_MENUPICK:
                for (uint16 item = code; item != MENUNULL;) {
                    struct MenuItem *mi = IIntuition->ItemAddress(win.get()->MenuStrip, item);
                    if (!mi) break;
                    uint32 id = (uint32)GTMENUITEM_USERDATA(mi);
                    if (id >= MENU_BRUSH) brush = (int)(id - MENU_BRUSH);
                    else if (id >= MENU_PEN) pen = (uint8)(id - MENU_PEN);
                    else if (id == MENU_CLEAR) {
                        if (!modified || win.askYesNo("Clear the sketch?", "Clear|Cancel")) {
                            canvas.clear();
                            win.paint(canvas);
                            modified = false;
                            title();
                        }
                    } else if (id == MENU_SAVE) save();
                    else if (id == MENU_ABOUT)
                        win.askYesNo("Sketch: an AmiForge C++ example.\n"
                                     "Draw with the left mouse button; the menus\n"
                                     "pick the pen and the brush. Save As writes\n"
                                     "an IFF ILBM picture.", "OK");
                    else if (id == MENU_QUIT && confirmQuit())
                        running = false;
                    item = mi->NextSelect;
                }
                break;
            }
        }
    }
    return 0;
}
