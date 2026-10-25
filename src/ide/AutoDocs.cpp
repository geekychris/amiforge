// AutoDocs: the AmigaOS 4 SDK's function reference (SDK:Documentation/AutoDocs/*.doc), browsable
// in a window and one key away from the editor (F1 on a function name).
//
// Each .doc file starts with a TABLE OF CONTENTS ("graphics.library/AddBob" lines), then one
// entry per function, each headed by its name at the start of a line (usually twice, and
// usually after a form feed). References to other entries in the text ("exec.library/AllocVec",
// "AllocVec()") are shown as links.
#include "App.h"
#include "sci/Theme.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/listbrowser.h>
#include <proto/wb.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>

#include "Scintilla.h"

namespace af {

static const char *docDir = "SDK:Documentation/AutoDocs";

enum { DG_ROOT = 1, DG_SEARCH, DG_BACK, DG_SDKDOCS, DG_LIST, DG_VIEW, DG_N };
enum { DS_TEXT = 0, DS_HEADING = 1, DS_LINK = 2, DS_TITLE = 3 };

// --- the index ---

static bool deviceExists(const char *name)
{
    struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
    bool found = IDOS->FindDosEntry(dl, name, LDF_ALL) != nullptr;
    IDOS->UnLockDosList(LDF_ALL | LDF_READ);
    return found;
}

bool App::loadDocIndex()
{
    if (!docIndex.empty()) return true;
    if (!deviceExists("SDK")) return false;
    busyPointer(true);
    BPTR lock = IDOS->Lock(docDir, SHARED_LOCK);
    if (lock) {
        APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, EX_DoCurrentDir, TRUE, TAG_END);
        if (ctx) {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx))) {
                if (!EXD_IS_FILE(ed) || extension((const char *)ed->Name) != "doc") continue;
                std::string path = joinPath(docDir, (const char *)ed->Name);
                // the table of contents is at the start; 64 KB covers the longest one
                BPTR fh = IDOS->Open(path.c_str(), MODE_OLDFILE);
                if (!fh) continue;
                std::string head(65536, '\0');
                int32 n = IDOS->Read(fh, &head[0], head.size());
                IDOS->Close(fh);
                if (n <= 0) continue;
                head.resize(n);
                size_t pos = head.find("TABLE OF CONTENTS");
                if (pos == std::string::npos) continue;
                pos = head.find('\n', pos);
                while (pos != std::string::npos && pos + 1 < head.size()) {
                    size_t nl = head.find('\n', pos + 1);
                    std::string line = trim(head.substr(pos + 1, nl == std::string::npos ? std::string::npos : nl - pos - 1));
                    pos = nl;
                    if (line.empty()) continue;
                    if (line[0] == '\f' || line.find('/') == std::string::npos || line.find(' ') != std::string::npos) break;
                    DocEntry e;
                    e.full = line;
                    e.file = path;
                    size_t slash = line.rfind('/');
                    e.func = line.substr(slash + 1);
                    e.lib = line.substr(0, line.find('/'));
                    if (!e.func.empty() && e.func[0] != '-') docIndex.push_back(e);
                }
            }
            IDOS->ReleaseDirContext(ctx);
        }
        IDOS->UnLock(lock);
    }
    std::sort(docIndex.begin(), docIndex.end(), [](const DocEntry &a, const DocEntry &b) {
        int c = strcasecmp(a.func.c_str(), b.func.c_str());
        return c ? c < 0 : strcasecmp(a.lib.c_str(), b.lib.c_str()) < 0;
    });
    busyPointer(false);
    return !docIndex.empty();
}

// "IExec->AllocVecTags" -> candidates in order: AllocVecTags, AllocVecTagList, AllocVec
const DocEntry *App::findDoc(const std::string &wordIn)
{
    if (!loadDocIndex()) return nullptr;
    std::string w = trim(wordIn);
    size_t arrow = w.rfind("->");
    if (arrow != std::string::npos) w = w.substr(arrow + 2);
    while (!w.empty() && (w.back() == '(' || w.back() == ')' || w.back() == ',' || w.back() == '.')) w.pop_back();
    // "exec.library/AllocVec" or "exec.library/debug/AddDebugHook"
    if (w.find('/') != std::string::npos) {
        for (auto &e : docIndex)
            if (strcasecmp(e.full.c_str(), w.c_str()) == 0) return &e;
        w = w.substr(w.rfind('/') + 1);
    }
    std::vector<std::string> tries = { w };
    auto ends = [&](const char *suf) {
        size_t n = strlen(suf);
        return w.size() > n && strcasecmp(w.c_str() + w.size() - n, suf) == 0;
    };
    if (ends("Tags")) {
        tries.push_back(w.substr(0, w.size() - 4) + "TagList");
        tries.push_back(w.substr(0, w.size() - 4) + "A");
        tries.push_back(w.substr(0, w.size() - 4));
    }
    if (ends("A")) tries.push_back(w.substr(0, w.size() - 1));
    if (ends("TagList")) tries.push_back(w.substr(0, w.size() - 7) + "Tags");
    for (auto &t : tries)
        for (auto &e : docIndex)
            if (strcasecmp(e.func.c_str(), t.c_str()) == 0) return &e;
    return nullptr;
}

// The text of one entry: its header is the name at the start of a line, either right after a
// form feed or followed by more text (the name repeated). The table of contents lists the
// names alone, so it doesn't match. The entry runs to the next form feed.
static std::string entryText(const DocEntry &e)
{
    std::string data;
    if (!readFile(e.file, data)) return "Can't read " + e.file;
    size_t pos = 0, start = std::string::npos;
    while ((pos = data.find(e.full, pos)) != std::string::npos) {
        bool afterFF = pos > 0 && data[pos - 1] == '\f';
        bool lineStart = afterFF || pos == 0 || data[pos - 1] == '\n';
        size_t after = pos + e.full.size();
        bool nameEnds = after >= data.size() || data[after] == '\n' || data[after] == ' ' || data[after] == '\t';
        size_t eol = data.find('\n', pos);
        bool more = !trim(data.substr(after, (eol == std::string::npos ? data.size() : eol) - after)).empty();
        if (lineStart && nameEnds && (afterFF || more)) { start = pos; break; }
        pos = after;
    }
    if (start == std::string::npos) return "Can't find " + e.full + " in " + e.file;
    size_t end = data.find('\f', start + 1);
    std::string text = data.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return text;
}

std::string App::docEntryText(const DocEntry &e) { return entryText(e); }

// --- the window ---

static uint32 docHookFunc(struct Hook *, APTR, struct IntuiMessage *im)
{
    if (gApp && gApp->docView) {
        if (im->Class == IDCMP_INTUITICKS) gApp->docView->tick();
        else gApp->docView->handleMessage(im);
    }
    return 0;
}
static struct Hook docHook = { {nullptr, nullptr}, (HOOKFUNC)docHookFunc, nullptr, nullptr };

Object *newEditorArea(uint32_t gid);

void App::openDocWindow()
{
    if (docWinObj) {
        IIntuition->WindowToFront(docWin);
        IIntuition->ActivateWindow(docWin);
        return;
    }
    static const char *titles[] = { "Function", "Library" };
    static const int weights[] = { 60, 40 };
    docList.hierarchical = false;
    Class *layout = ILayout->LAYOUT_GetClass();
    auto btn = [](uint32 id, const char *t) {
        return IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE,
                                     GA_Text, (uint32)t, TAG_DONE);
    };
    docWinObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"AutoDocs - AmigaOS 4.1 SDK",
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, TRUE, WA_ReportMouse, TRUE, WA_SmartRefresh, TRUE,
        WA_PubScreen, (uint32)win->WScreen,
        WA_Left, win->LeftEdge + 80, WA_Top, win->TopEdge + 40, WA_InnerWidth, 980, WA_InnerHeight, 520,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                  IDCMP_MOUSEMOVE | IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS | IDCMP_REFRESHWINDOW,
        WINDOW_IDCMPHook, (uint32)&docHook,
        WINDOW_IDCMPHookBits, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS,
        WINDOW_Layout, docGad[DG_ROOT] = IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, docGad[DG_SEARCH] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr,
                    GA_ID, DG_SEARCH, GA_RelVerify, TRUE, STRINGA_MaxChars, 127, TAG_DONE),
                CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)"_Find", TAG_DONE),
                LAYOUT_AddChild, docGad[DG_BACK] = btn(DG_BACK, "_Back"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, docGad[DG_SDKDOCS] = btn(DG_SDKDOCS, "SDK _Documentation"),
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_WeightBar, TRUE,
                LAYOUT_AddChild, docList.create(DG_LIST, titles, weights),
                CHILD_WeightedWidth, 28,
                LAYOUT_AddChild, docGad[DG_VIEW] = newEditorArea(DG_VIEW),
                CHILD_WeightedWidth, 72,
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);
    if (!docWinObj) return;
    docWin = (struct Window *)IIntuition->IDoMethod(docWinObj, WM_OPEN);
    if (!docWin) {
        IIntuition->DisposeObject(docWinObj);
        docWinObj = nullptr;
        return;
    }
    docList.win = docWin;
    docView = SciView::create(docWin, this);
    if (docView) {
        styleDocView();
        docView->send(SCI_SETHOTSPOTACTIVEUNDERLINE, 1);
        layoutDocView();
    }
    // fill the list with everything
    showDocMatches("");
}

// --- editor features: the AutoDocs viewer in the current theme's colours ---
void App::styleDocView()
{
    if (!docView) return;
    const Theme &t = currentTheme();
    sciSetupSourceEditor(docView, "autodoc.txt", fontSize);
    docView->send(SCI_SETILEXER, 0, 0);
    docView->send(SCI_SETMARGINWIDTHN, AF_MARGIN_NUMBERS, 0);
    docView->send(SCI_SETMARGINWIDTHN, AF_MARGIN_SYMBOLS, 0);
    docView->send(SCI_SETMARGINWIDTHN, AF_MARGIN_FOLD, 0);
    docView->send(SCI_SETCARETLINEVISIBLE, 0);
    docView->send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    docView->send(SCI_STYLESETBOLD, DS_HEADING, 1);
    docView->send(SCI_STYLESETFORE, DS_HEADING, sciColour(t.docHeading));
    docView->send(SCI_STYLESETFORE, DS_LINK, sciColour(t.docLink));
    docView->send(SCI_STYLESETUNDERLINE, DS_LINK, 1);
    docView->send(SCI_STYLESETHOTSPOT, DS_LINK, 1);
    docView->send(SCI_STYLESETBOLD, DS_TITLE, 1);
    docView->send(SCI_STYLESETFORE, DS_TITLE, sciColour(t.docTitle));
}

void App::closeDocWindow()
{
    if (docView) {
        docView->detach();
        delete docView;
        docView = nullptr;
    }
    if (docWinObj) IIntuition->DisposeObject(docWinObj);
    docWinObj = nullptr;
    docWin = nullptr;
    docList.win = nullptr;
    docList.obj = nullptr;
    memset(docGad, 0, sizeof docGad);
}

void App::layoutDocView()
{
    if (!docView || !docWin || !docGad[DG_VIEW]) return;
    struct Gadget *g = (struct Gadget *)docGad[DG_VIEW];
    int l, t, w, h;
    docView->getBounds(&l, &t, &w, &h);
    if (l != g->LeftEdge || t != g->TopEdge || w != g->Width || h != g->Height)
        docView->setBounds(g->LeftEdge, g->TopEdge, g->Width, g->Height);
    else
        docView->refresh();
}

void App::showDocMatches(const std::string &filter)
{
    if (!docWinObj) return;
    if (!loadDocIndex()) {
        docList.clear();
        docList.detach();
        docList.add({"The SDK isn't installed (no SDK:)", ""});
        docList.attach();
        return;
    }
    std::string f = filter;
    for (auto &c : f) c = tolower((unsigned char)c);
    docList.clear();
    docList.detach();
    int shown = 0, sel = -1;
    for (size_t i = 0; i < docIndex.size(); i++) {
        auto &e = docIndex[i];
        if (!f.empty()) {
            std::string n = e.func;
            for (auto &c : n) c = tolower((unsigned char)c);
            if (n.find(f) == std::string::npos) continue;
            if (sel < 0 && n == f) sel = shown;
        }
        docList.add({e.func, e.lib}, (void *)(intptr_t)(i + 1));
        if (++shown >= 3000) break;
    }
    docList.attach(sel);
}

// Shows one entry, marks headings and links.
void App::showDoc(const DocEntry *e, bool remember)
{
    if (!e) return;
    openDocWindow();
    if (!docView) return;
    if (remember && !docCurrent.empty() && docCurrent != e->full) docHistory.push_back(docCurrent);
    docCurrent = e->full;
    std::string text = entryText(*e);
    docView->send(SCI_SETREADONLY, 0);
    docView->send(SCI_SETTEXT, 0, (intptr_t)text.c_str());
    docView->send(SCI_STARTSTYLING, 0);
    docView->send(SCI_SETSTYLING, text.size(), DS_TEXT);
    auto style = [&](size_t from, size_t len, int st) {
        docView->send(SCI_STARTSTYLING, from);
        docView->send(SCI_SETSTYLING, len, st);
    };
    // the title line
    size_t eol = text.find('\n');
    style(0, eol == std::string::npos ? text.size() : eol, DS_TITLE);
    // section headings: an indented line in capitals (NAME, SYNOPSIS, FUNCTION, ...)
    size_t pos = 0;
    while (pos < text.size()) {
        size_t le = text.find('\n', pos);
        if (le == std::string::npos) le = text.size();
        size_t a = pos;
        while (a < le && (text[a] == ' ' || text[a] == '\t')) a++;
        bool heading = a > pos && le > a;
        for (size_t k = a; k < le && heading; k++)
            if (!(isupper((unsigned char)text[k]) || text[k] == ' ')) heading = false;
        if (heading) style(a, le - a, DS_HEADING);
        pos = le + 1;
    }
    // links: "something.library/Name" and "Name()" where Name is in the index
    pos = eol == std::string::npos ? text.size() : eol;
    while (pos < text.size()) {
        if (!isalpha((unsigned char)text[pos]) || (pos > 0 && (isalnum((unsigned char)text[pos - 1]) || text[pos - 1] == '_'))) {
            pos++;
            continue;
        }
        size_t a = pos;
        while (pos < text.size() && (isalnum((unsigned char)text[pos]) || strchr("_./-", text[pos]))) pos++;
        std::string word = text.substr(a, pos - a);
        while (!word.empty() && strchr("./-", word.back())) word.pop_back();
        bool call = pos < text.size() && text[pos] == '(';
        bool qualified = word.find('/') != std::string::npos;
        if ((qualified || call) && word != e->func && findDoc(word))
            style(a, word.size(), DS_LINK);
    }
    docView->send(SCI_SETREADONLY, 1);
    docView->send(SCI_SETEMPTYSELECTION, 0);
    docView->send(SCI_GOTOPOS, 0);
    docView->send(SCI_SETFIRSTVISIBLELINE, 0);
    setGadgetIn(docWin, docGad[DG_BACK], GA_Disabled, docHistory.empty());
    static std::string title;
    title = "AutoDocs - " + e->full;
    IIntuition->SetAttrs(docWinObj, WA_Title, (uint32)title.c_str(), TAG_DONE);
    docView->tick();
}

void App::lookupDoc(const std::string &word)
{
    const DocEntry *e = findDoc(word);
    if (e) {
        showDoc(e, true);
        return;
    }
    if (!loadDocIndex()) {
        info("The AutoDocs come with the AmigaOS 4.1 SDK, which isn't installed (there is no SDK: assign).");
        return;
    }
    // nothing exact: show the browser filtered by the word
    openDocWindow();
    if (docGad[DG_SEARCH])
        IIntuition->SetGadgetAttrs((struct Gadget *)docGad[DG_SEARCH], docWin, nullptr, STRINGA_TextVal, (uint32)word.c_str(), TAG_DONE);
    showDocMatches(word);
    setStatus("No AutoDoc entry called " + word + " - showing the closest names");
}

void App::setGadgetIn(struct Window *w, Object *g, uint32_t tag, uint32_t value)
{
    if (g && w) IIntuition->SetGadgetAttrs((struct Gadget *)g, w, nullptr, tag, value, TAG_DONE);
}

void App::handleDocWindow()
{
    uint32 result;
    uint16 code;
    bool close = false;
    while ((result = IIntuition->IDoMethod(docWinObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            close = true;
            break;
        case WMHI_NEWSIZE:
            layoutDocView();
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case DG_SEARCH: {
                uint32 v = 0;
                IIntuition->GetAttr(STRINGA_TextVal, docGad[DG_SEARCH], &v);
                std::string s = trim(v ? (const char *)v : "");
                showDocMatches(s);
                if (const DocEntry *e = findDoc(s)) showDoc(e, true);
                break;
            }
            case DG_LIST: {
                intptr_t i = (intptr_t)docList.userData(docList.selected()) - 1;
                if (i >= 0 && i < (intptr_t)docIndex.size()) showDoc(&docIndex[i], true);
                break;
            }
            case DG_BACK:
                if (!docHistory.empty()) {
                    std::string prev = docHistory.back();
                    docHistory.pop_back();
                    showDoc(findDoc(prev), false);
                }
                break;
            case DG_SDKDOCS:
                openSdkDocs();
                break;
            }
            break;
        }
    }
    if (pendingDocLink >= 0 && !close) {
        intptr_t at = pendingDocLink;
        pendingDocLink = -1;
        docLinkClicked(at);
    }
    if (docView) docView->tick();
    if (close) closeDocWindow();
}

// The SDK's documentation drawer (with the SDK manual PDF) in a Workbench window.
void App::openSdkDocs()
{
    if (!deviceExists("SDK")) {
        info("The AmigaOS 4.1 SDK isn't installed (there is no SDK: assign).");
        return;
    }
    if (!IWorkbench->OpenWorkbenchObjectA("SDK:Documentation", nullptr))
        info("Workbench couldn't open SDK:Documentation.");
}

// F1 in the editor: the identifier under the caret (including "IExec->" style prefixes)
void App::docForWordAtCaret()
{
    if (!editor || !cur) {
        openDocWindow();
        return;
    }
    intptr_t pos = editor->send(SCI_GETCURRENTPOS);
    intptr_t a = editor->send(SCI_WORDSTARTPOSITION, pos, 1), b = editor->send(SCI_WORDENDPOSITION, pos, 1);
    if (b <= a) {
        openDocWindow();
        return;
    }
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr;
    tr.chrg.cpMin = a;
    tr.chrg.cpMax = b;
    tr.lpstrText = buf.data();
    editor->send(SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    lookupDoc(buf.data());
}

void App::docLinkClicked(intptr_t position)
{
    if (!docView) return;
    // the whole link run around the click
    intptr_t len = docView->send(SCI_GETLENGTH);
    intptr_t a = position, b = position;
    while (a > 0 && docView->send(SCI_GETSTYLEAT, a - 1) == DS_LINK) a--;
    while (b < len && docView->send(SCI_GETSTYLEAT, b) == DS_LINK) b++;
    if (b <= a) return;
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr;
    tr.chrg.cpMin = a;
    tr.chrg.cpMax = b;
    tr.lpstrText = buf.data();
    docView->send(SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    lookupDoc(buf.data());
}

} // namespace af
