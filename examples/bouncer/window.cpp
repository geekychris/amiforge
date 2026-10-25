// window.cpp - the OS side: Intuition, GadTools menus, graphics and timer.device.
#include "window.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <libraries/gadtools.h>
#include <graphics/rpattr.h>

namespace os {

static struct NewMenu menuDef[] = {
    { NM_TITLE, (STRPTR)"Project",   nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"Add Ball",  (STRPTR)"A", 0, 0, (APTR)MENU_ADD },
    { NM_ITEM,  (STRPTR)"Clear",     (STRPTR)"C", 0, 0, (APTR)MENU_CLEAR },
    { NM_ITEM,  NM_BARLABEL,         nullptr, 0, 0, nullptr },
    { NM_ITEM,  (STRPTR)"About...",  (STRPTR)"?", 0, 0, (APTR)MENU_ABOUT },
    { NM_ITEM,  (STRPTR)"Quit",      (STRPTR)"Q", 0, 0, (APTR)MENU_QUIT },
    { NM_END,   nullptr,             nullptr, 0, 0, nullptr },
};

AppWindow::AppWindow(const char *title, int width, int height)
{
    win = IIntuition->OpenWindowTags(nullptr,
        WA_Title, title,
        WA_InnerWidth, width, WA_InnerHeight, height,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_MinWidth, 160, WA_MinHeight, 100, WA_MaxWidth, ~0U, WA_MaxHeight, ~0U,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY |
                  IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        TAG_DONE);
    if (!win) return;
    visual = IGadTools->GetVisualInfo(win->WScreen, TAG_DONE);
    menu = IGadTools->CreateMenus(menuDef, TAG_DONE);
    if (menu && visual && IGadTools->LayoutMenus(menu, visual, GTMN_NewLookMenus, TRUE, TAG_DONE))
        IIntuition->SetMenuStrip(win, menu);
}

AppWindow::~AppWindow()
{
    if (win) {
        if (menu) IIntuition->ClearMenuStrip(win);
        IIntuition->CloseWindow(win);
    }
    if (menu) IGadTools->FreeMenus(menu);
    if (visual) IGadTools->FreeVisualInfo(visual);
}

void AppWindow::fill(int x, int y, int w, int h, uint32 argb)
{
    if (w <= 0 || h <= 0) return;
    IGraphics->SetRPAttrs(win->RPort, RPTAG_APenColor, argb, TAG_DONE);
    IGraphics->RectFill(win->RPort, x, y, x + w - 1, y + h - 1);
}

void AppWindow::text(int x, int y, const char *s, uint32 argb)
{
    struct RastPort *r = win->RPort;
    IGraphics->SetRPAttrs(r, RPTAG_APenColor, argb, RPTAG_DrMd, JAM1, TAG_DONE);
    IGraphics->Move(r, x, y + r->TxBaseline);
    int n = 0;
    while (s[n]) n++;
    IGraphics->Text(r, s, n);
}

Ticker::Ticker(uint32 micros) : period(micros)
{
    port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!port) return;
    req = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, port, TAG_DONE);
    if (req && IExec->OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)req, 0) != 0) {
        IExec->FreeSysObject(ASOT_IOREQUEST, req);
        req = nullptr;
    }
    if (req) arm();
}

Ticker::~Ticker()
{
    if (req) {
        if (pending) {
            IExec->AbortIO((struct IORequest *)req);
            IExec->WaitIO((struct IORequest *)req);
        }
        IExec->CloseDevice((struct IORequest *)req);
        IExec->FreeSysObject(ASOT_IOREQUEST, req);
    }
    if (port) IExec->FreeSysObject(ASOT_PORT, port);
}

void Ticker::arm()
{
    req->Request.io_Command = TR_ADDREQUEST;
    req->Time.Seconds = period / 1000000;
    req->Time.Microseconds = period % 1000000;
    IExec->SendIO((struct IORequest *)req);
    pending = true;
}

bool Ticker::fired()
{
    if (!pending || !IExec->CheckIO((struct IORequest *)req)) return false;
    IExec->WaitIO((struct IORequest *)req);
    arm();
    return true;
}

} // namespace os
