// SciView: Scintilla's editor (ScintillaBase) in a rectangle of an Intuition window.
//
// Painting is deferred: Scintilla's invalidations collect into a dirty rectangle, painted
// into the off-screen buffer and blitted to the window at the end of each entry point.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>
#include <functional>

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"
#include "Scintilla.h"
#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"
#include "UniConversion.h"

#include "sci/PlatOS4.h"
#include "sci/SciView.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/textclip.h>
#include <devices/inputevent.h>
#include <intuition/pointerclass.h>

namespace af {

// Scintilla's names win over the AmigaOS structures of the same name (Window, Message, Point).
using namespace Scintilla;
using namespace Scintilla::Internal;
using Scintilla::Message;
using Scintilla::Internal::Window;
using Scintilla::Internal::Point;
using Scintilla::Internal::Menu;

namespace {

constexpr UWORD qualShift = IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT;
constexpr UWORD qualAlt = IEQUALIFIER_LALT | IEQUALIFIER_RALT;
constexpr UWORD qualAmiga = IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND;
// NewMouse wheel codes, which some mouse drivers send as raw keys.
constexpr UWORD rawWheelUp = 0x7A, rawWheelDown = 0x7B, rawWheelLeft = 0x7C, rawWheelRight = 0x7D;

KeyMod Modifiers(UWORD qual) noexcept {
    return ModifierFlags((qual & qualShift) != 0, (qual & IEQUALIFIER_CONTROL) != 0, (qual & qualAlt) != 0);
}

// Raw key codes of the keys Scintilla's key map knows by name.
std::optional<Keys> SpecialKey(UWORD code) noexcept {
    switch (code) {
    case 0x4C: return Keys::Up;
    case 0x4D: return Keys::Down;
    case 0x4E: return Keys::Right;
    case 0x4F: return Keys::Left;
    case 0x70: return Keys::Home;
    case 0x71: return Keys::End;
    case 0x48: return Keys::Prior;
    case 0x49: return Keys::Next;
    case 0x46: return Keys::Delete;
    case 0x47: return Keys::Insert;
    case 0x41: return Keys::Back;
    case 0x42: return Keys::Tab;
    case 0x43:
    case 0x44: return Keys::Return;
    case 0x45: return Keys::Escape;
    default: return std::nullopt;
    }
}

// Translate a raw key with keymap.library (the user's keymap, dead keys included).
int MapKey(UWORD code, UWORD qual, APTR deadKeys, char *buf, int len) {
    struct InputEvent ie = {};
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code;
    ie.ie_Qualifier = qual;
    ie.ie_EventAddress = deadKeys;
    return IKeymap->MapRawKey(&ie, buf, len, nullptr);
}

} // namespace

class ScintillaOS4 : public ScintillaBase, public SciView {
public:
    ScintillaOS4(struct ::Window *win_, SciViewHost *host_);
    ~ScintillaOS4() override;

    // SciView
    void setBounds(int left, int top, int width, int height) override;
    void getBounds(int *left, int *top, int *width, int *height) const override;
    intptr_t send(unsigned int msg, uintptr_t wParam, intptr_t lParam) override;
    bool handleMessage(const struct IntuiMessage *im) override;
    void setFocus(bool focus) override;
    bool hasFocus() const override { return focused && !focusLossPending; }
    void tick() override;
    void scrollTo(bool vertical, int pos) override;
    void refresh() override;
    void attach(struct ::Window *win) override;
    void detach() override;

private:
    // What Scintilla's wMain points to.
    class ViewWindow : public OS4Window {
    public:
        explicit ViewWindow(ScintillaOS4 &sci_) : sci(sci_) {}
        PRectangle ScreenRect() const override {
            if (!sci.win)
                return PRectangle(0, 0, sci.width, sci.height);
            const int x = sci.win->LeftEdge + sci.left, y = sci.win->TopEdge + sci.top;
            return PRectangle(x, y, x + sci.width, y + sci.height);
        }
        void Invalidate(PRectangle rc) override { sci.Invalidate(rc); }
        void SetPointer(Window::Cursor c) override { sci.SetPointer(c); }
        struct ::Screen *GetScreen() const override { return sci.win ? sci.win->WScreen : nullptr; }
    private:
        ScintillaOS4 &sci;
    };

    struct ::Window *win = nullptr;
    SciViewHost *host;
    ViewWindow viewWindow;
    OffscreenBuffer buffer;
    int left = 0, top = 0, width = 1, height = 1;
    bool focused = false;
    bool focusLossPending = false;
    bool capture = false;
    bool dragging = false;           // a button went down inside the view
    PRectangle dirty;
    bool isDirty = false;
    ULONG pointerType = POINTERTYPE_NORMAL;
    ULONG timeBase = 0;              // IntuiMessage seconds at creation
    bool braceMatch = false;
    bool autoIndent = false;
    int lastScroll[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };

    struct Ticker { bool running = false; int period = 0; unsigned int due = 0; };
    Ticker tickers[static_cast<int>(TickReason::platform) + 1];

    // Editor / ScintillaBase
    void SetVerticalScrollPos() override;
    void SetHorizontalScrollPos() override;
    bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
    void Copy() override;
    void Paste() override;
    void ClaimSelection() override {}
    void NotifyChange() override {}
    void NotifyParent(NotificationData scn) override;
    void CopyToClipboard(const SelectionText &selectedText) override;
    void SetMouseCapture(bool on) override { capture = on; }
    bool HaveMouseCapture() override { return capture; }
    bool FineTickerRunning(TickReason reason) override { return tickers[static_cast<int>(reason)].running; }
    void FineTickerStart(TickReason reason, int millis, int tolerance) override;
    void FineTickerCancel(TickReason reason) override { tickers[static_cast<int>(reason)].running = false; }
    bool SetIdle(bool on) override { idler.state = on; return true; }
    std::string UTF8FromEncoded(std::string_view encoded) const override;
    std::string EncodedFromUTF8(std::string_view utf8) const override;
    sptr_t DefWndProc(Message, uptr_t, sptr_t) override { return 0; }
    sptr_t WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) override;
    void CreateCallTipWindow(PRectangle rc) override;
    void AddToPopUp(const char *, int, bool) override {}

    void ApplyFocusLoss();
    void Invalidate(PRectangle rc);
    void Flush();
    void Blit(PRectangle rc);
    void SetPointer(Window::Cursor c);
    void ResetPointer();
    void ReportScroll(bool vertical, int pos, int visible, int total);
    unsigned int MessageTime(const struct IntuiMessage *im) const;
    bool HandleKey(const struct IntuiMessage *im);
    bool HandleMouse(const struct IntuiMessage *im);
    bool HandleWheel(const struct IntuiMessage *im);
    bool HandleWheelKey(const struct IntuiMessage *im);
    void Wheel(int dx, int dy, UWORD qual);
    void HighlightBraces();
    void IndentNewLine();
    void OutdentCloseBrace();
    sptr_t Call(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) {
        return WndProc(static_cast<Message>(msg), wParam, lParam);
    }
};

ScintillaOS4::ScintillaOS4(struct ::Window *win_, SciViewHost *host_)
    : host(host_), viewWindow(*this) {
    wMain = static_cast<OS4Window *>(&viewWindow);
    displayPopupMenu = PopUp::Never;     // the host shows context menus
    view.bufferedDraw = false;           // the whole view is already double-buffered
    ULONG micros;
    IIntuition->CurrentTime(&timeBase, &micros);
    attach(win_);
}

ScintillaOS4::~ScintillaOS4() {
    detach();
    Finalise();
}

// --- SciView ---

void ScintillaOS4::attach(struct ::Window *win_) {
    detach();
    win = win_;
    if (!win)
        return;
    OS4SetCurrentScreen(win->WScreen);
    buffer.Allocate(width, height, win->WScreen->RastPort.BitMap);
    InvalidateStyleRedraw();
    Invalidate(GetClientRectangle());
}

void ScintillaOS4::detach() {
    if (!win)
        return;
    AutoCompleteCancel();
    ct.CallTipCancel();
    ResetPointer();
    buffer.Free();
    OS4ReleasePens(win->WScreen);
    win = nullptr;
}

void ScintillaOS4::setBounds(int l, int t, int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    const bool resized = w != width || h != height || !buffer.RP();
    left = l;
    top = t;
    width = w;
    height = h;
    if (resized && win) {
        OS4SetCurrentScreen(win->WScreen);
        buffer.Allocate(width, height, win->WScreen->RastPort.BitMap);
    }
    try {
        ChangeSize();
    } catch (...) {
        errorStatus = Status::Failure;
    }
    Invalidate(GetClientRectangle());
    Flush();
}

void ScintillaOS4::getBounds(int *l, int *t, int *w, int *h) const {
    if (l) *l = left;
    if (t) *t = top;
    if (w) *w = width;
    if (h) *h = height;
}

intptr_t ScintillaOS4::send(unsigned int msg, uintptr_t wParam, intptr_t lParam) {
    try {
        return WndProc(static_cast<Message>(msg), wParam, lParam);
    } catch (std::bad_alloc &) {
        errorStatus = Status::BadAlloc;
    } catch (...) {
        errorStatus = Status::Failure;
    }
    return 0;
}

bool ScintillaOS4::handleMessage(const struct IntuiMessage *im) {
    if (!win || im->IDCMPWindow != win)
        return false;
    bool used = false;
    try {
        switch (im->Class) {
        case IDCMP_RAWKEY:
            if (im->Code >= rawWheelUp && im->Code <= rawWheelRight)
                used = HandleWheelKey(im);
            else
                used = focused && HandleKey(im);
            break;
        case IDCMP_MOUSEBUTTONS:
        case IDCMP_MOUSEMOVE:
            used = HandleMouse(im);
            break;
        case IDCMP_EXTENDEDMOUSE:
            used = HandleWheel(im);
            break;
        default:
            break;
        }
    } catch (std::bad_alloc &) {
        errorStatus = Status::BadAlloc;
    } catch (...) {
        errorStatus = Status::Failure;
    }
    Flush();
    return used;
}

void ScintillaOS4::setFocus(bool focus) {
    if (focus) {
        focusLossPending = false;
        if (!focused) {
            focused = true;
            SetFocusState(true);
        }
    } else if (focused) {
        // Clicking a popup (a ToolBox window) makes Intuition deactivate and reactivate this
        // window; applying the loss on the next tick() lets the autocomplete list survive it.
        focusLossPending = true;
    }
    Flush();
}

void ScintillaOS4::ApplyFocusLoss() {
    focusLossPending = false;
    focused = false;
    AutoCompleteCancel();
    ct.CallTipCancel();
    SetFocusState(false);
}

void ScintillaOS4::tick() {
    try {
        if (focusLossPending)
            ApplyFocusLoss();
        const unsigned int now = OS4Milliseconds();
        for (int r = 0; r <= static_cast<int>(TickReason::platform); r++) {
            Ticker &t = tickers[r];
            if (t.running && static_cast<int>(now - t.due) >= 0) {
                t.due = now + t.period;
                TickFor(static_cast<TickReason>(r));
            }
        }
        if (idler.state && !Idle())
            idler.state = false;
    } catch (...) {
        errorStatus = Status::Failure;
    }
    Flush();
}

void ScintillaOS4::scrollTo(bool vertical, int pos) {
    try {
        if (vertical)
            ScrollTo(pos);
        else
            HorizontalScrollTo(pos);
    } catch (...) {
        errorStatus = Status::Failure;
    }
    Flush();
}

void ScintillaOS4::refresh() {
    Flush();
    Blit(GetClientRectangle());
}

// --- painting ---

void ScintillaOS4::Invalidate(PRectangle rc) {
    const PRectangle client = GetClientRectangle();
    rc = PRectangle(std::max(rc.left, client.left), std::max(rc.top, client.top),
                    std::min(rc.right, client.right), std::min(rc.bottom, client.bottom));
    if (rc.Empty())
        return;
    if (!isDirty) {
        dirty = rc;
        isDirty = true;
    } else {
        dirty = PRectangle(std::min(dirty.left, rc.left), std::min(dirty.top, rc.top),
                           std::max(dirty.right, rc.right), std::max(dirty.bottom, rc.bottom));
    }
}

void ScintillaOS4::Flush() {
    OS4PollPopups();
    if (!isDirty || !win || !buffer.RP())
        return;
    OS4SetCurrentScreen(win->WScreen);
    PRectangle rc = dirty;
    isDirty = false;
    try {
        for (int pass = 0; pass < 2; pass++) {
            rcPaint = rc;
            paintState = PaintState::painting;
            std::unique_ptr<Surface> surface = Surface::Allocate(technology);
            surface->Init(buffer.RP(), wMain.GetID());
            surface->SetMode(CurrentSurfaceMode());
            surface->SetClip(rc);
            Paint(surface.get(), rc);
            surface->PopClip();
            surface->Release();
            if (paintState != PaintState::abandoned)
                break;
            // Styling or brace highlighting reached outside the area: paint everything.
            rc = GetClientRectangle();
        }
    } catch (...) {
        errorStatus = Status::Failure;
    }
    paintState = PaintState::notPainting;
    Blit(rc);
}

void ScintillaOS4::Blit(PRectangle rc) {
    if (!win || !buffer.BM())
        return;
    const int x = std::max(0, static_cast<int>(rc.left)), y = std::max(0, static_cast<int>(rc.top));
    const int x1 = std::min(width, static_cast<int>(std::ceil(rc.right)));
    const int y1 = std::min(height, static_cast<int>(std::ceil(rc.bottom)));
    if (x1 > x && y1 > y)
        IGraphics->BltBitMapRastPort(buffer.BM(), x, y, win->RPort, left + x, top + y, x1 - x, y1 - y, 0xC0);
}

void ScintillaOS4::SetPointer(Window::Cursor c) {
    ULONG type = POINTERTYPE_NORMAL;
    switch (c) {
    case Window::Cursor::text: type = POINTERTYPE_TEXT; break;
    case Window::Cursor::hand: type = POINTERTYPE_HAND; break;
    case Window::Cursor::wait: type = POINTERTYPE_BUSY; break;
    case Window::Cursor::horizontal: type = POINTERTYPE_EASTWESTRESIZE; break;
    case Window::Cursor::vertical: type = POINTERTYPE_NORTHSOUTHRESIZE; break;
    default: break;
    }
    if (win && type != pointerType) {
        IIntuition->SetWindowPointer(win, WA_PointerType, type, TAG_END);
        pointerType = type;
    }
}

void ScintillaOS4::ResetPointer() {
    SetPointer(Window::Cursor::arrow);
    wMain = wMain.GetID();    // forget Window's cached cursor so re-entry sets it again
}

// --- scrolling ---

void ScintillaOS4::ReportScroll(bool vertical, int pos, int visible, int total) {
    int *last = lastScroll[vertical ? 0 : 1];
    if (last[0] == pos && last[1] == visible && last[2] == total)
        return;
    last[0] = pos;
    last[1] = visible;
    last[2] = total;
    if (host)
        host->sciScrollInfo(this, vertical, pos, visible, total);
}

void ScintillaOS4::SetVerticalScrollPos() {
    ReportScroll(true, static_cast<int>(topLine), static_cast<int>(LinesOnScreen()),
                 static_cast<int>(MaxScrollPos() + LinesOnScreen()));
}

void ScintillaOS4::SetHorizontalScrollPos() {
    const PRectangle rc = GetTextRectangle();
    ReportScroll(false, xOffset, static_cast<int>(rc.Width()), std::max(scrollWidth, xOffset + static_cast<int>(rc.Width())));
}

bool ScintillaOS4::ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) {
    ReportScroll(true, static_cast<int>(topLine), static_cast<int>(nPage), static_cast<int>(nMax + 1));
    SetHorizontalScrollPos();
    return false;
}

// --- clipboard (textclip.library: IFF FTXT on clipboard unit 0) ---

void ScintillaOS4::CopyToClipboard(const SelectionText &selectedText) {
    ITextClip->WriteClipVector(const_cast<STRPTR>(selectedText.Data()), selectedText.Length());
}

void ScintillaOS4::Copy() {
    if (!sel.Empty()) {
        SelectionText selectedText;
        CopySelectionRange(&selectedText);
        CopyToClipboard(selectedText);
    }
}

void ScintillaOS4::Paste() {
    STRPTR data = nullptr;
    ULONG size = 0;
    if (!ITextClip->ReadClipVector(&data, &size) || !data)
        return;
    std::string text(data, size);
    ITextClip->DisposeClipVector(data);
    if (convertPastes)
        text = Document::TransformLineEnds(text.c_str(), text.size(), pdoc->eolMode);
    UndoGroup ug(pdoc);
    ClearSelection(multiPasteMode == MultiPaste::Each);
    InsertPasteShape(text.c_str(), text.length(), PasteShape::stream);
    EnsureCaretVisible();
}

// --- input ---

unsigned int ScintillaOS4::MessageTime(const struct IntuiMessage *im) const {
    return (im->Seconds - timeBase) * 1000 + im->Micros / 1000;
}

bool ScintillaOS4::HandleKey(const struct IntuiMessage *im) {
    const UWORD code = im->Code;
    const UWORD qual = im->Qualifier;
    if (code & IECODE_UP_PREFIX)
        return false;
    if (code >= 0x60 && code <= 0x67)     // the qualifier keys themselves
        return false;
    APTR deadKeys = im->IAddress ? *static_cast<APTR *>(im->IAddress) : nullptr;
    auto unhandled = [&] {
        if (host)
            host->sciUnhandledKey(this, code, qual);
        return true;
    };
    // Amiga-key combinations are the IDE's (menu shortcuts and the like).
    if (qual & qualAmiga)
        return unhandled();
    const KeyMod mods = Modifiers(qual);
    bool consumed = false;
    if (const std::optional<Keys> key = SpecialKey(code)) {
        KeyDownWithModifiers(*key, mods, &consumed);
        return consumed || unhandled();
    }
    char buf[8];
    if (qual & IEQUALIFIER_CONTROL) {
        // Ctrl with the keypad operators: zoom in/out/reset.
        const std::optional<Keys> keypad = code == 0x5E ? Keys::Add : code == 0x4A ? Keys::Subtract
                                         : code == 0x5C ? Keys::Divide : std::optional<Keys>();
        if (keypad) {
            KeyDownWithModifiers(*keypad, mods, &consumed);
            return consumed || unhandled();
        }
        // Ctrl+letter: Scintilla's key map wants the unshifted letter in upper case.
        const int n = MapKey(code, qual & ~(IEQUALIFIER_CONTROL | qualAlt | qualShift | IEQUALIFIER_CAPSLOCK),
                             deadKeys, buf, sizeof(buf));
        if (n == 1 && buf[0] > ' ' && buf[0] < 0x7F) {
            KeyDownWithModifiers(static_cast<Keys>(std::toupper(static_cast<unsigned char>(buf[0]))), mods, &consumed);
            if (consumed)
                return true;
        }
        return unhandled();
    }
    const int n = MapKey(code, qual, deadKeys, buf, sizeof(buf));
    if (n <= 0)
        return n == 0 ? true : unhandled();   // 0: a dead key, waiting for the next one
    const unsigned char ch = buf[0];
    if (n == 1 && ((ch >= 0x20 && ch < 0x7F) || ch >= 0xA0)) {
        InsertCharacter(std::string_view(buf, 1), CharacterSource::DirectInput);
        return true;
    }
    return unhandled();       // function keys, Help... (CSI sequences)
}

bool ScintillaOS4::HandleMouse(const struct IntuiMessage *im) {
    const Point pt(im->MouseX - left, im->MouseY - top);
    const bool inside = pt.x >= 0 && pt.y >= 0 && pt.x < width && pt.y < height;
    const KeyMod mods = Modifiers(im->Qualifier);
    const unsigned int when = MessageTime(im);
    if (im->Class == IDCMP_MOUSEMOVE) {
        if (dragging || inside) {
            ButtonMoveWithModifiers(pt, when, mods);
            return true;
        }
        ResetPointer();
        return false;
    }
    switch (im->Code) {
    case SELECTDOWN:
        if (OS4PopupAt(win->LeftEdge + im->MouseX, win->TopEdge + im->MouseY))
            return true;
        if (!inside)
            return false;
        if (!focused && host)
            host->sciWantFocus(this);
        dragging = true;
        ButtonDownWithModifiers(pt, when, mods);
        return true;
    case SELECTUP:
        if (!dragging) {
            if (OS4PopupClick(win->LeftEdge + im->MouseX, win->TopEdge + im->MouseY, OS4Milliseconds()))
                return true;
            return inside;
        }
        dragging = false;
        ButtonUpWithModifiers(pt, when, mods);
        return true;
    case MENUDOWN:
        if (!inside)
            return false;
        if (!focused && host)
            host->sciWantFocus(this);
        RightButtonDownWithModifiers(pt, when, mods);
        // Like most editors: a right click outside the selection moves the caret there.
        if (!PointInSelMargin(pt) && !PointInSelection(pt))
            SetEmptySelection(PositionFromLocation(pt));
        if (host)
            host->sciContextMenu(this, im->MouseX, im->MouseY);
        return true;
    case MENUUP:
        return inside;
    default:
        return inside;
    }
}

bool ScintillaOS4::HandleWheel(const struct IntuiMessage *im) {
    if (im->Code != IMSGCODE_INTUIWHEELDATA || !im->IAddress)
        return false;
    const int x = im->MouseX - left, y = im->MouseY - top;
    if (x < 0 || y < 0 || x >= width || y >= height)
        return false;
    const struct IntuiWheelData *wd = static_cast<const struct IntuiWheelData *>(im->IAddress);
    Wheel(wd->WheelX, wd->WheelY, im->Qualifier);
    return true;
}

bool ScintillaOS4::HandleWheelKey(const struct IntuiMessage *im) {
    const int x = im->MouseX - left, y = im->MouseY - top;
    if (x < 0 || y < 0 || x >= width || y >= height)
        return false;
    switch (im->Code) {
    case rawWheelUp: Wheel(0, -1, im->Qualifier); break;
    case rawWheelDown: Wheel(0, 1, im->Qualifier); break;
    case rawWheelLeft: Wheel(-1, 0, im->Qualifier); break;
    case rawWheelRight: Wheel(1, 0, im->Qualifier); break;
    default: break;
    }
    return true;
}

// dy > 0 scrolls towards the end of the document; Ctrl+wheel zooms.
void ScintillaOS4::Wheel(int dx, int dy, UWORD qual) {
    if (dy) {
        if (qual & IEQUALIFIER_CONTROL)
            KeyCommand(dy < 0 ? Message::ZoomIn : Message::ZoomOut);
        else
            ScrollTo(topLine + 3 * dy);
    }
    if (dx)
        HorizontalScrollTo(std::max(0, xOffset + dx * 4 * static_cast<int>(vs.aveCharWidth)));
}

// --- timers ---

void ScintillaOS4::FineTickerStart(TickReason reason, int millis, int) {
    Ticker &t = tickers[static_cast<int>(reason)];
    t.running = true;
    t.period = std::max(millis, 1);
    t.due = OS4Milliseconds() + t.period;
}

// --- notifications, brace matching, auto-indent ---

void ScintillaOS4::NotifyParent(NotificationData scn) {
    scn.nmhdr.hwndFrom = static_cast<SciView *>(this);
    scn.nmhdr.idFrom = GetCtrlID();
    if (scn.nmhdr.code == Notification::UpdateUI && braceMatch)
        HighlightBraces();
    if (scn.nmhdr.code == Notification::CharAdded && autoIndent) {
        if (scn.ch == '\n' || (scn.ch == '\r' && pdoc->eolMode == EndOfLine::Cr))
            IndentNewLine();
        else if (scn.ch == '}')
            OutdentCloseBrace();
    }
    if (host)
        host->sciNotify(this, scn);
}

void ScintillaOS4::HighlightBraces() {
    auto isBrace = [](char c) { return c && std::strchr("()[]{}", c); };
    const Sci::Position caret = sel.MainCaret();
    Sci::Position pos = -1;
    if (caret > 0 && isBrace(pdoc->CharAt(caret - 1)))
        pos = caret - 1;
    else if (isBrace(pdoc->CharAt(caret)))
        pos = caret;
    if (pos < 0) {
        Call(SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
        return;
    }
    const sptr_t match = Call(SCI_BRACEMATCH, pos, 0);
    if (match < 0)
        Call(SCI_BRACEBADLIGHT, pos);
    else
        Call(SCI_BRACEHIGHLIGHT, pos, match);
}

void ScintillaOS4::IndentNewLine() {
    const Sci::Line line = pdoc->SciLineFromPosition(sel.MainCaret());
    if (line <= 0 || !sel.Empty())
        return;
    int indent = pdoc->GetLineIndentation(line - 1);
    // One level more after a line ending in '{'.
    Sci::Position p = pdoc->LineEnd(line - 1);
    const Sci::Position start = pdoc->LineStart(line - 1);
    while (p > start && (pdoc->CharAt(p - 1) == ' ' || pdoc->CharAt(p - 1) == '\t'))
        p--;
    if (p > start && pdoc->CharAt(p - 1) == '{')
        indent += pdoc->IndentSize();
    if (indent > 0) {
        Call(SCI_SETLINEINDENTATION, line, indent);
        Call(SCI_GOTOPOS, Call(SCI_GETLINEINDENTPOSITION, line));
    }
}

void ScintillaOS4::OutdentCloseBrace() {
    const Sci::Position caret = sel.MainCaret();
    const Sci::Line line = pdoc->SciLineFromPosition(caret);
    // Only when the '}' is the first thing on the line.
    if (Call(SCI_GETLINEINDENTPOSITION, line) != caret - 1)
        return;
    const sptr_t match = Call(SCI_BRACEMATCH, caret - 1, 0);
    if (match < 0)
        return;
    const int indent = pdoc->GetLineIndentation(pdoc->SciLineFromPosition(match));
    Call(SCI_SETLINEINDENTATION, line, indent);
    Call(SCI_GOTOPOS, Call(SCI_GETLINEINDENTPOSITION, line) + 1);
}

sptr_t ScintillaOS4::WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) {
    switch (static_cast<unsigned int>(iMessage)) {
    case AF_SCI_SETBRACEMATCH:
        braceMatch = wParam != 0;
        if (!braceMatch)
            Call(SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
        return 0;
    case AF_SCI_SETAUTOINDENT:
        autoIndent = wParam != 0;
        return 0;
    case SCI_GRABFOCUS:
        if (host)
            host->sciWantFocus(this);
        return 0;
    default:
        return ScintillaBase::WndProc(iMessage, wParam, lParam);
    }
}

// --- text encoding: the document is Latin-1 ---

std::string ScintillaOS4::UTF8FromEncoded(std::string_view encoded) const {
    if (IsUnicodeMode())
        return std::string(encoded);
    std::string out;
    out.reserve(encoded.size());
    for (unsigned char c : encoded) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::string ScintillaOS4::EncodedFromUTF8(std::string_view utf8) const {
    if (IsUnicodeMode())
        return std::string(utf8);
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ) {
        const unsigned char c = utf8[i];
        const int len = c < 0x80 ? 1 : std::max(1, static_cast<int>(UTF8BytesOfLead[c]));
        unsigned int cp = c;
        if (len == 2 && i + 1 < utf8.size())
            cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
        else if (len > 1)
            cp = '?';
        out.push_back(cp < 0x100 ? static_cast<char>(cp) : '?');
        i += len;
    }
    return out;
}

// --- calltips ---

void ScintillaOS4::CreateCallTipWindow(PRectangle) {
    if (ct.wCallTip.Created() || !win)
        return;
    ct.wCallTip = OS4CreatePopup(win->WScreen,
        [this](Surface &s, PRectangle) { ct.PaintCT(&s); },
        [this](Point pt, unsigned int) {
            ct.MouseClick(pt);
            CallTipClick();
        });
}

SciView *SciView::create(struct ::Window *win, SciViewHost *host) {
    try {
        return new ScintillaOS4(win, host);
    } catch (...) {
        return nullptr;
    }
}

} // namespace af
