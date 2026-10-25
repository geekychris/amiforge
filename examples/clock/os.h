// os.h - small RAII wrappers around the AmigaOS 4 resources the clock uses. Each class opens
// its resource in the constructor and gives it back in the destructor, so leaving main() (or
// an early return) always cleans up, in reverse order.
#pragma once

#include <exec/types.h>
#include <intuition/intuition.h>
#include <devices/timer.h>
#include <graphics/rastport.h>

namespace os {

// A resizable Intuition window on the Workbench screen.
class Window {
public:
    Window(const char *title, int width, int height);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool ok() const { return win != nullptr; }
    struct ::Window *get() const { return win; }
    uint32 signal() const { return 1UL << win->UserPort->mp_SigBit; }
    int left() const { return win->BorderLeft; }
    int top() const { return win->BorderTop; }
    int width() const { return win->Width - win->BorderLeft - win->BorderRight; }
    int height() const { return win->Height - win->BorderTop - win->BorderBottom; }

private:
    struct ::Window *win = nullptr;
};

// An off-screen bitmap (a "friend" of the window's, so blitting it in is fast) with its own
// RastPort. Drawing goes here, then one blit shows the finished frame: no flicker.
class BackBuffer {
public:
    BackBuffer(struct ::Window *w, int width, int height);
    ~BackBuffer();
    BackBuffer(const BackBuffer &) = delete;
    BackBuffer &operator=(const BackBuffer &) = delete;

    bool ok() const { return bm != nullptr; }
    int width() const { return w; }
    int height() const { return h; }
    struct RastPort *rp() { return &port; }
    void show(struct ::Window *win, int x, int y);   // blit into the window

    // drawing helpers with 0xAARRGGBB colours
    void fill(int x, int y, int width, int height, uint32 argb);
    void disc(int cx, int cy, int r, uint32 argb);
    void line(int x0, int y0, int x1, int y1, int thickness, uint32 argb);
    void text(int x, int y, const char *s, uint32 argb, bool centred);

private:
    struct BitMap *bm = nullptr;
    struct RastPort port;
    int w, h;
};

// timer.device: a signal every `micros` microseconds.
class Ticker {
public:
    explicit Ticker(uint32 micros);
    ~Ticker();
    Ticker(const Ticker &) = delete;
    Ticker &operator=(const Ticker &) = delete;
    bool ok() const { return req != nullptr; }
    uint32 signal() const { return 1UL << port->mp_SigBit; }
    bool fired();   // true once per tick; re-arms itself

private:
    void arm();
    struct MsgPort *port = nullptr;
    struct TimeRequest *req = nullptr;
    uint32 period;
    bool pending = false;
};

} // namespace os
