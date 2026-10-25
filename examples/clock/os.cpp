// os.cpp - the OS side of the clock: Intuition, graphics.library and timer.device.
#include "os.h"

#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <graphics/rpattr.h>

namespace os {

Window::Window(const char *title, int width, int height)
{
    win = IIntuition->OpenWindowTags(nullptr,
        WA_Title, title,
        WA_InnerWidth, width, WA_InnerHeight, height,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_MinWidth, 120, WA_MinHeight, 120, WA_MaxWidth, ~0U, WA_MaxHeight, ~0U,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_DONE);
}

Window::~Window()
{
    if (win) IIntuition->CloseWindow(win);
}

BackBuffer::BackBuffer(struct ::Window *win, int width, int height) : w(width), h(height)
{
    struct BitMap *friendBm = win->RPort->BitMap;
    uint32 depth = IGraphics->GetBitMapAttr(friendBm, BMA_DEPTH);
    bm = IGraphics->AllocBitMapTags(width, height, depth, BMATags_Friend, friendBm, TAG_DONE);
    IGraphics->InitRastPort(&port);
    port.BitMap = bm;
    IGraphics->SetFont(&port, win->RPort->Font);
}

BackBuffer::~BackBuffer()
{
    if (bm) IGraphics->FreeBitMap(bm);
}

void BackBuffer::show(struct ::Window *win, int x, int y)
{
    IGraphics->BltBitMapRastPort(bm, 0, 0, win->RPort, x, y, w, h, 0xC0);
}

void BackBuffer::fill(int x, int y, int width, int height, uint32 argb)
{
    if (width <= 0 || height <= 0) return;
    IGraphics->SetRPAttrs(&port, RPTAG_APenColor, argb, TAG_DONE);
    IGraphics->RectFill(&port, x, y, x + width - 1, y + height - 1);
}

// A filled circle, one horizontal span per row.
void BackBuffer::disc(int cx, int cy, int r, uint32 argb)
{
    IGraphics->SetRPAttrs(&port, RPTAG_APenColor, argb, TAG_DONE);
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        IGraphics->RectFill(&port, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// A thick line: the same line drawn at every offset inside a small circle.
void BackBuffer::line(int x0, int y0, int x1, int y1, int t, uint32 argb)
{
    IGraphics->SetRPAttrs(&port, RPTAG_APenColor, argb, TAG_DONE);
    for (int oy = -t; oy <= t; oy++)
        for (int ox = -t; ox <= t; ox++) {
            if (ox * ox + oy * oy > t * t) continue;
            IGraphics->Move(&port, x0 + ox, y0 + oy);
            IGraphics->Draw(&port, x1 + ox, y1 + oy);
        }
}

void BackBuffer::text(int x, int y, const char *s, uint32 argb, bool centred)
{
    int n = (int)strlen(s);
    if (centred) x -= IGraphics->TextLength(&port, s, n) / 2;
    IGraphics->SetRPAttrs(&port, RPTAG_APenColor, argb, RPTAG_DrMd, JAM1, TAG_DONE);
    IGraphics->Move(&port, x, y + port.TxBaseline);
    IGraphics->Text(&port, s, n);
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
