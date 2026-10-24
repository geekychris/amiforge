// SciView — a Scintilla editor view inside a rectangle of an Intuition window (AmigaOS 4.1).
//
// The IDE lays out its window with ReAction and leaves a space.gadget where each editor goes.
// It hands SciView that rectangle (window coordinates), routes the window's IntuiMessages to
// it, and calls tick() regularly. Everything runs on the IDE's own task: no input.device
// context, no locking.
//
// SciView paints into an off-screen bitmap and blits it to the window, so it never
// flickers and can repaint itself after the window is uncovered.
#pragma once

#include <stdint.h>

struct Window;
struct IntuiMessage;

namespace Scintilla { struct NotificationData; }

namespace af {

class SciView;

// Implemented by the owner (the IDE, or a test program).
class SciViewHost {
public:
    virtual ~SciViewHost() {}
    // Scintilla notifications (SCN_* in Scintilla.h: SCN_MODIFIED, SCN_MARGINCLICK, SCN_UPDATEUI,
    // SCN_SAVEPOINTREACHED, SCN_SAVEPOINTLEFT, SCN_CHARADDED, SCN_DWELLSTART...).
    virtual void sciNotify(SciView *view, const Scintilla::NotificationData &n) = 0;
    // The view's scroll range changed or it scrolled: update scroller gadgets.
    // pos/visible/total are in lines (vertical) or pixels (horizontal).
    virtual void sciScrollInfo(SciView *view, bool vertical, int pos, int visible, int total) {}
    // The view was clicked and wants keyboard focus (the host decides; call setFocus).
    virtual void sciWantFocus(SciView *view) {}
    // A key the view did not use (function keys, Amiga-key shortcuts when no menu took them...).
    // rawkey/qualifier are from the IntuiMessage.
    virtual void sciUnhandledKey(SciView *view, uint16_t rawkey, uint16_t qualifier) {}
    // Right mouse button inside the view at window coordinates (x, y) — show a context menu.
    virtual void sciContextMenu(SciView *view, int x, int y) {}
};

class SciView {
public:
    // Create a view on an open window. The window must have IDCMP_MOUSEBUTTONS,
    // IDCMP_MOUSEMOVE (+ WFLG_REPORTMOUSE), IDCMP_RAWKEY, IDCMP_EXTENDEDMOUSE and
    // IDCMP_INTUITICKS enabled; the host routes those messages to handleMessage().
    static SciView *create(struct Window *win, SciViewHost *host);
    virtual ~SciView() {}

    // Place the view (window coordinates, inclusive left/top, width/height in pixels).
    // Call after the first layout and on every WMHI_NEWSIZE. Repaints.
    virtual void setBounds(int left, int top, int width, int height) = 0;
    virtual void getBounds(int *left, int *top, int *width, int *height) const = 0;

    // Scintilla message API (SCI_* from Scintilla.h, plus the AF_SCI_* messages below).
    // send() does not repaint by itself: the view repaints on the next handleMessage(),
    // tick(), setBounds(), scrollTo() or setFocus(), so a batch of messages costs one paint.
    virtual intptr_t send(unsigned int msg, uintptr_t wParam = 0, intptr_t lParam = 0) = 0;

    // Offer an IntuiMessage (not yet replied). Returns true if the view used it.
    // Mouse messages are used when inside the view's bounds (or while it is dragging);
    // key messages only when the view has focus.
    virtual bool handleMessage(const struct IntuiMessage *im) = 0;

    // Focus loss takes effect at the next tick(): clicking the autocomplete list (a ToolBox
    // window) briefly deactivates the host window, and the list must survive that.
    virtual void setFocus(bool focus) = 0;
    virtual bool hasFocus() const = 0;

    // Call on every IDCMP_INTUITICKS (~10 Hz) and whenever the host's event loop is idle:
    // caret blink, drag auto-scroll, idle styling.
    virtual void tick() = 0;

    // A scroller gadget moved: pos in lines (vertical) or pixels (horizontal).
    virtual void scrollTo(bool vertical, int pos) = 0;

    // Re-blit the off-screen image (after IDCMP_REFRESHWINDOW, or the window was redrawn).
    virtual void refresh() = 0;

    // The window's screen/font/colour depth changed, or the window is being closed and
    // re-opened (iconify): drop and re-create bitmaps/pens for the given window.
    virtual void attach(struct Window *win) = 0;
    virtual void detach() = 0;

    // Opaque pointer for the host.
    void *userData = nullptr;
};

// Set up a view for source editing: DejaVu Sans Mono (falling back to topaz), Latin-1,
// line numbers, a breakpoint margin, folding, and the lexer for the file name
// ("c", "cpp", "asm" for .s/.S/.asm/.i, "makefile", "props", "diff", or plain text).
void sciSetupSourceEditor(SciView *v, const char *fileName, int fontSize = 13);

// Markers the IDE uses in the symbol margin (margin 1).
enum {
    AF_MARKER_BREAKPOINT = 1,        // red circle
    AF_MARKER_BREAKPOINT_OFF = 2,    // hollow circle: disabled or not resolved to code
    AF_MARKER_CURRENT_LINE = 3,      // yellow arrow: where execution is stopped
    AF_MARKER_CURRENT_BG = 4,        // background highlight of that line
    AF_MARKER_ERROR = 5,             // red background: compiler error line
    AF_MARKER_WARNING = 6,           // amber background: compiler warning line
    AF_MARKER_FRAME = 7,             // green arrow: selected (non-top) stack frame
};
enum { AF_MARGIN_NUMBERS = 0, AF_MARGIN_SYMBOLS = 1, AF_MARGIN_FOLD = 2 };

// Messages SciView handles itself (send them like SCI_* messages). sciSetupSourceEditor
// turns both on.
enum {
    AF_SCI_SETBRACEMATCH = 0x7F00,   // wParam != 0: highlight the brace pair at the caret
    AF_SCI_SETAUTOINDENT = 0x7F01,   // wParam != 0: a new line gets the previous line's indent,
                                     // one level more after '{'; a '}' typed first is outdented
};

} // namespace af
