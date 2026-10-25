// bouncer - a C++ example that uses the AmigaOS 4 APIs: an Intuition window with a GadTools
// menu, graphics.library drawing, IDCMP input and timer.device for the frame rate.
//
// Click in the window (or Project > Add Ball, Amiga-A) to add balls; Amiga-C clears them.
// Try: put the cursor on OpenWindowTags in window.cpp and press F1 for its AutoDoc, or set a
// breakpoint in Ball::step and watch `balls.size()`.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <proto/exec.h>
#include <dos/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <libraries/gadtools.h>

#include "window.h"

struct Ball {
    int x, y, dx, dy, size;
    uint32 colour;

    void step(int width, int height)
    {
        x += dx;
        y += dy;
        if (x < 0) { x = 0; dx = -dx; }
        if (y < 0) { y = 0; dy = -dy; }
        if (x + size > width) { x = width - size; dx = -dx; }
        if (y + size > height) { y = height - size; dy = -dy; }
    }
};

static Ball randomBall(int x, int y)
{
    static const uint32 colours[] = { 0xFFE04040, 0xFF40C040, 0xFF4060E0, 0xFFE0C020, 0xFFC040C0, 0xFF20C0C0 };
    Ball b;
    b.size = 10 + std::rand() % 20;
    b.x = x - b.size / 2;
    b.y = y - b.size / 2;
    do { b.dx = std::rand() % 9 - 4; } while (b.dx == 0);
    do { b.dy = std::rand() % 9 - 4; } while (b.dy == 0);
    b.colour = colours[std::rand() % 6];
    return b;
}

int main()
{
    os::AppWindow win("Bouncer - click to add balls", 480, 320);
    if (!win.ok()) {
        std::printf("can't open the window\n");
        return 20;
    }
    os::Ticker ticker(20000);   // 50 frames a second
    if (!ticker.ok()) {
        std::printf("can't open timer.device\n");
        return 20;
    }

    std::vector<Ball> balls;
    for (int i = 0; i < 5; i++) balls.push_back(randomBall(240, 160));

    bool running = true;
    unsigned long frames = 0;
    while (running) {
        uint32 got = IExec->Wait(win.signal() | ticker.signal() | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) running = false;

        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)IExec->GetMsg(win.get()->UserPort))) {
            uint32 cls = msg->Class;
            uint16 code = msg->Code;
            int mx = msg->MouseX - win.innerLeft(), my = msg->MouseY - win.innerTop();
            IExec->ReplyMsg((struct Message *)msg);
            switch (cls) {
            case IDCMP_CLOSEWINDOW:
                running = false;
                break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTDOWN) balls.push_back(randomBall(mx, my));
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27) running = false;              // Esc
                break;
            case IDCMP_REFRESHWINDOW:
                IIntuition->BeginRefresh(win.get());
                IIntuition->EndRefresh(win.get(), TRUE);
                break;
            case IDCMP_MENUPICK: {
                uint16 item = code;
                while (item != MENUNULL) {
                    struct MenuItem *mi = IIntuition->ItemAddress(win.get()->MenuStrip, item);
                    if (!mi) break;
                    switch ((uint32)GTMENUITEM_USERDATA(mi)) {
                    case MENU_ADD: balls.push_back(randomBall(win.innerWidth() / 2, win.innerHeight() / 2)); break;
                    case MENU_CLEAR: balls.clear(); break;
                    case MENU_QUIT: running = false; break;
                    case MENU_ABOUT:
                        std::printf("Bouncer: an AmiForge C++ example (%lu balls)\n", (unsigned long)balls.size());
                        break;
                    }
                    item = mi->NextSelect;
                }
                break;
            }
            }
        }

        if (ticker.fired()) {
            int w = win.innerWidth(), h = win.innerHeight();
            win.fill(win.innerLeft(), win.innerTop(), w, h, 0xFF101828);
            for (auto &b : balls) {
                b.step(w, h);
                win.fill(win.innerLeft() + b.x, win.innerTop() + b.y, b.size, b.size, b.colour);
            }
            char status[64];
            std::snprintf(status, sizeof status, "%lu balls  frame %lu", (unsigned long)balls.size(), ++frames);
            win.text(win.innerLeft() + 6, win.innerTop() + 4, status, 0xFFE0E0E0);
        }
    }
    return 0;
}
