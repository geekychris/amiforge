// The hex editor: a binary file shown as "offset | hex | text" lines in a read-only
// Scintilla document. Typing goes to the bytes (overtype): hex digits in the hex columns,
// any character in the text column; the line is formatted again from the bytes. Saving
// writes the bytes back exactly. The formatting is HexData.cpp's (tested on the host).
#include "App.h"
#include "Commands.h"
#include "HexData.h"
#include "Integrations.h"

#include <string.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <devices/inputevent.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }
static intptr_t bgr(uint32_t rgb) { return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF); }
const int INDIC_HEX_TWIN = 12;

Document *App::openHex(const std::string &path, const std::string &data)
{
    if (Document *d = findDocument(path)) {
        if (d->hex) { activate(d); return d; }
        if (d->modified && ask("Open as Hex", d->title + " has unsaved changes. Open the saved file as hex anyway?",
                               "_Open|_Cancel") != 1) return nullptr;
        closeDocument(d);
    }
    auto d = std::make_unique<Document>();
    d->path = path;
    d->title = fileName(path) + " (hex)";
    d->hex = true;
    d->bytes = data;
    d->lexerName = "hexview.txt";
    std::string text = hex::formatAll(data);
    d->sciDoc = (void *)S(editor, SCI_CREATEDOCUMENT, text.size() + 1024, SC_DOCUMENTOPTION_DEFAULT);
    Document *p = d.get();
    docs.push_back(std::move(d));
    activate(p);
    S(editor, SCI_SETREADONLY, 0);
    S(editor, SCI_SETUNDOCOLLECTION, 0);
    S(editor, SCI_SETTEXT, 0, (intptr_t)text.c_str());
    S(editor, SCI_SETSAVEPOINT);
    S(editor, SCI_SETREADONLY, 1);
    S(editor, SCI_GOTOPOS, hex::kHexCol);
    p->modified = false;
    integrationsActivated(p);
    lastDir = dirName(path);
    rebuildTabs();
    updateTitle();
    setStatus(strprintf("%s: %lu bytes. Type hex digits (or text in the right column) to change bytes; "
                        "Amiga-L goes to an offset, Amiga-F finds bytes.", fileName(path).c_str(), (unsigned long)data.size()));
    return p;
}

// Re-format the line(s) holding `offset` and put the caret at (offset, area, nibble).
void App::hexRender(Document *d, intptr_t offset, int area, int nibble)
{
    if (!d || d != cur) return;
    size_t lines = hex::lineCount(d->bytes.size());
    intptr_t docLines = S(editor, SCI_GETLINECOUNT);
    S(editor, SCI_SETREADONLY, 0);
    if ((intptr_t)lines != docLines) {
        intptr_t first = S(editor, SCI_GETFIRSTVISIBLELINE);
        std::string text = hex::formatAll(d->bytes);
        S(editor, SCI_SETTEXT, 0, (intptr_t)text.c_str());
        S(editor, SCI_SETFIRSTVISIBLELINE, first);
    } else {
        intptr_t line = offset / hex::kBytesPerLine;
        // the byte just edited may be the last of the previous line
        for (intptr_t l = std::max<intptr_t>(0, line - 1); l <= line && l < docLines; l++) {
            std::string t = hex::formatLine(d->bytes, (size_t)l * hex::kBytesPerLine);
            S(editor, SCI_SETTARGETRANGE, S(editor, SCI_POSITIONFROMLINE, l), S(editor, SCI_GETLINEENDPOSITION, l));
            S(editor, SCI_REPLACETARGET, t.size(), (intptr_t)t.c_str());
        }
    }
    S(editor, SCI_SETREADONLY, 1);
    intptr_t line = offset / hex::kBytesPerLine;
    int col = hex::columnOf((int)(offset % hex::kBytesPerLine), area == 1 ? hex::Area::Text : hex::Area::Hex, nibble);
    intptr_t pos = S(editor, SCI_POSITIONFROMLINE, line) + col;
    S(editor, SCI_GOTOPOS, pos);
    d->hexNibble = nibble;
    bool was = d->modified;
    d->modified = true;
    if (!was) { rebuildTabs(); updateTitle(); }
    hexHighlightTwin();
}

// Where the caret is: byte offset, area (0 hex, 1 text), nibble.
static bool caretByte(SciView *ed, Document *d, size_t &offset, hex::Hit &hit)
{
    intptr_t pos = S(ed, SCI_GETCURRENTPOS);
    intptr_t line = S(ed, SCI_LINEFROMPOSITION, pos);
    int col = (int)(pos - S(ed, SCI_POSITIONFROMLINE, line));
    hit = hex::hitTest(col);
    offset = (size_t)line * hex::kBytesPerLine + hit.byte;
    if (offset > d->bytes.size()) offset = d->bytes.size();
    return true;
}

bool App::hexKey(struct IntuiMessage *im)
{
    if (!cur || !cur->hex || !editor) return false;
    uint16 qual = im->Qualifier;
    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND | IEQUALIFIER_CONTROL)) return false;   // shortcuts
    char buf[8];
    struct InputEvent ie = {};
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = im->Code;
    ie.ie_Qualifier = qual;
    ie.ie_EventAddress = im->IAddress ? *(APTR *)im->IAddress : nullptr;
    int n = IKeymap->MapRawKey(&ie, buf, sizeof buf, nullptr);
    if (n != 1) return false;
    unsigned char ch = buf[0];
    if (ch < 0x20 || ch == 0x7F || (ch >= 0x80 && ch < 0xA0)) return false;   // Return, Tab, Del, cursor keys...
    size_t off;
    hex::Hit hit;
    caretByte(editor, cur, off, hit);
    int nib = hit.area == hex::Area::Hex ? hit.nibble : 0;
    int old;
    size_t before = off;
    if (!hex::typeChar(cur->bytes, off, hit.area, nib, ch, old)) {
        IIntuition->DisplayBeep(nullptr);
        return true;
    }
    cur->hexUndo.push_back({ before, old });
    hexRender(cur, (intptr_t)off, hit.area == hex::Area::Text ? 1 : 0, nib);
    return true;
}

void App::hexUndoLast()
{
    if (!cur || !cur->hex || cur->hexUndo.empty()) return;
    auto u = cur->hexUndo.back();
    cur->hexUndo.pop_back();
    if (u.second < 0) cur->bytes.erase(u.first, 1);
    else cur->bytes[u.first] = (char)u.second;
    hexRender(cur, (intptr_t)u.first, 0, 0);
    if (cur->hexUndo.empty()) {
        // back to what is on disk?
        std::string disk;
        if (readFile(cur->path, disk) && disk == cur->bytes) {
            cur->modified = false;
            rebuildTabs();
            updateTitle();
        }
    }
}

// Underline the other column's view of the byte at the caret.
void App::hexHighlightTwin()
{
    if (!cur || !cur->hex || !editor) return;
    S(editor, SCI_INDICSETSTYLE, INDIC_HEX_TWIN, INDIC_ROUNDBOX);
    S(editor, SCI_INDICSETFORE, INDIC_HEX_TWIN, bgr(0x2060FF));
    S(editor, SCI_INDICSETALPHA, INDIC_HEX_TWIN, 90);
    S(editor, SCI_SETINDICATORCURRENT, INDIC_HEX_TWIN);
    S(editor, SCI_INDICATORCLEARRANGE, 0, S(editor, SCI_GETLENGTH));
    size_t off;
    hex::Hit hit;
    caretByte(editor, cur, off, hit);
    if (off >= cur->bytes.size()) return;
    intptr_t ls = S(editor, SCI_POSITIONFROMLINE, off / hex::kBytesPerLine);
    int b = (int)(off % hex::kBytesPerLine);
    S(editor, SCI_INDICATORFILLRANGE, ls + hex::columnOf(b, hex::Area::Hex, 0), 2);
    S(editor, SCI_INDICATORFILLRANGE, ls + hex::columnOf(b, hex::Area::Text, 0), 1);
    setStatus(strprintf("Offset 0x%lX (%lu): byte 0x%02X = %u", (unsigned long)off, (unsigned long)off,
                        (unsigned)(unsigned char)cur->bytes[off], (unsigned)(unsigned char)cur->bytes[off]));
}

void App::hexGoto()
{
    if (!cur || !cur->hex) return;
    std::string s;
    if (!askString("Go to Offset", "Offset (hex like 1A2B or 0x1A2B; decimal as #1234):", s)) return;
    size_t off;
    if (!hex::parseOffset(s, off)) { info("That isn't an offset: " + s); return; }
    if (off > cur->bytes.size()) off = cur->bytes.size();
    intptr_t line = off / hex::kBytesPerLine;
    intptr_t pos = S(editor, SCI_POSITIONFROMLINE, line) + hex::columnOf((int)(off % hex::kBytesPerLine), hex::Area::Hex, 0);
    S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, line);
    S(editor, SCI_GOTOPOS, pos);
    hexHighlightTwin();
}

void App::hexFind(bool again)
{
    if (!cur || !cur->hex) return;
    if (!again || hexLastFind.empty()) {
        std::string s = hexLastFind;
        if (!askString("Find Bytes", "Bytes in hex (4E 75) or text (FORM; \"4E75\" in quotes for text that looks like hex):", s))
            return;
        if (s.empty()) return;
        hexLastFind = s;
    }
    std::string pat = hex::parsePattern(hexLastFind);
    size_t off;
    hex::Hit hit;
    caretByte(editor, cur, off, hit);
    size_t r = hex::find(cur->bytes, pat, again ? off + 1 : off);
    if (r == std::string::npos) {
        setStatus("Not found: " + hexLastFind);
        IIntuition->DisplayBeep(nullptr);
        return;
    }
    intptr_t line = r / hex::kBytesPerLine;
    intptr_t pos = S(editor, SCI_POSITIONFROMLINE, line) + hex::columnOf((int)(r % hex::kBytesPerLine), hex::Area::Hex, 0);
    S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, line);
    S(editor, SCI_GOTOPOS, pos);
    hexHighlightTwin();
    setStatus(strprintf("Found at offset 0x%lX (%lu)%s", (unsigned long)r, (unsigned long)r, r < off ? " - wrapped around" : ""));
}

} // namespace af
