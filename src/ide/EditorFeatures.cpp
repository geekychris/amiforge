// Editor features: colour themes, a split view and a minimap of the current document,
// multiple cursors, snippets, the symbol outline, Find All References and Rename Symbol.
//
// Views. `mainView` is the editor that owns the scrollers; with View > Split Editor a second
// view (`splitView`) shows the same Scintilla document beside it, and View > Minimap adds a
// tiny read-only view (`miniView`) on the right. `editor` is whichever edit view was clicked
// last; the commands work on it. All views share the document, so text, markers, undo and
// indicators are common; scrolling, the selection and the colours are per view.
#include "App.h"
#include <intuition/gadgetclass.h>
#include "Commands.h"

#include <string.h>
#include <ctype.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>

#include "Scintilla.h"
#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "sci/Theme.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static const char *const prefsDir[] = { "ENV:AmiForge", "ENVARC:AmiForge" };

static void savePref(const char *name, const std::string &value)
{
    for (const char *dir : prefsDir) {
        BPTR l = IDOS->Lock(dir, SHARED_LOCK);
        if (!l) l = IDOS->CreateDir(dir);
        if (l) IDOS->UnLock(l);
        writeFile(joinPath(dir, name), value + "\n");
    }
}

static std::string loadPref(const char *name)
{
    std::string text;
    if (!readFile(std::string("ENV:AmiForge/") + name, text) && !readFile(std::string("ENVARC:AmiForge/") + name, text))
        return "";
    return trim(text);
}

static std::string textRange(SciView *v, intptr_t a, intptr_t b)
{
    if (!v || b <= a) return "";
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr = { { a, b }, buf.data() };
    v->send(SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return std::string(buf.data(), b - a);
}

static std::string wholeText(SciView *v)
{
    intptr_t len = S(v, SCI_GETLENGTH);
    std::vector<char> buf(len + 1);
    S(v, SCI_GETTEXT, len + 1, (intptr_t)buf.data());
    return std::string(buf.data(), len);
}

static bool asmSource(const std::string &path)
{
    return isAsmFile(path) || extension(path) == "i";
}

// ---------------------------------------------------------------------------------------
// set-up
// ---------------------------------------------------------------------------------------

void App::initEditorFeatures()
{
    mainView = editor;
    themeName = themeNamed(loadPref("Theme")).name;
    setCurrentTheme(themeName);
    splitOn = loadPref("SplitView") == "1";
    minimapOn = loadPref("Minimap") == "1";
    loadSnippets();
    if (splitOn) { splitOn = false; toggleSplit(); }
    if (minimapOn) { minimapOn = false; toggleMinimap(); }
    syncViewButtons();
    rebuildMenus();
}

void App::closeEditorFeatures()
{
    for (SciView **v : { &splitView, &miniView }) {
        if (!*v) continue;
        (*v)->detach();
        delete *v;
        *v = nullptr;
    }
    editor = mainView;
}

// Colours and per-view settings for the current document.
void App::styleView(SciView *v)
{
    if (!v) return;
    sciSetupSourceEditor(v, cur && !cur->path.empty() ? cur->path.c_str() : "untitled.c", fontSize);
    S(v, SCI_SETWRAPMODE, wrapLines ? SC_WRAP_WORD : SC_WRAP_NONE);
    S(v, SCI_SETVIEWWS, showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    S(v, SCI_SETVIEWEOL, showWhitespace);
    gitApplyMarginsTo(v);   // --- integrations --- the git change and blame margins (not on the minimap)
    if (v == miniView) {
        // a tiny, plain, read-only overview: no margins, no caret; the selection shows the
        // part of the document the main view shows
        const Theme &t = currentTheme();
        S(v, SCI_SETZOOM, -10);
        for (int m = 0; m < 5; m++) S(v, SCI_SETMARGINWIDTHN, m, 0);
        S(v, SCI_SETCARETSTYLE, CARETSTYLE_INVISIBLE);
        S(v, SCI_SETCARETLINEVISIBLE, 0);
        S(v, SCI_SETWRAPMODE, SC_WRAP_NONE);
        S(v, SCI_SETVIEWWS, SCWS_INVISIBLE);
        S(v, SCI_SETVIEWEOL, 0);
        S(v, SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, sciColour(t.minimapView) | 0xFF000000);
        S(v, SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, sciColour(t.minimapView) | 0xFF000000);
        S(v, SCI_SETSELEOLFILLED, 1);
        S(v, AF_SCI_SETBRACEMATCH, 0);
        S(v, SCI_SETMULTIPLESELECTION, 0);
        S(v, SCI_SETMOUSEDWELLTIME, SC_TIME_FOREVER);
    }
}

// The views other than the active one show the current document too (or nothing).
void App::syncViews()
{
    for (SciView *v : { mainView, splitView, miniView }) {
        if (!v || v == editor) continue;
        if (!cur) {
            S(v, SCI_SETDOCPOINTER, 0, 0);
            continue;
        }
        if ((void *)S(v, SCI_GETDOCPOINTER) != cur->sciDoc) {
            S(v, SCI_SETDOCPOINTER, 0, (intptr_t)cur->sciDoc);
            styleView(v);
            if (v != miniView) {
                S(v, SCI_SETSEL, S(editor, SCI_GETANCHOR), S(editor, SCI_GETCURRENTPOS));
                S(v, SCI_SETFIRSTVISIBLELINE, S(editor, SCI_GETFIRSTVISIBLELINE));
            }
        }
    }
    if (snip.active && snip.doc != cur) endSnippet();
    updateMinimap();
    refreshOutline();
}

// ---------------------------------------------------------------------------------------
// themes
// ---------------------------------------------------------------------------------------

void App::setTheme(const std::string &name)
{
    themeName = themeNamed(name).name;
    setCurrentTheme(themeName);
    savePref("Theme", themeName);
    for (SciView *v : { mainView, splitView, miniView }) {
        if (!v || !cur) continue;
        styleView(v);
        S(v, SCI_COLOURISE, 0, -1);
    }
    refreshMarkers();
    if (snip.active) paintSnippetFields();
    updateMinimap();
    // the AutoDocs viewer
    if (docView) {
        styleDocView();
        docView->refresh();
    }
    rebuildMenus();
    for (SciView *v : { mainView, splitView, miniView }) if (v) v->refresh();
    setStatus(std::string("Theme: ") + themeName);
}

// ---------------------------------------------------------------------------------------
// split view and minimap
// ---------------------------------------------------------------------------------------

void App::syncViewButtons()
{
    setGadget(GID_TB_SPLIT, GA_Selected, splitOn);
    setGadget(GID_TB_MAP, GA_Selected, minimapOn);
}

void App::toggleSplit()
{
    if (!win) return;
    if (splitOn) {
        if (editor == splitView) setActiveView(mainView);
        splitView->detach();
        delete splitView;
        splitView = nullptr;
        splitOn = false;
    } else {
        splitView = SciView::create(win, this);
        if (!splitView) return;
        splitOn = true;
        if (cur) {
            S(splitView, SCI_SETDOCPOINTER, 0, (intptr_t)cur->sciDoc);
            styleView(splitView);
            S(splitView, SCI_SETFIRSTVISIBLELINE, S(mainView, SCI_GETFIRSTVISIBLELINE));
            S(splitView, SCI_SETSEL, S(mainView, SCI_GETANCHOR), S(mainView, SCI_GETCURRENTPOS));
        }
        splitView->setFocus(false);
    }
    savePref("SplitView", splitOn ? "1" : "0");
    syncViewButtons();
    // the views take new places: clear the old ones first so no stale image is left
    if (struct Gadget *g = (struct Gadget *)gad[GID_EDITOR])
        IGraphics->EraseRect(win->RPort, g->LeftEdge, g->TopEdge, g->LeftEdge + g->Width - 1, g->TopEdge + g->Height - 1);
    for (SciView *v : { mainView, splitView, miniView }) if (v) v->setBounds(0, 0, 0, 0);
    layoutEditor();
    rebuildMenus();
    setStatus(splitOn ? "Split view: two views of the same file. Click Split (or View > Split Editor) for one view" : "Split view off");
}

void App::toggleMinimap()
{
    if (!win) return;
    if (minimapOn) {
        miniView->detach();
        delete miniView;
        miniView = nullptr;
        minimapOn = false;
        miniDragging = false;
    } else {
        miniView = SciView::create(win, this);
        if (!miniView) return;
        minimapOn = true;
        if (cur) {
            S(miniView, SCI_SETDOCPOINTER, 0, (intptr_t)cur->sciDoc);
            styleView(miniView);
        }
        miniView->setFocus(false);
    }
    savePref("Minimap", minimapOn ? "1" : "0");
    syncViewButtons();
    setStatus(minimapOn ? "Minimap on: click it to jump. Click Map (or View > Minimap) to hide it" : "Minimap off");
    if (struct Gadget *g = (struct Gadget *)gad[GID_EDITOR])
        IGraphics->EraseRect(win->RPort, g->LeftEdge, g->TopEdge, g->LeftEdge + g->Width - 1, g->TopEdge + g->Height - 1);
    for (SciView *v : { mainView, splitView, miniView }) if (v) v->setBounds(0, 0, 0, 0);
    layoutEditor();
    updateMinimap();
    rebuildMenus();
}

// Place the views in the editor's box: [main | split] [minimap].
bool App::layoutViews(int left, int top, int width, int height, bool reblit)
{
    const int gap = 3;
    int miniW = minimapOn && miniView ? std::min(110, std::max(40, width / 6)) : 0;
    int editW = width - (miniW ? miniW + gap : 0);
    int mainW = splitView ? (editW - gap) / 2 : editW;
    struct Box { SciView *v; int l, t, w, h; };
    std::vector<Box> boxes = { { mainView, left, top, mainW, height } };
    if (splitView) boxes.push_back({ splitView, left + mainW + gap, top, editW - mainW - gap, height });
    if (miniW) boxes.push_back({ miniView, left + editW + gap, top, miniW, height });
    bool moved = false;
    int oldMainW = 0;
    for (auto &b : boxes) {
        int l, t, w, h;
        b.v->getBounds(&l, &t, &w, &h);
        if (b.v == mainView) oldMainW = w;
        if (l == b.l && t == b.t && w == b.w && h == b.h) {
            if (reblit) b.v->refresh();
            continue;
        }
        b.v->setBounds(b.l, b.t, b.w, b.h);
        moved = true;
    }
    // the gaps between the views
    if (win && (moved || reblit) && boxes.size() > 1) {
        for (size_t i = 0; i + 1 < boxes.size(); i++) {
            int x0 = boxes[i].l + boxes[i].w, x1 = boxes[i + 1].l - 1;
            if (x1 >= x0) IGraphics->EraseRect(win->RPort, x0, top, x1, top + height - 1);
        }
    }
    if (moved && miniView) updateMinimap();
    (void)oldMainW;
    return moved;
}

SciView *App::viewAt(int x, int y)
{
    for (SciView *v : { mainView, splitView, miniView }) {
        if (!v) continue;
        int l, t, w, h;
        v->getBounds(&l, &t, &w, &h);
        if (x >= l && x < l + w && y >= t && y < t + h) return v;
    }
    return nullptr;
}

void App::setActiveView(SciView *v)
{
    if (!v || v == miniView || v == editor) return;
    if (editor) editor->setFocus(false);
    if (S(editor, SCI_AUTOCACTIVE)) S(editor, SCI_AUTOCCANCEL);
    if (S(editor, SCI_CALLTIPACTIVE)) S(editor, SCI_CALLTIPCANCEL);
    editor = v;
    editorFocus = true;
    v->setFocus(true);
    updateCaretStatus();
}

void App::tickViews()
{
    for (SciView *v : { mainView, splitView, miniView })
        if (v) v->tick();
}

// The minimap shows the main view's lines as a selection, scrolled so that it stays in sight.
void App::updateMinimap()
{
    if (!miniView || !mainView || !cur) return;
    intptr_t firstVis = S(mainView, SCI_GETFIRSTVISIBLELINE);
    intptr_t onScreen = S(mainView, SCI_LINESONSCREEN);
    intptr_t docFirst = S(mainView, SCI_DOCLINEFROMVISIBLE, firstVis);
    intptr_t docLast = S(mainView, SCI_DOCLINEFROMVISIBLE, firstVis + std::max<intptr_t>(onScreen, 1) - 1);
    intptr_t lines = S(miniView, SCI_GETLINECOUNT);
    intptr_t a = S(miniView, SCI_POSITIONFROMLINE, docFirst);
    intptr_t b = docLast + 1 < lines ? S(miniView, SCI_POSITIONFROMLINE, docLast + 1) : S(miniView, SCI_GETLENGTH);
    S(miniView, SCI_SETSELECTION, b, a);
    // keep the highlighted part in view: scroll in proportion to the main view
    intptr_t miniOnScreen = S(miniView, SCI_LINESONSCREEN);
    intptr_t visTotal = S(miniView, SCI_VISIBLEFROMDOCLINE, lines);
    intptr_t top = 0;
    if (visTotal > miniOnScreen) {
        intptr_t range = std::max<intptr_t>(1, visTotal - onScreen);
        top = (intptr_t)((int64_t)firstVis * (visTotal - miniOnScreen) / range);
        top = std::max<intptr_t>(0, std::min(top, visTotal - miniOnScreen));
    }
    if (S(miniView, SCI_GETFIRSTVISIBLELINE) != top) S(miniView, SCI_SETFIRSTVISIBLELINE, top);
    miniView->tick();
}

// Clicking or dragging in the minimap centres the main view on that line.
void App::minimapMouse(const struct IntuiMessage *im)
{
    if (!miniView || !mainView) return;
    if (im->Class == IDCMP_MOUSEBUTTONS) {
        if (im->Code == SELECTDOWN) miniDragging = true;
        else if (im->Code == SELECTUP) { miniDragging = false; return; }
        else return;
    } else if (!miniDragging) {
        return;
    }
    int l, t, w, h;
    miniView->getBounds(&l, &t, &w, &h);
    int y = std::max(0, std::min(im->MouseY - t, h - 1));
    intptr_t miniFirst = S(miniView, SCI_GETFIRSTVISIBLELINE);
    intptr_t lineH = std::max<intptr_t>(1, S(miniView, SCI_TEXTHEIGHT, 0));
    intptr_t visLine = miniFirst + y / lineH;
    intptr_t onScreen = S(mainView, SCI_LINESONSCREEN);
    intptr_t docLine = S(miniView, SCI_DOCLINEFROMVISIBLE, visLine);
    intptr_t target = S(mainView, SCI_VISIBLEFROMDOCLINE, docLine) - onScreen / 2;
    S(mainView, SCI_SETFIRSTVISIBLELINE, std::max<intptr_t>(0, target));
    mainView->tick();
    updateMinimap();
}

void App::minimapWheel(int lines)
{
    if (!mainView) return;
    S(mainView, SCI_LINESCROLL, 0, lines);
    mainView->tick();
    updateMinimap();
}

// Input for the edit views and the minimap, from the IDCMP hook. Returns true when the message
// was dealt with here.
bool App::editorIdcmp(struct IntuiMessage *im, uint32_t &result)
{
    result = 0;
    const UWORD alt = IEQUALIFIER_LALT | IEQUALIFIER_RALT;
    switch (im->Class) {
    case IDCMP_RAWKEY: {
        UWORD code = im->Code, qual = im->Qualifier;
        // the wheel (as raw keys) scrolls the view under the pointer
        if (code >= 0x7A && code <= 0x7D) {
            SciView *v = viewAt(im->MouseX, im->MouseY);
            if (v == miniView && v) minimapWheel(code == 0x7A ? -3 : code == 0x7B ? 3 : 0);
            else if (v) v->handleMessage(im);
            return true;
        }
        if (code & IECODE_UP_PREFIX) return false;
        if (!editor || !editorFocus) return false;
        // Tab: expand a snippet / move between its fields
        if (code == 0x42 && !(qual & (IEQUALIFIER_CONTROL | alt | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
            bool shift = qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT);
            if (snippetTab(shift)) return true;
        }
        // Esc also ends a snippet and clears the reference highlights
        if (code == 0x45) {
            if (snip.active) endSnippet();
            if (occurrencesShown) clearOccurrences();
        }
        return false;
    }
    case IDCMP_MOUSEBUTTONS: {
        SciView *v = viewAt(im->MouseX, im->MouseY);
        if (miniDragging || (v && v == miniView)) {
            minimapMouse(im);
            return true;
        }
        if (v && im->Code == SELECTDOWN) setActiveView(v);
        if (v && (im->Code == MENUDOWN) && v != editor) setActiveView(v);
        // Alt-click adds a caret (Scintilla does that for Ctrl-click, which is go-to-definition here)
        if (v && im->Code == SELECTDOWN && (im->Qualifier & alt) && !(im->Qualifier & IEQUALIFIER_CONTROL)) {
            struct IntuiMessage copy = *im;
            copy.Qualifier = (im->Qualifier & ~alt) | IEQUALIFIER_CONTROL;
            editor->handleMessage(&copy);
            altClick = true;
            return true;
        }
        if (altClick && im->Code == SELECTUP) {
            struct IntuiMessage copy = *im;
            copy.Qualifier = (im->Qualifier & ~alt) | IEQUALIFIER_CONTROL;
            editor->handleMessage(&copy);
            altClick = false;
            return true;
        }
        return false;   // the usual handling, for `editor`
    }
    case IDCMP_MOUSEMOVE: {
        if (miniDragging) {
            minimapMouse(im);
            return true;
        }
        if (altClick && editor) {
            struct IntuiMessage copy = *im;
            copy.Qualifier = (im->Qualifier & ~alt) | IEQUALIFIER_CONTROL;
            editor->handleMessage(&copy);
            return true;
        }
        // the right button opens the context menu over any edit view
        SciView *v = viewAt(im->MouseX, im->MouseY);
        if (win) {
            if (v && v != miniView) win->Flags |= WFLG_RMBTRAP;
            else win->Flags &= ~WFLG_RMBTRAP;
        }
        for (SciView *e : { mainView, splitView })
            if (e) e->handleMessage(im);
        return true;
    }
    case IDCMP_EXTENDEDMOUSE: {
        SciView *v = viewAt(im->MouseX, im->MouseY);
        if (v && v == miniView) {
            if (im->Code == IMSGCODE_INTUIWHEELDATA && im->IAddress)
                minimapWheel(3 * ((struct IntuiWheelData *)im->IAddress)->WheelY);
            return true;
        }
        for (SciView *e : { mainView, splitView })
            if (e) e->handleMessage(im);
        return true;
    }
    case IDCMP_INTUITICKS:
        tickViews();
        if (outlineDelay > 0 && --outlineDelay == 0) queueCommand(CMD_REFRESH_OUTLINE);
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------
// multiple cursors
// ---------------------------------------------------------------------------------------

void App::addNextOccurrence(bool all)
{
    if (!editor || !cur) return;
    // whole words when starting from a caret (Scintilla selects the word), case-sensitive
    S(editor, SCI_TARGETWHOLEDOCUMENT);
    S(editor, SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | (S(editor, SCI_GETSELECTIONEMPTY) ? SCFIND_WHOLEWORD : 0));
    if (all) S(editor, SCI_MULTIPLESELECTADDEACH);
    else S(editor, SCI_MULTIPLESELECTADDNEXT);
    S(editor, SCI_SCROLLCARET);
    intptr_t n = S(editor, SCI_GETSELECTIONS);
    setStatus(strprintf("%ld selection%s - type to change them all, Esc for one cursor", (long)n, n == 1 ? "" : "s"));
}

// ---------------------------------------------------------------------------------------
// snippets
// ---------------------------------------------------------------------------------------

static const char snippetMark = '\xBB';   // "for»" in the completion list is the snippet

static const char *const snippetFileHelp =
    "# AmiForge snippets. Type a snippet's name and press Tab to insert it.\n"
    "#\n"
    "# snippet <name>  <description>\n"
    "# <tab>body line (each body line starts with a tab or four spaces)\n"
    "#\n"
    "# In the body: ${1:text} is the first field (Tab moves to the next one), ${2} an empty\n"
    "# field, ${0} where the cursor ends, and a field number used again copies the field.\n"
    "# A tab at the start of a body line is one indent step. \\$ is a dollar sign.\n"
    "# A snippet with a built-in snippet's name (main, openlib, window, timer, rexx, tags,\n"
    "# for, while, if, switch, struct, class) replaces it.\n"
    "\n"
    "snippet hello  print a line\n"
    "\tIDOS->Printf(\"${1:Hello, world}\\n\");${0}\n";

void App::loadSnippets()
{
    std::string text;
    std::vector<Snippet> user;
    if (readFile("ENV:AmiForge/Snippets", text) || readFile("ENVARC:AmiForge/Snippets", text))
        user = parseSnippets(text);
    snippets = mergeSnippets(builtinSnippets(), user);
}

void App::editSnippets()
{
    const char *path = "ENVARC:AmiForge/Snippets";
    if (!fileExists(path)) {
        BPTR l = IDOS->Lock("ENVARC:AmiForge", SHARED_LOCK);
        if (!l) l = IDOS->CreateDir("ENVARC:AmiForge");
        if (l) IDOS->UnLock(l);
        writeFile(path, snippetFileHelp);
    }
    openFile(path);
    setStatus("Your snippets: they're read again when you save this file");
}

void App::addSnippetWords(const std::string &prefix, std::set<std::string> &words)
{
    for (auto &s : snippets)
        if (prefix.empty() || strncasecmp(s.name.c_str(), prefix.c_str(), prefix.size()) == 0)
            words.insert(s.name + snippetMark);
}

void App::insertSnippetMenu()
{
    if (!editor || !cur) return;
    std::set<std::string> words;
    addSnippetWords("", words);
    std::string list;
    for (auto &w : words) list += (list.empty() ? "" : " ") + w;
    S(editor, SCI_AUTOCSETSEPARATOR, ' ');
    S(editor, SCI_AUTOCSETIGNORECASE, 1);
    S(editor, SCI_AUTOCSETMAXHEIGHT, 12);
    S(editor, SCI_AUTOCSHOW, 0, (intptr_t)list.c_str());
    setStatus("Pick a snippet (or type its name and press Tab)");
}

// A snippet picked from the completion list: insert it instead of its name.
bool App::snippetCompletion(const char *text, intptr_t wordStart)
{
    size_t n = text ? strlen(text) : 0;
    if (n < 2 || text[n - 1] != snippetMark) return false;
    std::string name(text, n - 1);
    const Snippet *s = findSnippet(snippets, name);
    if (!s) return false;
    S(editor, SCI_AUTOCCANCEL);
    insertSnippet(*s, wordStart, S(editor, SCI_GETCURRENTPOS));
    return true;
}

bool App::snippetTab(bool backwards)
{
    if (!editor || !cur) return false;
    intptr_t caret = S(editor, SCI_GETCURRENTPOS);
    if (snip.active) {
        if (snip.doc == cur && !snip.fields.empty()) {
            size_t lo = SIZE_MAX, hi = 0;
            for (auto &f : snip.fields) { lo = std::min(lo, f.start); hi = std::max(hi, f.end); }
            if ((size_t)caret >= lo && (size_t)caret <= hi) {
                // in a snippet Tab moves on; a completion list that popped up meanwhile goes away
                // (Return still accepts a completion)
                if (S(editor, SCI_AUTOCACTIVE)) S(editor, SCI_AUTOCCANCEL);
                snippetGoto(backwards ? -1 : 1);
                return true;
            }
        }
        endSnippet();
    }
    if (S(editor, SCI_AUTOCACTIVE)) return false;   // Tab accepts the completion (a "name»" expands)
    if (backwards) return false;
    if (!S(editor, SCI_GETSELECTIONEMPTY) || S(editor, SCI_GETSELECTIONS) > 1) return false;
    intptr_t start = S(editor, SCI_WORDSTARTPOSITION, caret, 1);
    if (start >= caret) return false;
    char next = (char)S(editor, SCI_GETCHARAT, caret);
    if (isalnum((unsigned char)next) || next == '_') return false;
    const Snippet *s = findSnippet(snippets, textRange(editor, start, caret));
    if (!s) return false;
    insertSnippet(*s, start, caret);
    return true;
}

void App::insertSnippet(const Snippet &s, intptr_t from, intptr_t to)
{
    if (snip.active) endSnippet();
    intptr_t line = S(editor, SCI_LINEFROMPOSITION, from);
    intptr_t ls = S(editor, SCI_POSITIONFROMLINE, line);
    intptr_t ie = std::min(S(editor, SCI_GETLINEINDENTPOSITION, line), from);
    std::string indent = textRange(editor, ls, ie);
    std::string unit = S(editor, SCI_GETUSETABS) ? std::string("\t") : std::string(std::max<intptr_t>(1, S(editor, SCI_GETINDENT)), ' ');
    SnippetExpansion ex = expandSnippet(s.body, indent, unit);
    S(editor, SCI_BEGINUNDOACTION);
    S(editor, SCI_SETTARGETRANGE, from, to);
    S(editor, SCI_REPLACETARGET, ex.text.size(), (intptr_t)ex.text.c_str());
    S(editor, SCI_ENDUNDOACTION);
    snip = SnippetSession();
    snip.order = ex.tabOrder();
    for (auto f : ex.fields) {
        f.start += from;
        f.end += from;
        snip.fields.push_back(f);
    }
    if (snip.order.empty()) {
        S(editor, SCI_GOTOPOS, from + ex.text.size());
        return;
    }
    snip.active = true;
    snip.doc = cur;
    snip.at = -1;
    paintSnippetFields();
    snippetGoto(1);
    setStatus("Snippet " + s.name + ": type over a field, Tab for the next one, Esc when done");
}

// Leave the current field (copying its text to its mirrors) and select the next/previous one.
void App::snippetGoto(int step)
{
    if (!snip.active) return;
    if (snip.at >= 0 && snip.at < (int)snip.order.size()) {
        const SnippetField &f = snip.fields[snip.order[snip.at]];
        std::string text = textRange(editor, f.start, f.end);
        // mirrors, from the end so earlier positions stay put
        std::vector<size_t> mirrors;
        for (size_t k = 0; k < snip.fields.size(); k++)
            if (snip.fields[k].mirror && snip.fields[k].number == f.number) mirrors.push_back(k);
        std::sort(mirrors.begin(), mirrors.end(), [this](size_t a, size_t b) { return snip.fields[a].start > snip.fields[b].start; });
        if (!mirrors.empty()) {
            S(editor, SCI_BEGINUNDOACTION);
            for (size_t k : mirrors) {
                const SnippetField &m = snip.fields[k];
                if (textRange(editor, m.start, m.end) == text) continue;
                S(editor, SCI_SETTARGETRANGE, m.start, m.end);
                S(editor, SCI_REPLACETARGET, text.size(), (intptr_t)text.c_str());
            }
            S(editor, SCI_ENDUNDOACTION);
        }
    }
    snip.at += step;
    if (snip.at < 0) snip.at = 0;
    if (snip.at >= (int)snip.order.size()) {
        // past the last field: to the end of the snippet
        size_t hi = 0;
        for (auto &f : snip.fields) hi = std::max(hi, f.end);
        S(editor, SCI_GOTOPOS, hi);
        endSnippet();
        return;
    }
    const SnippetField &f = snip.fields[snip.order[snip.at]];
    if (f.number == 0) {
        S(editor, SCI_GOTOPOS, f.start);
        endSnippet();
        setStatus("");
        return;
    }
    S(editor, SCI_SETSEL, f.start, f.end);
    S(editor, SCI_SCROLLCARET);
    paintSnippetFields();
}

void App::paintSnippetFields()
{
    SciView *v = mainView;
    if (!v || !cur) return;
    S(v, SCI_SETINDICATORCURRENT, AF_INDIC_SNIPPET);
    S(v, SCI_INDICATORCLEARRANGE, 0, S(v, SCI_GETLENGTH));
    if (!snip.active || snip.doc != cur) return;
    for (size_t k = 0; k < snip.fields.size(); k++) {
        const SnippetField &f = snip.fields[k];
        if (f.number == 0 || f.end <= f.start) continue;
        S(v, SCI_INDICATORFILLRANGE, f.start, f.end - f.start);
    }
}

void App::endSnippet()
{
    snip.active = false;
    Document *d = snip.doc;
    snip = SnippetSession();
    if (d == cur) {
        S(mainView, SCI_SETINDICATORCURRENT, AF_INDIC_SNIPPET);
        S(mainView, SCI_INDICATORCLEARRANGE, 0, S(mainView, SCI_GETLENGTH));
    }
}

// SCN_MODIFIED of the current document (from the main view, which always shows it).
void App::editorModified(const Scintilla::NotificationData &n)
{
    int type = (int)n.modificationType;
    if (!(type & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
    if (snip.active && snip.doc == cur) {
        adjustSnippetFields(snip.fields, (size_t)n.position, (size_t)n.length, (type & SC_MOD_INSERTTEXT) != 0);
        queueCommand(CMD_SNIPPET_PAINT);
    }
    outlineDelay = 8;   // ~0.8 s after typing stops
    if (miniView && n.linesAdded) queueCommand(CMD_MINIMAP_UPDATE);
}

// ---------------------------------------------------------------------------------------
// the symbol outline
// ---------------------------------------------------------------------------------------

void App::refreshOutline()
{
    if (!symbolList.obj) return;
    outlineDelay = 0;
    outline.clear();
    if (cur && mainView) {
        std::string path = cur->path.empty() ? std::string("untitled.c") : cur->path;
        std::string ext = extension(path);
        bool code = ext == "c" || ext == "h" || isCppFile(path) || ext == "hpp" || ext == "hh" || ext == "hxx" ||
                    asmSource(path) || cur->path.empty();
        if (code) outline = outlineSymbols(wholeText(mainView), asmSource(path));
    }
    symbolList.clear();
    symbolList.detach();
    for (size_t i = 0; i < outline.size(); i++) {
        const Symbol &s = outline[i];
        std::string label = std::string(s.depth * 2, ' ') + s.name;
        if (s.kind == SymKind::Function || (s.kind == SymKind::Define && !s.detail.empty() && s.detail != "global"))
            label += s.detail.empty() || s.detail[0] != '(' ? "()" : s.detail.size() > 2 ? "(...)" : "()";
        symbolList.add({label, symKindName(s.kind)}, (void *)(intptr_t)(i + 1));
    }
    symbolList.attach();
}

void App::outlineClicked()
{
    struct Node *n = symbolList.selected();
    intptr_t idx = (intptr_t)symbolList.userData(n) - 1;
    if (idx < 0 || idx >= (intptr_t)outline.size() || !cur) return;
    const Symbol &s = outline[idx];
    if (cur->path.empty()) {
        S(editor, SCI_GOTOLINE, s.line - 1);
        S(editor, SCI_SCROLLCARET);
    } else {
        gotoLocation(cur->path, s.line);
    }
    // select the name on that line
    intptr_t ls = S(editor, SCI_POSITIONFROMLINE, s.line - 1), le = S(editor, SCI_GETLINEENDPOSITION, s.line - 1);
    std::string line = textRange(editor, ls, le);
    std::string bare = s.name.substr(s.name.rfind(':') == std::string::npos ? 0 : s.name.rfind(':') + 1);
    size_t at = line.find(bare);
    if (at != std::string::npos) S(editor, SCI_SETSEL, ls + at, ls + at + bare.size());
    editorFocus = true;
    editor->setFocus(true);
}

// ---------------------------------------------------------------------------------------
// references and renaming
// ---------------------------------------------------------------------------------------

std::vector<std::string> App::sourceFilesForSearch()
{
    std::vector<std::string> files;
    for (auto &f : project.files) files.push_back(project.abs(f));
    for (auto &d : docs) {
        if (d->path.empty()) continue;
        bool have = false;
        for (auto &f : files) if (sameFile(f, d->path)) { have = true; break; }
        if (!have) files.push_back(d->path);
    }
    return files;
}

// Run fn with the main view showing another open document, then put the current one back.
void App::withDocument(Document *d, const std::function<void()> &fn)
{
    if (!mainView || !d) return;
    if (d == cur) { fn(); return; }
    intptr_t first = S(mainView, SCI_GETFIRSTVISIBLELINE), caret = S(mainView, SCI_GETCURRENTPOS);
    intptr_t anchor = S(mainView, SCI_GETANCHOR), xo = S(mainView, SCI_GETXOFFSET);
    batchEditing = true;
    S(mainView, SCI_SETDOCPOINTER, 0, (intptr_t)d->sciDoc);
    fn();
    S(mainView, SCI_SETDOCPOINTER, 0, cur ? (intptr_t)cur->sciDoc : 0);
    S(mainView, SCI_SETSEL, anchor, caret);
    S(mainView, SCI_SETFIRSTVISIBLELINE, first);
    S(mainView, SCI_SETXOFFSET, xo);
    batchEditing = false;
}

std::string App::documentText(Document *d)
{
    std::string text;
    withDocument(d, [&] { text = wholeText(mainView); });
    return text;
}

void App::clearOccurrences()
{
    occurrencesShown = false;
    for (auto &d : docs)
        withDocument(d.get(), [&] {
            S(mainView, SCI_SETINDICATORCURRENT, AF_INDIC_OCCURRENCE);
            S(mainView, SCI_INDICATORCLEARRANGE, 0, S(mainView, SCI_GETLENGTH));
        });
}

void App::findReferences()
{
    if (!editor || !cur) return;
    std::string word = wordAt(S(editor, SCI_GETCURRENTPOS));
    if (!isIdentifierName(word)) {
        setStatus("Put the cursor on a name first");
        return;
    }
    if (occurrencesShown) clearOccurrences();
    findResults.clear();
    findLog.clear();
    findLog.detach();
    busyPointer(true);
    std::vector<std::string> files = sourceFilesForSearch();
    // an untitled current document is searched too
    bool curListed = !cur->path.empty();
    int nfiles = 0;
    auto search = [&](const std::string &label, const std::string &path, Document *d) {
        std::string text;
        if (d) text = documentText(d);
        else if (!readFile(path, text)) return;
        std::vector<size_t> hits = findIdentifier(text, word, asmSource(path));
        if (hits.empty()) return;
        nfiles++;
        for (size_t h : hits) {
            int line, col;
            lineColumn(text, h, line, col);
            size_t ls = text.rfind('\n', h);
            ls = ls == std::string::npos ? 0 : ls + 1;
            size_t le = text.find('\n', h);
            BuildLine r;
            r.kind = MsgKind::Note;
            r.file = path;
            r.line = line;
            r.col = col;
            r.text = trim(text.substr(ls, le == std::string::npos ? std::string::npos : le - ls));
            findResults.push_back(r);
            findLog.add({strprintf("%s:%d", label.c_str(), line), r.text}, (void *)(intptr_t)findResults.size());
        }
        if (d) {
            withDocument(d, [&] {
                S(mainView, SCI_SETINDICATORCURRENT, AF_INDIC_OCCURRENCE);
                for (size_t h : hits) S(mainView, SCI_INDICATORFILLRANGE, h, word.size());
            });
            occurrencesShown = true;
        }
    };
    if (!curListed) search(cur->title, "", cur);
    for (auto &f : files) search(project.rel(f), f, findDocument(f));
    busyPointer(false);
    findLog.attach();
    showBottomPage(1);
    setStatus(strprintf("%d reference%s to %s in %d file%s (comments and strings left out); Esc clears the highlights",
                        (int)findResults.size(), findResults.size() == 1 ? "" : "s", word.c_str(), nfiles, nfiles == 1 ? "" : "s"));
}

void App::renameSymbol()
{
    if (!editor || !cur) return;
    std::string word = wordAt(S(editor, SCI_GETCURRENTPOS));
    if (!isIdentifierName(word) || isReservedWord(word)) {
        setStatus("Put the cursor on a name to rename first");
        return;
    }
    std::string newName = word;
    if (!askString("Rename Symbol", "Rename " + word + " everywhere in the project to:", newName)) return;
    newName = trim(newName);
    if (newName == word) return;
    if (!isIdentifierName(newName) || isReservedWord(newName)) {
        info("\"" + newName + "\" isn't a name that can be used in C or C++.");
        return;
    }
    // count first, so the requester can say what will change
    struct Target { std::string path, label; Document *doc; int count; };
    std::vector<Target> targets;
    int total = 0, clashes = 0;
    auto consider = [&](const std::string &path, const std::string &label, Document *d) {
        std::string text = d ? documentText(d) : "";
        if (!d && !readFile(path, text)) return;
        int n = (int)findIdentifier(text, word, asmSource(path)).size();
        clashes += (int)findIdentifier(text, newName, asmSource(path)).size();
        if (n) { targets.push_back({path, label, d, n}); total += n; }
    };
    if (cur->path.empty()) consider("", cur->title, cur);
    for (auto &f : sourceFilesForSearch()) consider(f, project.rel(f), findDocument(f));
    if (!total) {
        info("No uses of " + word + " were found in the project's files.");
        return;
    }
    std::string body = strprintf("Rename %s to %s: %d change%s in %d file%s.\n\n", word.c_str(), newName.c_str(),
                                 total, total == 1 ? "" : "s", (int)targets.size(), targets.size() == 1 ? "" : "s");
    for (size_t i = 0; i < targets.size() && i < 12; i++)
        body += strprintf("    %s  (%d)\n", targets[i].label.c_str(), targets[i].count);
    if (targets.size() > 12) body += strprintf("    ... and %d more\n", (int)targets.size() - 12);
    body += "\nComments and strings are left alone. Open files can be undone with Undo;\n"
            "files that aren't open are saved straight away.";
    if (clashes) body += strprintf("\n\nWarning: %s is already used %d time%s.", newName.c_str(), clashes, clashes == 1 ? "" : "s");
    if (ask("Rename Symbol", body, "_Rename|_Cancel") != 1) return;
    busyPointer(true);
    int done = 0, failed = 0;
    for (auto &t : targets) {
        if (t.doc) {
            withDocument(t.doc, [&] {
                std::string text = wholeText(mainView);
                std::vector<size_t> hits = findIdentifier(text, word, asmSource(t.path));
                S(mainView, SCI_BEGINUNDOACTION);
                for (size_t k = hits.size(); k-- > 0;) {
                    S(mainView, SCI_SETTARGETRANGE, hits[k], hits[k] + word.size());
                    S(mainView, SCI_REPLACETARGET, newName.size(), (intptr_t)newName.c_str());
                }
                S(mainView, SCI_ENDUNDOACTION);
                done += (int)hits.size();
                t.doc->modified = S(mainView, SCI_GETMODIFY) != 0;
            });
        } else {
            std::string text;
            int n = 0;
            if (readFile(t.path, text) && writeFile(t.path, replaceIdentifier(text, word, newName, asmSource(t.path), &n)))
                done += n;
            else
                failed++;
        }
    }
    busyPointer(false);
    rebuildTabs();
    updateTitle();
    refreshOutline();
    if (failed) info(strprintf("%d file%s couldn't be written.", failed, failed == 1 ? "" : "s"));
    setStatus(strprintf("Renamed %s to %s: %d change%s", word.c_str(), newName.c_str(), done, done == 1 ? "" : "s"));
}

} // namespace af
