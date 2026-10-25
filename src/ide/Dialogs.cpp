// Requesters and small windows: file requesters, message/string/number requesters,
// Find & Replace, Project Settings, About.
#include "App.h"
#include "Commands.h"

#include <string.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <proto/requester.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <proto/space.h>
#include <gadgets/space.h>
#include <libraries/asl.h>
#include <classes/requester.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <gadgets/checkbox.h>
#include <images/label.h>
#include <workbench/startup.h>

#include "Scintilla.h"

namespace af {

static Object *gap()
{
    return IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, SPACE_MinWidth, 12, TAG_DONE);
}

// --- ASL file requesters ---

std::string App::requestFile(const char *title, bool save, const std::string &initial, const char *pattern)
{
    std::string drawer = initial, file;
    if (!initial.empty() && (extension(initial) != "" || save)) {
        drawer = dirName(initial);
        file = fileName(initial);
    }
    struct FileRequester *fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (uint32)title,
        ASLFR_Window, (uint32)win, ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save,
        ASLFR_InitialDrawer, (uint32)drawer.c_str(),
        ASLFR_InitialFile, (uint32)file.c_str(),
        pattern ? ASLFR_DoPatterns : TAG_IGNORE, TRUE,
        pattern ? ASLFR_InitialPattern : TAG_IGNORE, (uint32)pattern,
        TAG_END);
    if (!fr) return "";
    std::string result;
    if (IAsl->AslRequestTags(fr, TAG_END) && fr->fr_File && fr->fr_File[0])
        result = joinPath(fr->fr_Drawer ? (const char *)fr->fr_Drawer : "", fr->fr_File);
    IAsl->FreeAslRequest(fr);
    return result;
}

std::string App::requestDrawer(const char *title, const std::string &initial)
{
    struct FileRequester *fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (uint32)title,
        ASLFR_Window, (uint32)win, ASLFR_SleepWindow, TRUE,
        ASLFR_DrawersOnly, TRUE,
        ASLFR_InitialDrawer, (uint32)initial.c_str(),
        TAG_END);
    if (!fr) return "";
    std::string result;
    if (IAsl->AslRequestTags(fr, TAG_END) && fr->fr_Drawer && fr->fr_Drawer[0]) result = fr->fr_Drawer;
    IAsl->FreeAslRequest(fr);
    return result;
}

std::vector<std::string> App::requestFiles(const char *title, const std::string &drawer)
{
    std::vector<std::string> out;
    struct FileRequester *fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (uint32)title,
        ASLFR_Window, (uint32)win, ASLFR_SleepWindow, TRUE,
        ASLFR_DoMultiSelect, TRUE,
        ASLFR_InitialDrawer, (uint32)drawer.c_str(),
        TAG_END);
    if (!fr) return out;
    if (IAsl->AslRequestTags(fr, TAG_END)) {
        if (fr->fr_NumArgs > 0) {
            for (int i = 0; i < fr->fr_NumArgs; i++) {
                char dir[1024] = "";
                if (fr->fr_ArgList[i].wa_Lock) IDOS->NameFromLock(fr->fr_ArgList[i].wa_Lock, dir, sizeof dir);
                out.push_back(joinPath(dir[0] ? std::string(dir) : std::string(fr->fr_Drawer), fr->fr_ArgList[i].wa_Name));
            }
        } else if (fr->fr_File && fr->fr_File[0]) {
            out.push_back(joinPath(fr->fr_Drawer, fr->fr_File));
        }
        if (!out.empty()) lastDir = dirName(out.front());
    }
    IAsl->FreeAslRequest(fr);
    return out;
}

// --- requester.class ---

static uint32 openReq(struct Window *win, Object *req)
{
    struct orRequest msg = { RM_OPENREQ, nullptr, win, win ? win->WScreen : nullptr };
    return IIntuition->IDoMethodA(req, (Msg)&msg);
}

int App::ask(const char *title, const std::string &body, const char *buttons)
{
    // requester.class formats its body text: protect '%'
    std::string text;
    for (char c : body) { text += c; if (c == '%') text += '%'; }
    Object *req = IIntuition->NewObject(IRequester->REQUESTER_GetClass(), nullptr,
        REQ_Type, REQTYPE_INFO, REQ_TitleText, (uint32)title, REQ_BodyText, (uint32)text.c_str(),
        REQ_GadgetText, (uint32)buttons, REQ_Image, REQIMAGE_QUESTION, TAG_DONE);
    if (!req) return 0;
    int r = (int)openReq(win, req);
    IIntuition->DisposeObject(req);
    return r;
}

// String and number input: a small window of our own, because requester.class before 53.20
// (as in AmigaOS 4.1 Final Edition) ignores REQS_ReturnEnds, so Return would not confirm.
// The text is marked when the window opens, so typing replaces it; Return means OK.
static bool inputWindow(struct Window *parent, const char *title, const std::string &body,
                        std::string &value, bool numeric)
{
    enum { IN_STRING = 1, IN_OK, IN_CANCEL };
    Class *layout = ILayout->LAYOUT_GetClass();
    Object *str = nullptr;
    Object *root = IIntuition->NewObject(layout, nullptr,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddImage, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr,
            LABEL_Text, (uint32)body.c_str(), TAG_DONE),
        LAYOUT_AddChild, str = IIntuition->NewObject(IString->STRING_GetClass(), nullptr,
            GA_ID, IN_STRING, GA_RelVerify, TRUE,
            STRINGA_MaxChars, 511, STRINGA_TextVal, (uint32)value.c_str(),
            STRINGA_MarkActive, TRUE,
            numeric ? STRINGA_Justification : TAG_IGNORE, GACT_STRINGCENTER,
            TAG_DONE),
        CHILD_MinWidth, 320,
        LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, IN_OK, GA_RelVerify, TRUE, GA_Text, (uint32)"_OK", TAG_DONE),
            LAYOUT_AddChild, gap(),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, IN_CANCEL, GA_RelVerify, TRUE, GA_Text, (uint32)"_Cancel", TAG_DONE),
            TAG_DONE),
        TAG_DONE);
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)title, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_Activate, TRUE, WA_PubScreen, (uint32)(parent ? parent->WScreen : nullptr),
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_VANILLAKEY,
        WINDOW_Position, parent ? WPOS_CENTERWINDOW : WPOS_CENTERSCREEN,
        parent ? WINDOW_RefWindow : TAG_IGNORE, (uint32)parent,
        WINDOW_Layout, root,
        TAG_DONE);
    if (!wo) return false;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        return false;
    }
    ILayout->ActivateLayoutGadget((struct Gadget *)root, w, nullptr, (uint32)str);
    uint32 sig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, wo, &sig);
    int done = 0;   // 1 OK, -1 cancel
    while (!done) {
        IExec->Wait(sig);
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                done = -1;
                break;
            case WMHI_GADGETUP:
                // the string gadget reports GADGETUP when Return ends editing
                if ((result & WMHI_GADGETMASK) == IN_STRING || (result & WMHI_GADGETMASK) == IN_OK) done = 1;
                if ((result & WMHI_GADGETMASK) == IN_CANCEL) done = -1;
                break;
            case WMHI_VANILLAKEY:
                if ((result & WMHI_KEYMASK) == 27) done = -1;   // Esc
                break;
            }
        }
    }
    if (done == 1) {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, str, &v);
        value = v ? (const char *)v : "";
    }
    IIntuition->DisposeObject(wo);
    return done == 1;
}

bool App::askString(const char *title, const std::string &body, std::string &value)
{
    std::string v = value;
    if (!inputWindow(win, title, body, v, false)) return false;
    value = v;
    return true;
}

bool App::askInteger(const char *title, const std::string &body, int &value, int min, int max)
{
    std::string v = strprintf("%ld", (long)value);
    for (;;) {
        if (!inputWindow(win, title, body, v, true)) return false;
        char *end = nullptr;
        long n = strtol(trim(v).c_str(), &end, 10);
        if (end && *end == '\0' && !trim(v).empty() && n >= min && n <= max) {
            value = (int)n;
            return true;
        }
        info(strprintf("Please enter a number from %ld to %ld.", (long)min, (long)max));
    }
}

void App::gotoLineDialog()
{
    if (!cur || !editor) return;
    int lines = (int)editor->send(SCI_GETLINECOUNT);
    int line = (int)editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS)) + 1;
    if (!askInteger("Go to Line", strprintf("Line number (1-%d):", lines), line, 1, lines)) return;
    if (cur->path.empty())   // untitled: gotoLocation needs a file
        editor->send(SCI_GOTOLINE, line - 1);
    else
        gotoLocation(cur->path, line);
}

void App::about()
{
    ask("About AmiForge",
        "AmiForge 0.1 - a native IDE for AmigaOS 4.1\n\n"
        "Editing: Scintilla 5.5.7 and Lexilla 5.4.5 by Neil Hodgson\n"
        "Building: the AmigaOS 4.1 SDK's GCC 11.5 (C, C++) and GNU as\n"
        "Debugging: AmiForge's own debugger on the exec debug interface\n\n"
        "F5 debug/continue  F9 breakpoint  F10 step over\n"
        "F11 step into  Shift-F11 step out  F7 build  Ctrl-F5 run",
        "_OK");
}

// --- Find & Replace window ---

enum { FG_FIND = 1, FG_REPLACE, FG_CASE, FG_WORD, FG_REGEX, FG_NEXT, FG_PREV, FG_REPL1, FG_REPLALL, FG_CLOSE };

void App::openFindWindow(bool replace)
{
    // seed the search text with the selection
    if (cur && editor) {
        intptr_t a = editor->send(SCI_GETSELECTIONSTART), b = editor->send(SCI_GETSELECTIONEND);
        if (b > a && b - a < 200) {
            std::vector<char> buf(b - a + 1);
            editor->send(SCI_GETSELTEXT, 0, (intptr_t)buf.data());
            if (!strchr(buf.data(), '\n')) findText = buf.data();
        }
    }
    if (findWinObj) {
        IIntuition->SetGadgetAttrs((struct Gadget *)findGad[FG_FIND], findWin, nullptr,
                                   STRINGA_TextVal, (uint32)findText.c_str(), TAG_DONE);
        IIntuition->WindowToFront(findWin);
        IIntuition->ActivateWindow(findWin);
        ILayout->ActivateLayoutGadget((struct Gadget *)findGad[0], findWin, nullptr, (uint32)findGad[FG_FIND]);
        return;
    }
    Class *string = IString->STRING_GetClass();
    Class *check = ICheckBox->CHECKBOX_GetClass();
    Class *layout = ILayout->LAYOUT_GetClass();
    auto label = [](const char *t) {
        return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE);
    };
    auto btn = [](uint32 id, const char *t) {
        return IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE,
                                     GA_Text, (uint32)t, TAG_DONE);
    };
    findWinObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)(replace ? "Find & Replace" : "Find"),
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_PubScreen, (uint32)win->WScreen,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, findGad[0] = IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, findGad[FG_FIND] = IIntuition->NewObject(string, nullptr,
                GA_ID, FG_FIND, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
                STRINGA_MaxChars, 255, STRINGA_TextVal, (uint32)findText.c_str(), TAG_DONE),
            CHILD_Label, label("_Find"),
            CHILD_MinWidth, 300,
            LAYOUT_AddChild, findGad[FG_REPLACE] = IIntuition->NewObject(string, nullptr,
                GA_ID, FG_REPLACE, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
                STRINGA_MaxChars, 255, STRINGA_TextVal, (uint32)replaceText.c_str(), TAG_DONE),
            CHILD_Label, label("Replace _with"),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, findGad[FG_CASE] = IIntuition->NewObject(check, nullptr, GA_ID, FG_CASE,
                    GA_RelVerify, TRUE, GA_Text, (uint32)"Match _case", GA_Selected, findCase, TAG_DONE),
                LAYOUT_AddChild, findGad[FG_WORD] = IIntuition->NewObject(check, nullptr, GA_ID, FG_WORD,
                    GA_RelVerify, TRUE, GA_Text, (uint32)"Whole _word", GA_Selected, findWord, TAG_DONE),
                LAYOUT_AddChild, findGad[FG_REGEX] = IIntuition->NewObject(check, nullptr, GA_ID, FG_REGEX,
                    GA_RelVerify, TRUE, GA_Text, (uint32)"Regular e_xpression", GA_Selected, findRegex, TAG_DONE),
                TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, findGad[FG_NEXT] = btn(FG_NEXT, "Find _Next"),
                LAYOUT_AddChild, findGad[FG_PREV] = btn(FG_PREV, "Find _Previous"),
                LAYOUT_AddChild, findGad[FG_REPL1] = btn(FG_REPL1, "_Replace"),
                LAYOUT_AddChild, findGad[FG_REPLALL] = btn(FG_REPLALL, "Replace _All"),
                LAYOUT_AddChild, findGad[FG_CLOSE] = btn(FG_CLOSE, "Close"),
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);
    if (!findWinObj) return;
    findWin = (struct Window *)IIntuition->IDoMethod(findWinObj, WM_OPEN);
    if (!findWin) {
        IIntuition->DisposeObject(findWinObj);
        findWinObj = nullptr;
        return;
    }
    ILayout->ActivateLayoutGadget((struct Gadget *)findGad[0], findWin, nullptr, (uint32)findGad[FG_FIND]);
}

void App::closeFindWindow()
{
    if (findWinObj) IIntuition->DisposeObject(findWinObj);
    findWinObj = nullptr;
    findWin = nullptr;
    memset(findGad, 0, sizeof findGad);
}

void App::handleFindWindow()
{
    uint32 result;
    uint16 code;
    bool close = false;
    auto readFields = [this]() {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, findGad[FG_FIND], &v);
        findText = v ? (const char *)v : "";
        IIntuition->GetAttr(STRINGA_TextVal, findGad[FG_REPLACE], &v);
        replaceText = v ? (const char *)v : "";
        IIntuition->GetAttr(GA_Selected, findGad[FG_CASE], &v); findCase = v;
        IIntuition->GetAttr(GA_Selected, findGad[FG_WORD], &v); findWord = v;
        IIntuition->GetAttr(GA_Selected, findGad[FG_REGEX], &v); findRegex = v;
    };
    while ((result = IIntuition->IDoMethod(findWinObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            close = true;
            break;
        case WMHI_GADGETUP:
            readFields();
            switch (result & WMHI_GADGETMASK) {
            case FG_FIND: if (code == 0) findNext(false); break;   // Return in the field (not Tab)
            case FG_NEXT: findNext(false); break;
            case FG_PREV: findNext(true); break;
            case FG_REPL1: replaceOne(); break;
            case FG_REPLALL: replaceAll(); break;
            case FG_CLOSE: close = true; break;
            }
            break;
        case WMHI_RAWKEY:
            if (code == 0x45) close = true;   // Esc
            break;
        }
    }
    if (close) closeFindWindow();
}

// --- Project settings (a small modal window) ---

bool App::editProjectSettings()
{
    enum { PS_NAME = 1, PS_OUTPUT, PS_CC, PS_CXX, PS_CFLAGS, PS_CXXFLAGS, PS_ASFLAGS, PS_LDFLAGS, PS_LIBS,
           PS_ARGS, PS_CONSOLE, PS_STOPMAIN, PS_OK, PS_CANCEL, PS_TRACKLEAKS /* debugtools */, PS_RUNCMD /* designer */, PS_N };
    Object *g[PS_N] = {};
    Class *string = IString->STRING_GetClass();
    Class *layout = ILayout->LAYOUT_GetClass();
    auto label = [](const char *t) {
        return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE);
    };
    auto field = [&](int id, const std::string &v) {
        return g[id] = IIntuition->NewObject(string, nullptr, GA_ID, id, GA_TabCycle, TRUE,
                                             STRINGA_MaxChars, 1023, STRINGA_TextVal, (uint32)v.c_str(), TAG_DONE);
    };
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"Project Settings", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, field(PS_NAME, project.name), CHILD_Label, label("Name"), CHILD_MinWidth, 420,
            LAYOUT_AddChild, field(PS_OUTPUT, project.output), CHILD_Label, label("Program file"),
            LAYOUT_AddChild, field(PS_CC, project.cc), CHILD_Label, label("C compiler"),
            LAYOUT_AddChild, field(PS_CXX, project.cxx), CHILD_Label, label("C++ compiler"),
            LAYOUT_AddChild, field(PS_CFLAGS, project.cflags), CHILD_Label, label("C flags"),
            LAYOUT_AddChild, field(PS_CXXFLAGS, project.cxxflags), CHILD_Label, label("C++ flags"),
            LAYOUT_AddChild, field(PS_ASFLAGS, project.asflags), CHILD_Label, label("Assembler flags"),
            LAYOUT_AddChild, field(PS_LDFLAGS, project.ldflags), CHILD_Label, label("Linker flags"),
            LAYOUT_AddChild, field(PS_LIBS, project.libs), CHILD_Label, label("Libraries"),
            LAYOUT_AddChild, field(PS_ARGS, project.args), CHILD_Label, label("Program arguments"),
            LAYOUT_AddChild, field(PS_CONSOLE, project.console), CHILD_Label, label("Console (empty: a window)"),
            // --- designer ---
            LAYOUT_AddChild, field(PS_RUNCMD, project.runcmd), CHILD_Label, label("Run command (empty: the program)"),
            // --- end designer ---
            LAYOUT_AddChild, g[PS_STOPMAIN] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr,
                GA_ID, PS_STOPMAIN, GA_Text, (uint32)"Stop at main() when debugging", GA_Selected, project.stopAtMain, TAG_DONE),
            // --- debugtools ---
            LAYOUT_AddChild, g[PS_TRACKLEAKS] = IIntuition->NewObject(ICheckBox->CHECKBOX_GetClass(), nullptr,
                GA_ID, PS_TRACKLEAKS, GA_Text, (uint32)"Track leaks when debugging", GA_Selected, project.trackLeaks, TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, g[PS_OK] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, PS_OK, GA_RelVerify, TRUE, GA_Text, (uint32)"_Save", TAG_DONE),
                LAYOUT_AddChild, gap(),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, g[PS_CANCEL] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, PS_CANCEL, GA_RelVerify, TRUE, GA_Text, (uint32)"_Cancel", TAG_DONE),
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);
    if (!wo) return false;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        return false;
    }
    busyPointer(true);
    uint32 sig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, wo, &sig);
    int done = 0;   // 1 save, -1 cancel
    while (!done) {
        IExec->Wait(sig);
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = -1;
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                if ((result & WMHI_GADGETMASK) == PS_OK) done = 1;
                if ((result & WMHI_GADGETMASK) == PS_CANCEL) done = -1;
            }
        }
    }
    if (done == 1) {
        auto get = [&](int id) {
            uint32 v = 0;
            IIntuition->GetAttr(STRINGA_TextVal, g[id], &v);
            return trim(v ? (const char *)v : "");
        };
        project.name = get(PS_NAME);
        project.output = get(PS_OUTPUT);
        project.cc = get(PS_CC);
        project.cxx = get(PS_CXX);
        project.cflags = get(PS_CFLAGS);
        project.cxxflags = get(PS_CXXFLAGS);
        project.asflags = get(PS_ASFLAGS);
        project.ldflags = get(PS_LDFLAGS);
        project.libs = get(PS_LIBS);
        project.args = get(PS_ARGS);
        project.console = get(PS_CONSOLE);
        project.runcmd = get(PS_RUNCMD);   // designer
        uint32 v = 0;
        IIntuition->GetAttr(GA_Selected, g[PS_STOPMAIN], &v);
        project.stopAtMain = v;
        IIntuition->GetAttr(GA_Selected, g[PS_TRACKLEAKS], &v);   // --- debugtools ---
        project.trackLeaks = v;
        rebuildMenus();
        if (project.output.empty()) project.output = project.name.empty() ? "program" : project.name;
    }
    IIntuition->DisposeObject(wo);
    busyPointer(false);
    updateTitle();
    return done == 1;
}

void App::preferences() {}

} // namespace af
