// Documents: one SciView shows one Scintilla document at a time; each open file has its own
// document (SCI_CREATEDOCUMENT) and a clicktab tab. Markers live in the documents, but the
// IDE keeps the model (breakpoints, where execution stopped, diagnostics) and redraws the
// markers of the document being shown.
#include "App.h"
#include "Commands.h"

#include <string.h>
#include <ctype.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include "Scintilla.h"
#include "ScintillaTypes.h"
#include "ScintillaStructures.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

Document *App::findDocument(const std::string &path)
{
    for (auto &d : docs)
        if (!d->path.empty() && sameFile(d->path, path)) return d.get();
    return nullptr;
}

static std::string tabTitle(const Document *d)
{
    return d->title + (d->modified ? " *" : "");
}

void App::rebuildTabs()
{
    if (!gad[GID_TABS]) return;
    if (win) IIntuition->SetGadgetAttrs((struct Gadget *)gad[GID_TABS], win, nullptr, CLICKTAB_Labels, ~0UL, TAG_DONE);
    struct Node *n;
    while ((n = IExec->RemHead(tabLabels))) IClickTab->FreeClickTabNode(n);
    int curIndex = 0, i = 0;
    for (auto &d : docs) {
        d->tabText = tabTitle(d.get());
        d->tabNode = IClickTab->AllocClickTabNode(TNA_Text, (uint32)d->tabText.c_str(), TNA_Number, i,
                                                  TNA_CloseGadget, TRUE, TAG_DONE);
        if (d->tabNode) IExec->AddTail(tabLabels, d->tabNode);
        if (d.get() == cur) curIndex = i;
        i++;
    }
    if (win)
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gad[GID_TABS], win, nullptr,
            CLICKTAB_Labels, (uint32)tabLabels, CLICKTAB_Current, curIndex, TAG_DONE);
}

Document *App::newDocument()
{
    auto d = std::make_unique<Document>();
    d->untitledNo = ++untitledCounter;
    d->title = strprintf("Untitled %d", d->untitledNo);
    d->sciDoc = (void *)S(editor, SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    Document *p = d.get();
    docs.push_back(std::move(d));
    activate(p);
    return p;
}

Document *App::openFile(const std::string &pathIn, bool doActivate)
{
    std::string path = absolutePath(pathIn);
    if (Document *d = findDocument(path)) {
        if (doActivate) activate(d);
        return d;
    }
    std::string text;
    if (!readFile(path, text)) {
        info("Can't open " + path);
        return nullptr;
    }
    auto d = std::make_unique<Document>();
    d->path = path;
    d->title = fileName(path);
    d->sciDoc = (void *)S(editor, SCI_CREATEDOCUMENT, text.size() + 1024, SC_DOCUMENTOPTION_DEFAULT);
    Document *p = d.get();
    docs.push_back(std::move(d));
    activate(p);
    S(editor, SCI_SETREADONLY, 0);
    S(editor, SCI_SETEOLMODE, text.find("\r\n") != std::string::npos ? SC_EOL_CRLF : SC_EOL_LF);
    S(editor, SCI_SETTEXT, 0, (intptr_t)text.c_str());
    S(editor, SCI_EMPTYUNDOBUFFER);
    S(editor, SCI_SETSAVEPOINT);
    S(editor, SCI_GOTOPOS, 0);
    p->modified = false;
    lastDir = dirName(path);
    refreshMarkers();
    rebuildTabs();
    updateTitle();
    if (!doActivate && docs.size() > 1) activate(docs[docs.size() - 2].get());
    return p;
}

void App::activate(Document *d)
{
    if (!editor) return;
    if (cur && cur != d) {
        harvestBreakpoints(cur);
        cur->firstLine = S(editor, SCI_GETFIRSTVISIBLELINE);
        cur->caret = S(editor, SCI_GETCURRENTPOS);
        cur->anchor = S(editor, SCI_GETANCHOR);
        cur->xOffset = S(editor, SCI_GETXOFFSET);
    }
    if (!d) {
        // nothing open: show an empty, read-only document
        cur = nullptr;
        S(editor, SCI_SETDOCPOINTER, 0, 0);
        S(editor, SCI_SETREADONLY, 1);
        rebuildTabs();
        updateTitle();
        updateCaretStatus();
        return;
    }
    bool switching = cur != d;
    cur = d;
    if (switching) {
        S(editor, SCI_SETDOCPOINTER, 0, (intptr_t)d->sciDoc);
        sciSetupSourceEditor(editor, d->path.empty() ? "untitled.c" : d->path.c_str(), fontSize);
        S(editor, SCI_SETWRAPMODE, wrapLines ? SC_WRAP_WORD : SC_WRAP_NONE);
        S(editor, SCI_SETVIEWWS, showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
        S(editor, SCI_SETREADONLY, 0);
        S(editor, SCI_SETSEL, d->anchor, d->caret);
        S(editor, SCI_SETFIRSTVISIBLELINE, d->firstLine);
        S(editor, SCI_SETXOFFSET, d->xOffset);
        refreshMarkers();
    }
    rebuildTabs();
    updateTitle();
    updateCaretStatus();
}

bool App::saveDocument(Document *d, bool saveAs)
{
    if (!d) return false;
    if (d != cur) activate(d);
    std::string path = d->path;
    if (saveAs || path.empty()) {
        path = requestFile("Save File As", true, path.empty() ? joinPath(project.path.empty() ? lastDir : project.dir, d->title + ".c") : path);
        if (path.empty()) return false;
        path = absolutePath(path);
    }
    intptr_t len = S(editor, SCI_GETLENGTH);
    std::string text(len, '\0');
    std::vector<char> buf(len + 1);
    S(editor, SCI_GETTEXT, len + 1, (intptr_t)buf.data());
    text.assign(buf.data(), len);
    if (!writeFile(path, text)) {
        info("Can't write " + path);
        return false;
    }
    bool renamed = d->path != path;
    d->path = path;
    d->title = fileName(path);
    S(editor, SCI_SETSAVEPOINT);
    d->modified = false;
    if (renamed) sciSetupSourceEditor(editor, path.c_str(), fontSize);
    rebuildTabs();
    updateTitle();
    setStatus("Saved " + path);
    return true;
}

bool App::saveAll()
{
    Document *was = cur;
    for (auto &d : docs)
        if (d->modified && !saveDocument(d.get())) return false;
    if (was && was != cur) activate(was);
    return true;
}

bool App::closeDocument(Document *d)
{
    if (!d) return true;
    if (d->modified) {
        if (d != cur) activate(d);
        int r = ask("Unsaved Changes", "Save the changes to " + d->title + "?", "_Save|_Don't Save|_Cancel");
        if (r == 0) return false;
        if (r == 1 && !saveDocument(d)) return false;
    }
    harvestBreakpoints(d);
    auto it = std::find_if(docs.begin(), docs.end(), [d](const std::unique_ptr<Document> &p) { return p.get() == d; });
    size_t idx = it - docs.begin();
    void *sciDoc = d->sciDoc;
    if (d == cur) {
        Document *next = nullptr;
        if (docs.size() > 1) next = docs[idx + 1 < docs.size() ? idx + 1 : idx - 1].get();
        cur = nullptr;   // don't harvest a document that's going away
        if (next) activate(next);
        else activate(nullptr);
    }
    docs.erase(it);
    S(editor, SCI_RELEASEDOCUMENT, 0, (intptr_t)sciDoc);
    rebuildTabs();
    updateTitle();
    return true;
}

bool App::closeAll()
{
    while (!docs.empty())
        if (!closeDocument(docs.back().get())) return false;
    return true;
}

void App::gotoLocation(const std::string &file, int line, int col, bool select)
{
    Document *d = findDocument(file);
    if (!d) d = fileExists(file) ? openFile(file) : nullptr;
    if (!d) {
        setStatus("Can't find " + file);
        return;
    }
    if (d != cur) activate(d);
    if (line <= 0) return;
    intptr_t l = line - 1;
    S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, l);
    intptr_t pos = col > 0 ? S(editor, SCI_FINDCOLUMN, l, col - 1) : S(editor, SCI_POSITIONFROMLINE, l);
    if (select) S(editor, SCI_GOTOPOS, pos);
    // keep the line away from the window edges
    intptr_t first = S(editor, SCI_GETFIRSTVISIBLELINE), vis = S(editor, SCI_LINESONSCREEN);
    intptr_t dl = S(editor, SCI_VISIBLEFROMDOCLINE, l);
    if (dl < first + 3 || dl > first + vis - 4) S(editor, SCI_SETFIRSTVISIBLELINE, dl > vis / 3 ? dl - vis / 3 : 0);
    editorFocus = true;
    editor->setFocus(true);
}

void App::updateCaretStatus()
{
    static std::string text;
    if (!cur) text = "";
    else {
        intptr_t pos = S(editor, SCI_GETCURRENTPOS);
        intptr_t line = S(editor, SCI_LINEFROMPOSITION, pos);
        intptr_t col = S(editor, SCI_GETCOLUMN, pos);
        text = strprintf("Ln %ld, Col %ld%s", (long)line + 1, (long)col + 1, S(editor, SCI_GETOVERTYPE) ? "  OVR" : "");
    }
    setGadget(GID_STATUS, GA_Text, (uint32)text.c_str());
}

// --- markers ---

void App::harvestBreakpoints(Document *d)
{
    if (!d || d != cur || d->path.empty() || !editor) return;
    std::string rel = project.rel(d->path);
    // the breakpoints of this file, in line order, and the marker lines in the same order
    std::vector<int> idx;
    for (size_t i = 0; i < project.breakpoints.size(); i++)
        if (sameFile(project.abs(project.breakpoints[i].file), d->path)) idx.push_back((int)i);
    std::sort(idx.begin(), idx.end(), [this](int a, int b) { return project.breakpoints[a].line < project.breakpoints[b].line; });
    std::vector<int> lines;
    int mask = (1 << AF_MARKER_BREAKPOINT) | (1 << AF_MARKER_BREAKPOINT_OFF);
    for (intptr_t l = S(editor, SCI_MARKERNEXT, 0, mask); l >= 0; l = S(editor, SCI_MARKERNEXT, l + 1, mask))
        lines.push_back((int)l + 1);
    if (lines.size() != idx.size()) return;   // edited away; keep the model as it is
    for (size_t k = 0; k < idx.size(); k++) project.breakpoints[idx[k]].line = lines[k];
}

void App::refreshMarkers()
{
    if (!editor || !cur) return;
    for (int m : { AF_MARKER_BREAKPOINT, AF_MARKER_BREAKPOINT_OFF, AF_MARKER_CURRENT_LINE,
                   AF_MARKER_CURRENT_BG, AF_MARKER_FRAME, AF_MARKER_ERROR, AF_MARKER_WARNING })
        S(editor, SCI_MARKERDELETEALL, m);
    if (cur->path.empty()) return;
    for (auto &b : project.breakpoints)
        if (sameFile(project.abs(b.file), cur->path))
            S(editor, SCI_MARKERADD, b.line - 1, b.enabled ? AF_MARKER_BREAKPOINT : AF_MARKER_BREAKPOINT_OFF);
    for (auto &dl : diagnostics)
        if (dl.line > 0 && !dl.file.empty() && (dl.kind == MsgKind::Error || dl.kind == MsgKind::Warning) &&
            sameFile(dl.file, cur->path))
            S(editor, SCI_MARKERADD, dl.line - 1, dl.kind == MsgKind::Error ? AF_MARKER_ERROR : AF_MARKER_WARNING);
    if (execLine > 0 && !execFile.empty() && sameFile(execFile, cur->path)) {
        S(editor, SCI_MARKERADD, execLine - 1, AF_MARKER_CURRENT_LINE);
        S(editor, SCI_MARKERADD, execLine - 1, AF_MARKER_CURRENT_BG);
    }
    if (frameLine > 0 && !frameFile.empty() && sameFile(frameFile, cur->path))
        S(editor, SCI_MARKERADD, frameLine - 1, AF_MARKER_FRAME);
}

void App::clearMarkers(int marker)
{
    S(editor, SCI_MARKERDELETEALL, marker);
}

void App::markErrors() { refreshMarkers(); }

void App::toggleBreakpointAtCaret()
{
    if (!cur) return;
    intptr_t line = S(editor, SCI_LINEFROMPOSITION, S(editor, SCI_GETCURRENTPOS));
    toggleBreakpoint(cur, (int)line + 1);
}

void App::toggleBreakpoint(Document *d, int line)
{
    if (!d) return;
    if (d->path.empty()) {
        info("Save the file before setting breakpoints in it.");
        return;
    }
    if (d == cur) harvestBreakpoints(d);
    std::string rel = project.rel(d->path);
    for (size_t i = 0; i < project.breakpoints.size(); i++) {
        auto &b = project.breakpoints[i];
        if (b.line == line && sameFile(project.abs(b.file), d->path)) {
            project.breakpoints.erase(project.breakpoints.begin() + i);
            syncDebuggerBreakpoints();
            if (d == cur) refreshMarkers();
            refreshBreakpointList();
            return;
        }
    }
    project.breakpoints.push_back({rel, line, true, ""});
    syncDebuggerBreakpoints();
    if (d == cur) refreshMarkers();
    refreshBreakpointList();
}

void App::syncBreakpointMarkers(Document *d)
{
    if (d == cur) refreshMarkers();
}

// --- search ---

void App::findNext(bool backwards)
{
    if (!cur || findText.empty()) {
        if (findText.empty()) openFindWindow(false);
        return;
    }
    int flags = (findCase ? SCFIND_MATCHCASE : 0) | (findWord ? SCFIND_WHOLEWORD : 0) |
                (findRegex ? SCFIND_REGEXP | SCFIND_CXX11REGEX : 0);
    S(editor, SCI_SETSEARCHFLAGS, flags);
    intptr_t len = S(editor, SCI_GETLENGTH);
    intptr_t selStart = S(editor, SCI_GETSELECTIONSTART), selEnd = S(editor, SCI_GETSELECTIONEND);
    auto search = [&](intptr_t from, intptr_t to) {
        S(editor, SCI_SETTARGETRANGE, from, to);
        return S(editor, SCI_SEARCHINTARGET, findText.size(), (intptr_t)findText.c_str());
    };
    intptr_t r = backwards ? search(selStart, 0) : search(selEnd, len);
    bool wrapped = false;
    if (r < 0) {
        r = backwards ? search(len, selEnd) : search(0, selStart);
        wrapped = true;
    }
    if (r < 0) {
        setStatus("\"" + findText + "\" not found");
        IIntuition->DisplayBeep(nullptr);
        return;
    }
    intptr_t ts = S(editor, SCI_GETTARGETSTART), te = S(editor, SCI_GETTARGETEND);
    S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, S(editor, SCI_LINEFROMPOSITION, ts));
    S(editor, SCI_SETSEL, ts, te);
    S(editor, SCI_SCROLLCARET);
    setStatus(wrapped ? "Search wrapped around" : "");
}

void App::replaceOne()
{
    if (!cur || findText.empty()) return;
    intptr_t a = S(editor, SCI_GETSELECTIONSTART), b = S(editor, SCI_GETSELECTIONEND);
    if (a != b) {
        int flags = (findCase ? SCFIND_MATCHCASE : 0) | (findWord ? SCFIND_WHOLEWORD : 0) |
                    (findRegex ? SCFIND_REGEXP | SCFIND_CXX11REGEX : 0);
        S(editor, SCI_SETSEARCHFLAGS, flags);
        S(editor, SCI_SETTARGETRANGE, a, b);
        if (S(editor, SCI_SEARCHINTARGET, findText.size(), (intptr_t)findText.c_str()) == a &&
            S(editor, SCI_GETTARGETEND) == b) {
            S(editor, findRegex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, replaceText.size(), (intptr_t)replaceText.c_str());
            S(editor, SCI_SETSEL, S(editor, SCI_GETTARGETEND), S(editor, SCI_GETTARGETEND));
        }
    }
    findNext(false);
}

int App::replaceAll()
{
    if (!cur || findText.empty()) return 0;
    int flags = (findCase ? SCFIND_MATCHCASE : 0) | (findWord ? SCFIND_WHOLEWORD : 0) |
                (findRegex ? SCFIND_REGEXP | SCFIND_CXX11REGEX : 0);
    S(editor, SCI_SETSEARCHFLAGS, flags);
    S(editor, SCI_BEGINUNDOACTION);
    int n = 0;
    intptr_t pos = 0;
    for (;;) {
        S(editor, SCI_SETTARGETRANGE, pos, S(editor, SCI_GETLENGTH));
        if (S(editor, SCI_SEARCHINTARGET, findText.size(), (intptr_t)findText.c_str()) < 0) break;
        intptr_t start = S(editor, SCI_GETTARGETSTART);
        S(editor, findRegex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, replaceText.size(), (intptr_t)replaceText.c_str());
        intptr_t end = S(editor, SCI_GETTARGETEND);
        pos = end > start ? end : end + 1;   // an empty match must still move on
        n++;
        if (n > 1000000) break;
    }
    S(editor, SCI_ENDUNDOACTION);
    setStatus(strprintf("Replaced %d occurrence%s", n, n == 1 ? "" : "s"));
    return n;
}

// --- editing helpers ---

void App::toggleComment()
{
    if (!cur) return;
    bool asmFile = isAsmFile(cur->path) || extension(cur->path) == "i";
    const char *mark = asmFile ? "# " : "// ";
    size_t ml = strlen(mark);
    intptr_t a = S(editor, SCI_GETSELECTIONSTART), b = S(editor, SCI_GETSELECTIONEND);
    intptr_t l0 = S(editor, SCI_LINEFROMPOSITION, a), l1 = S(editor, SCI_LINEFROMPOSITION, b);
    if (l1 > l0 && S(editor, SCI_POSITIONFROMLINE, l1) == b) l1--;
    // all lines commented already? then uncomment
    bool all = true;
    for (intptr_t l = l0; l <= l1 && all; l++) {
        intptr_t p = S(editor, SCI_GETLINEINDENTPOSITION, l);
        if (p == S(editor, SCI_GETLINEENDPOSITION, l)) continue;   // blank
        char c0 = (char)S(editor, SCI_GETCHARAT, p), c1 = (char)S(editor, SCI_GETCHARAT, p + 1);
        all = asmFile ? c0 == '#' : (c0 == '/' && c1 == '/');
    }
    S(editor, SCI_BEGINUNDOACTION);
    for (intptr_t l = l0; l <= l1; l++) {
        intptr_t p = S(editor, SCI_GETLINEINDENTPOSITION, l);
        if (p == S(editor, SCI_GETLINEENDPOSITION, l)) continue;
        if (all) {
            intptr_t n = asmFile ? 1 : 2;
            if ((char)S(editor, SCI_GETCHARAT, p + n) == ' ') n++;
            S(editor, SCI_DELETERANGE, p, n);
        } else {
            S(editor, SCI_INSERTTEXT, p, (intptr_t)mark);
        }
    }
    S(editor, SCI_ENDUNDOACTION);
    (void)ml;
}

// After a newline: keep the previous line's indentation, one level more after '{'.
// After '}' at the start of a line: one level less.
void App::autoIndent(int ch)
{
    if (!cur || !autoIndentEnabled) return;
    intptr_t pos = S(editor, SCI_GETCURRENTPOS);
    intptr_t line = S(editor, SCI_LINEFROMPOSITION, pos);
    intptr_t width = S(editor, SCI_GETINDENT);
    if (width <= 0) width = S(editor, SCI_GETTABWIDTH);
    if (ch == '\n' || ch == '\r') {
        if (line == 0) return;
        intptr_t prev = line - 1;
        intptr_t indent = S(editor, SCI_GETLINEINDENTATION, prev);
        // last non-blank character of the previous line
        intptr_t e = S(editor, SCI_GETLINEENDPOSITION, prev), s = S(editor, SCI_POSITIONFROMLINE, prev);
        char last = 0;
        for (intptr_t p = e - 1; p >= s; p--) {
            char c = (char)S(editor, SCI_GETCHARAT, p);
            if (c != ' ' && c != '\t') { last = c; break; }
        }
        if (last == '{' && !isAsmFile(cur->path)) indent += width;
        S(editor, SCI_SETLINEINDENTATION, line, indent);
        S(editor, SCI_GOTOPOS, S(editor, SCI_GETLINEINDENTPOSITION, line));
    } else if (ch == '}') {
        intptr_t ip = S(editor, SCI_GETLINEINDENTPOSITION, line);
        if (ip != pos - 1 || line == 0) return;   // not the first thing on the line
        // match the line that opened the block
        intptr_t open = S(editor, SCI_BRACEMATCH, pos - 1, 0);
        intptr_t indent = open >= 0 ? S(editor, SCI_GETLINEINDENTATION, S(editor, SCI_LINEFROMPOSITION, open))
                                    : std::max<intptr_t>(0, S(editor, SCI_GETLINEINDENTATION, line) - width);
        S(editor, SCI_SETLINEINDENTATION, line, indent);
        S(editor, SCI_GOTOPOS, S(editor, SCI_GETLINEINDENTPOSITION, line) + 1);
    }
}

// --- SciViewHost ---

void App::sciNotify(SciView *v, const Scintilla::NotificationData &n)
{
    if (v != editor) return;
    switch ((int)n.nmhdr.code) {
    case SCN_SAVEPOINTLEFT:
    case SCN_SAVEPOINTREACHED:
        if (cur) {
            bool m = (int)n.nmhdr.code == SCN_SAVEPOINTLEFT;
            if (m != cur->modified) {
                cur->modified = m;
                rebuildTabs();
                updateTitle();
            }
        }
        break;
    case SCN_UPDATEUI:
        updateCaretStatus();
        break;
    case SCN_CHARADDED:
        autoIndent(n.ch);
        break;
    case SCN_MARGINCLICK: {
        intptr_t line = S(editor, SCI_LINEFROMPOSITION, (intptr_t)n.position);
        if (n.margin == AF_MARGIN_FOLD) S(editor, SCI_TOGGLEFOLD, line);
        else if (cur) toggleBreakpoint(cur, (int)line + 1);
        break;
    }
    default:
        break;
    }
}

void App::sciContextMenu(SciView *, int, int)
{
    // the IDE's actions are on the menus and function keys
}

} // namespace af
