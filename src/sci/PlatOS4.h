// Internal to src/sci: what Scintilla's opaque WindowID / SurfaceID point to on AmigaOS 4.
//
// - SurfaceID is a struct RastPort *. If the RastPort's layer has no Intuition window it is
//   one of our off-screen layers, and the surface may read pixels back (alpha blending).
// - WindowID is an OS4Window *: the editor view itself, or a popup (autocomplete list,
//   calltip) which is a borderless Intuition window of its own.
#pragma once

#include <functional>

struct Screen;
struct Window;
struct RastPort;
struct BitMap;
struct Layer_Info;
struct Layer;

namespace Scintilla::Internal {

class OS4Window {
public:
    virtual ~OS4Window() {}
    // Screen coordinates of the client area.
    virtual PRectangle ScreenRect() const = 0;
    virtual void SetScreenRect(PRectangle) {}
    virtual void Show(bool) {}
    // Client coordinates.
    virtual void Invalidate(PRectangle rc) = 0;
    virtual void SetPointer(Window::Cursor) {}
    virtual struct Screen *GetScreen() const = 0;
    // Popups delete themselves; the editor view ignores this.
    virtual void DestroyWindow() {}
};

// An off-screen bitmap (a friend of the screen's) with its own layer, so that drawing into
// it is clipped to the bitmap and InstallClipRegion works.
class OffscreenBuffer {
public:
    OffscreenBuffer() = default;
    OffscreenBuffer(const OffscreenBuffer &) = delete;
    OffscreenBuffer &operator=(const OffscreenBuffer &) = delete;
    ~OffscreenBuffer() { Free(); }
    bool Allocate(int width, int height, struct ::BitMap *friendBitMap);
    void Free();
    struct ::RastPort *RP() const;
    struct ::BitMap *BM() const { return bm; }
    int Width() const { return width; }
    int Height() const { return height; }
private:
    struct ::BitMap *bm = nullptr;
    struct ::Layer_Info *li = nullptr;
    struct ::Layer *layer = nullptr;
    int width = 0, height = 0;
};

// The screen whose bitmap new pixmaps are friends of, and whose colour map palette pens
// come from. Set by the view before it paints.
void OS4SetCurrentScreen(struct Screen *screen);
// Release palette pens obtained on a screen (before the window closes).
void OS4ReleasePens(struct Screen *screen);
// A borderless popup window (calltips) that paints with `paint` and reports clicks to
// `click` (client coordinates, milliseconds). Hidden until shown; Window::Destroy deletes it.
OS4Window *OS4CreatePopup(struct Screen *screen,
                          std::function<void(Surface &, PRectangle)> paint,
                          std::function<void(Point, unsigned int)> click);
// Popups are ToolBox windows, which are never activated: a click on one reaches the active
// window as just a SELECTUP (the SELECTDOWN is swallowed). The view passes those here in
// screen coordinates. OS4PopupAt tells whether a point is on a popup.
bool OS4PopupAt(int screenX, int screenY);
bool OS4PopupClick(int screenX, int screenY, unsigned int milliseconds);
// Poll the popup windows' message ports (clicks into the autocomplete list or a calltip)
// and repaint those that need it.
void OS4PollPopups();
// Monotonic milliseconds.
unsigned int OS4Milliseconds();

}
