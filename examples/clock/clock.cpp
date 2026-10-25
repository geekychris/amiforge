// clock - an analog and digital clock in a resizable window. A C++ example using the OS:
//   - timer.device (os::Ticker) wakes the program four times a second;
//   - dos.library's DateStamp() and DateToStr() give the local time and the date;
//   - graphics.library draws the face into an off-screen bitmap (os::BackBuffer, a friend of
//     the screen's bitmap), which one BltBitMapRastPort() then copies into the window, so the
//     hands never flicker; resizing the window makes a new bitmap of the new size;
//   - Intuition: a simple-refresh window (IDCMP_REFRESHWINDOW repaints from the bitmap).
// No floating point: the hands use a table of sines in 6-degree steps.
//
// Try: set a breakpoint in drawClock() and watch `hand`, or turn on Debug > Track Leaks and
// close the window: everything the program opened is given back.
#include <cstdio>
#include <memory>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <dos/datetime.h>

#include "os.h"

// sin(i * 6 degrees) * 1024, i = 0..59
static const int sine[60] = {
    0, 107, 213, 316, 416, 512, 602, 685, 761, 828, 887, 935, 974, 1002, 1018, 1024, 1018, 1002,
    974, 935, 887, 828, 761, 685, 602, 512, 416, 316, 213, 107, 0, -107, -213, -316, -416, -512,
    -602, -685, -761, -828, -887, -935, -974, -1002, -1018, -1024, -1018, -1002, -974, -935, -887,
    -828, -761, -685, -602, -512, -416, -316, -213, -107,
};

// The point at `len` from the centre, at position `pos` of 60 (0 = twelve o'clock).
static void handEnd(int cx, int cy, int pos, int len, int &x, int &y)
{
    pos = ((pos % 60) + 60) % 60;
    x = cx + sine[pos] * len / 1024;
    y = cy - sine[(pos + 15) % 60] * len / 1024;
}

struct Time {
    int hours, minutes, seconds;
    char day[32], date[32];
};

static Time now()
{
    Time t = {};
    struct DateTime dt = {};
    IDOS->DateStamp(&dt.dat_Stamp);
    t.hours = (int)(dt.dat_Stamp.ds_Minute / 60);
    t.minutes = (int)(dt.dat_Stamp.ds_Minute % 60);
    t.seconds = (int)(dt.dat_Stamp.ds_Tick / TICKS_PER_SECOND);
    dt.dat_Format = FORMAT_DOS;
    dt.dat_StrDay = t.day;
    dt.dat_StrDate = t.date;
    IDOS->DateToStr(&dt);
    return t;
}

static void drawClock(os::BackBuffer &b, const Time &t)
{
    const uint32 background = 0xFF1C2433, face = 0xFFF4F1E8, rim = 0xFF8090A8, ink = 0xFF202020,
                 second = 0xFFD03030, label = 0xFFE0E6F0;
    int w = b.width(), h = b.height();
    int textH = b.rp()->TxHeight;
    b.fill(0, 0, w, h, background);

    // the face fills what's left above two lines of text
    int faceArea = h - 2 * textH - 12;
    int r = (w < faceArea ? w : faceArea) / 2 - 6;
    int cx = w / 2, cy = 4 + r + 2;
    if (r > 10) {
        b.disc(cx, cy, r + 3, rim);
        b.disc(cx, cy, r, face);
        for (int i = 0; i < 60; i++) {
            int x0, y0, x1, y1;
            bool hour = i % 5 == 0;
            handEnd(cx, cy, i, r - 2, x0, y0);
            handEnd(cx, cy, i, r - (hour ? r / 7 : r / 18) - 2, x1, y1);
            b.line(x0, y0, x1, y1, hour ? 1 : 0, ink);
        }
        int x, y;
        int hand = (t.hours % 12) * 5 + t.minutes / 12;   // the hour hand moves every 12 minutes
        handEnd(cx, cy, hand, r * 5 / 10, x, y);
        b.line(cx, cy, x, y, r > 60 ? 3 : 2, ink);
        handEnd(cx, cy, t.minutes, r * 8 / 10, x, y);
        b.line(cx, cy, x, y, r > 60 ? 2 : 1, ink);
        handEnd(cx, cy, t.seconds, r * 9 / 10, x, y);
        b.line(cx, cy, x, y, 0, second);
        b.disc(cx, cy, r > 60 ? 4 : 2, second);
    }
    char digital[16];
    std::snprintf(digital, sizeof digital, "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
    int ty = h - 2 * textH - 6;
    b.text(cx, ty, digital, label, true);
    char date[80];
    std::snprintf(date, sizeof date, "%s %s", t.day, t.date);
    b.text(cx, ty + textH + 2, date, label, true);
}

int main()
{
    os::Window win("Clock", 240, 280);
    if (!win.ok()) {
        std::printf("clock: can't open the window\n");
        return 20;
    }
    os::Ticker ticker(250000);   // four times a second: the second hand is never late by much
    if (!ticker.ok()) {
        std::printf("clock: can't open timer.device\n");
        return 20;
    }

    std::unique_ptr<os::BackBuffer> buffer;
    int lastSecond = -1;
    bool running = true;
    while (running) {
        bool redraw = false, reshow = false;
        uint32 got = IExec->Wait(win.signal() | ticker.signal() | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) running = false;

        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)IExec->GetMsg(win.get()->UserPort))) {
            uint32 cls = msg->Class;
            uint16 code = msg->Code;
            IExec->ReplyMsg((struct Message *)msg);
            switch (cls) {
            case IDCMP_CLOSEWINDOW: running = false; break;
            case IDCMP_VANILLAKEY: if (code == 27) running = false; break;   // Esc
            case IDCMP_NEWSIZE: redraw = true; break;
            case IDCMP_REFRESHWINDOW:
                IIntuition->BeginRefresh(win.get());
                if (buffer) buffer->show(win.get(), win.left(), win.top());
                IIntuition->EndRefresh(win.get(), TRUE);
                break;
            }
        }
        if (ticker.fired()) reshow = true;
        if (!running) break;

        // a new bitmap when the window's size changed
        if (!buffer || buffer->width() != win.width() || buffer->height() != win.height()) {
            buffer.reset();   // free the old one first
            buffer.reset(new os::BackBuffer(win.get(), win.width(), win.height()));
            if (!buffer->ok()) {
                std::printf("clock: out of memory for a %dx%d bitmap\n", win.width(), win.height());
                return 20;
            }
            redraw = true;
        }
        Time t = now();
        if (redraw || (reshow && t.seconds != lastSecond)) {
            drawClock(*buffer, t);
            buffer->show(win.get(), win.left(), win.top());
            lastSecond = t.seconds;
        }
    }
    return 0;
}
