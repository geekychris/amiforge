// window.h - small C++ wrappers around AmigaOS 4 resources (RAII: everything that is
// opened is closed again by a destructor, in the right order).
#pragma once

#include <exec/types.h>
#include <intuition/intuition.h>
#include <devices/timer.h>

namespace os {

// An Intuition window on the Workbench screen with a menu strip.
class AppWindow {
public:
    AppWindow(const char *title, int width, int height);
    ~AppWindow();
    AppWindow(const AppWindow &) = delete;
    AppWindow &operator=(const AppWindow &) = delete;

    bool ok() const { return win != nullptr; }
    struct Window *get() const { return win; }
    struct RastPort *rp() const { return win->RPort; }
    uint32 signal() const { return 1UL << win->UserPort->mp_SigBit; }
    int innerLeft() const { return win->BorderLeft; }
    int innerTop() const { return win->BorderTop; }
    int innerWidth() const { return win->Width - win->BorderLeft - win->BorderRight; }
    int innerHeight() const { return win->Height - win->BorderTop - win->BorderBottom; }

    // Fill with an ARGB colour (true-colour screens) or the nearest pen.
    void fill(int x, int y, int w, int h, uint32 argb);
    void text(int x, int y, const char *s, uint32 argb);

private:
    struct Window *win = nullptr;
    struct Menu *menu = nullptr;
    APTR visual = nullptr;
};

// A timer.device request that wakes us up every `micros` microseconds.
class Ticker {
public:
    explicit Ticker(uint32 micros);
    ~Ticker();
    bool ok() const { return req != nullptr; }
    uint32 signal() const { return 1UL << port->mp_SigBit; }
    bool fired();    // true once per tick; re-arms itself
private:
    void arm();
    struct MsgPort *port = nullptr;
    struct TimeRequest *req = nullptr;
    uint32 period;
    bool pending = false;
};

} // namespace os

// Menu item user data
enum { MENU_ADD = 1, MENU_CLEAR, MENU_ABOUT, MENU_QUIT };
