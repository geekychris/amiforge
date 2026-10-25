// AmiForge's ARexx port "AMIFORGE" (arexx.class) and the Macros menu.
//
// Every command is registered with arexx.class with the template "ARGS/F", so the class
// hands us the raw arguments; RexxCmd.cpp parses them against our own templates (tested on
// the host). Macros are .rexx files in PROGDIR:Rexx/ and in the project's Rexx/ drawer;
// they run with AMIFORGE as their host, so they can send any of these commands.
#include "App.h"
#include "Commands.h"
#include "RexxCmd.h"
#include "Integrations.h"

#include <string.h>
#include <strings.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/arexx.h>
#include <classes/arexx.h>
#include <rexx/storage.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static App *gRexxApp;
static std::vector<struct ARexxCmd> gCmds;
static std::string gPortName;

static void rexxDispatch(struct ARexxCmd *ac, struct RexxMsg *)
{
    App *a = gRexxApp;
    if (!a) return;
    std::string line = ac->ac_Name;
    if (ac->ac_ArgList && ac->ac_ArgList[0]) line += std::string(" ") + (const char *)ac->ac_ArgList[0];
    int rc = 0;
    a->rexxResult = a->rexxCommand(line, rc);
    ac->ac_RC = rc;
    ac->ac_RC2 = 0;
    ac->ac_Result = rc == 0 ? (STRPTR)a->rexxResult.c_str() : nullptr;
}

bool App::openRexxPort()
{
    gRexxApp = this;
    gCmds.clear();
    uint16 id = 1;
    for (auto &c : rexx::commands()) {
        struct ARexxCmd ac = {};
        ac.ac_Name = c.name;
        ac.ac_ID = id++;
        ac.ac_Func = rexxDispatch;
        ac.ac_ArgTemplate = "ARGS/F";
        gCmds.push_back(ac);
    }
    gCmds.push_back({});   // the end
    Class *cl = IARexx->AREXX_GetClass();
    uint32 err = 0;
    rexxObj = IIntuition->NewObject(cl, nullptr, AREXX_HostName, (uint32)"AMIFORGE", AREXX_NoSlot, TRUE,
                                    AREXX_Commands, (uint32)gCmds.data(), AREXX_ErrorCode, (uint32)&err, TAG_DONE);
    if (!rexxObj && err == RXERR_PORT_ALREADY_EXISTS)   // another AmiForge: AMIFORGE.1, .2...
        rexxObj = IIntuition->NewObject(cl, nullptr, AREXX_HostName, (uint32)"AMIFORGE",
                                        AREXX_Commands, (uint32)gCmds.data(), TAG_DONE);
    if (!rexxObj) return false;
    uint32 name = 0;
    IIntuition->GetAttr(AREXX_HostName, rexxObj, &name);
    gPortName = name ? (const char *)name : "AMIFORGE";
    return true;
}

void App::closeRexxPort()
{
    if (rexxObj) IIntuition->DisposeObject(rexxObj);
    rexxObj = nullptr;
    gRexxApp = nullptr;
}

uint32_t rexxSignals(App *a)
{
    uint32 s = 0;
    if (a->rexxObj) IIntuition->GetAttr(AREXX_SigMask, a->rexxObj, &s);
    return s;
}

void rexxHandle(App *a)
{
    if (a->rexxObj) IIntuition->IDoMethod(a->rexxObj, AM_HANDLEEVENT);
}

std::string rexxPortName() { return gPortName; }

static std::string textRange(SciView *ed, intptr_t a, intptr_t b)
{
    if (b <= a) return "";
    std::vector<char> buf(b - a + 1);
    struct Sci_TextRangeFull tr = { { a, b }, buf.data() };
    S(ed, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return std::string(buf.data(), b - a);
}

static std::string unescape(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') { o += '\n'; i++; continue; }
            if (n == 't') { o += '\t'; i++; continue; }
            if (n == '\\') { o += '\\'; i++; continue; }
        }
        o += s[i];
    }
    return o;
}

// Run one command; returns RESULT, sets rc (0 ok, 5 warn/not found, 10 error, 20 failure).
std::string App::rexxCommand(const std::string &line, int &rc)
{
    std::string cmd, rest;
    rexx::splitCommand(line, cmd, rest);
    const rexx::CommandDef *def = rexx::findCommand(cmd);
    rc = 0;
    if (!def) { rc = 20; return ""; }
    rexx::Args a;
    std::string err;
    if (!rexx::parseArgs(def->tmpl, rest, a, err)) {
        setStatus("ARexx " + cmd + ": " + err);
        rc = 10;
        return "";
    }
    bool needDoc = cmd == "SAVE" || cmd == "SAVEAS" || cmd == "CLOSE" || cmd == "GOTO" || cmd == "GETLINE" ||
                   cmd == "GETCOL" || cmd == "GETTEXT" || cmd == "GETLINETEXT" || cmd == "GETWORD" || cmd == "INSERT" ||
                   cmd == "REPLACE" || cmd == "SELECT" || cmd == "FIND";
    if (needDoc && (!cur || !editor)) { rc = 10; return ""; }
    if ((cmd == "INSERT" || cmd == "REPLACE") && (cur->hex || cur->readOnly)) { rc = 10; return ""; }
    intptr_t pos = editor ? S(editor, SCI_GETCURRENTPOS) : 0;

    if (cmd == "OPEN") { rc = openFile(a.str("FILE")) ? 0 : 10; return ""; }
    if (cmd == "SAVE") { rc = saveDocument(cur) ? 0 : 10; return ""; }
    if (cmd == "SAVEAS") {
        std::string p = a.str("FILE");
        if (dirName(p).empty() && !cur->path.empty()) p = joinPath(dirName(cur->path), p);
        std::string old = cur->path;
        cur->path = absolutePath(p);
        if (!saveDocument(cur)) { cur->path = old; rc = 10; }
        return cur->path;
    }
    if (cmd == "CLOSE") {
        if (a.has("FORCE")) cur->modified = false;
        rc = closeDocument(cur) ? 0 : 5;
        return "";
    }
    if (cmd == "GOTO") {
        long ln = std::max(1L, a.num("LINE")), col = std::max(1L, a.num("COL", 1));
        intptr_t l = std::min<intptr_t>(ln - 1, S(editor, SCI_GETLINECOUNT) - 1);
        S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, l);
        S(editor, SCI_GOTOPOS, S(editor, SCI_FINDCOLUMN, l, col - 1));
        return "";
    }
    if (cmd == "GETLINE") return std::to_string((long)S(editor, SCI_LINEFROMPOSITION, pos) + 1);
    if (cmd == "GETCOL") return std::to_string((long)S(editor, SCI_GETCOLUMN, pos) + 1);
    if (cmd == "GETFILE") return cur ? cur->path : "";
    if (cmd == "GETTEXT") {
        intptr_t s = S(editor, SCI_GETSELECTIONSTART), e = S(editor, SCI_GETSELECTIONEND);
        if (a.has("ALL")) return textRange(editor, 0, S(editor, SCI_GETLENGTH));
        return textRange(editor, s, e);
    }
    if (cmd == "GETLINETEXT") {
        intptr_t l = a.has("LINE") ? a.num("LINE") - 1 : S(editor, SCI_LINEFROMPOSITION, pos);
        if (l < 0 || l >= S(editor, SCI_GETLINECOUNT)) { rc = 5; return ""; }
        return textRange(editor, S(editor, SCI_POSITIONFROMLINE, l), S(editor, SCI_GETLINEENDPOSITION, l));
    }
    if (cmd == "GETWORD") return wordAt(pos);
    if (cmd == "INSERT") {
        std::string t = unescape(a.str("TEXT"));
        S(editor, SCI_INSERTTEXT, pos, (intptr_t)t.c_str());
        S(editor, SCI_GOTOPOS, pos + (intptr_t)t.size());
        return "";
    }
    if (cmd == "REPLACE") {
        std::string t = unescape(a.str("TEXT"));
        S(editor, SCI_REPLACESEL, 0, (intptr_t)t.c_str());
        return "";
    }
    if (cmd == "SELECT") {
        intptr_t n = S(editor, SCI_GETLINECOUNT);
        intptr_t l0 = std::max(0L, a.num("FROMLINE") - 1), l1 = std::min<intptr_t>(a.num("TOLINE") - 1, n - 1);
        if (l1 < l0) { rc = 5; return ""; }
        intptr_t e = l1 + 1 < n ? S(editor, SCI_POSITIONFROMLINE, l1 + 1) : S(editor, SCI_GETLENGTH);
        S(editor, SCI_SETSEL, S(editor, SCI_POSITIONFROMLINE, l0), e);
        return "";
    }
    if (cmd == "FIND") {
        std::string t = a.str("TEXT");
        S(editor, SCI_SETSEARCHFLAGS, 0);
        intptr_t from = S(editor, SCI_GETSELECTIONEND);
        S(editor, SCI_SETTARGETRANGE, from, S(editor, SCI_GETLENGTH));
        intptr_t r = S(editor, SCI_SEARCHINTARGET, t.size(), (intptr_t)t.c_str());
        if (r < 0) { rc = 5; return ""; }
        S(editor, SCI_SETSEL, S(editor, SCI_GETTARGETSTART), S(editor, SCI_GETTARGETEND));
        S(editor, SCI_SCROLLCARET);
        return std::to_string((long)S(editor, SCI_LINEFROMPOSITION, r) + 1);
    }
    if (cmd == "BUILD") { queueCommand(CMD_BUILD); return ""; }
    if (cmd == "RUN") { queueCommand(CMD_RUN); return ""; }
    if (cmd == "DEBUG") { queueCommand(CMD_DEBUG_START); return ""; }
    if (cmd == "STOP") { queueCommand(CMD_DEBUG_STOP); return ""; }
    if (cmd == "PROJECT") {
        std::string p = a.str("FILE");
        if (!fileExists(p)) { rc = 10; return ""; }
        openProject(p);
        return project.path;
    }
    if (cmd == "GETPROJECT") return project.path;
    if (cmd == "STATUS") { setStatus(a.str("TEXT")); return ""; }
    if (cmd == "REQUEST") return ask("ARexx", unescape(a.str("TEXT")), "_OK|_Cancel") == 1 ? "1" : "0";
    if (cmd == "QUIT") {
        if (a.has("FORCE")) for (auto &d : docs) d->modified = false;
        queueCommand(CMD_QUIT);
        return "";
    }
    if (cmd == "HELP") {
        if (a.has("COMMAND")) {
            const rexx::CommandDef *d = rexx::findCommand(a.str("COMMAND"));
            if (!d) { rc = 5; return ""; }
            return std::string(d->name) + (*d->tmpl ? std::string(" ") + d->tmpl : "") + " - " + d->help;
        }
        return rexx::helpText();
    }
    rc = 20;
    return "";
}

// --- macros ---

void App::scanMacros()
{
    macroFiles.clear();
    std::vector<std::string> dirs = { "PROGDIR:Rexx" };
    if (!project.dir.empty()) dirs.push_back(joinPath(project.dir, "Rexx"));
    for (auto &d : dirs) {
        BPTR lock = IDOS->Lock(d.c_str(), SHARED_LOCK);
        if (!lock) continue;
        APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, EX_DoCurrentDir, TRUE, TAG_END);
        std::vector<std::string> found;
        if (ctx) {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx)))
                if (EXD_IS_FILE(ed) && (extension((const char *)ed->Name) == "rexx" || extension((const char *)ed->Name) == "afrx"))
                    found.push_back(joinPath(d, (const char *)ed->Name));
            IDOS->ReleaseDirContext(ctx);
        }
        IDOS->UnLock(lock);
        std::sort(found.begin(), found.end());
        for (auto &f : found)
            if (macroFiles.size() < (size_t)(CMD_MACRO_LAST - CMD_MACRO_FIRST + 1)) macroFiles.push_back(f);
    }
}

void App::runMacro(const std::string &path)
{
    if (!rexxObj) { info("The ARexx port couldn't be opened, so macros can't run (is RexxMast running?)."); return; }
    std::string p = absolutePath(path);
    int32 rc = 0, rc2 = 0;
    STRPTR result = nullptr;
    IIntuition->IDoMethod(rexxObj, AM_EXECUTE, (uint32)p.c_str(), (uint32)"REXX", (uint32)&rc, (uint32)&rc2, (uint32)&result, 0);
    if (rc) setStatus(strprintf("Couldn't start %s (ARexx error %ld; is RexxMast running?)", fileName(p).c_str(), (long)rc2));
    else setStatus("Running " + fileName(p));
}

// The Macros menu (rebuildMenus calls this before the Help menu)
static std::vector<std::string> gMacroLabels;

void appendMacroMenu(App *a, std::vector<struct NewMenu> &m)
{
    a->scanMacros();
    gMacroLabels.clear();
    gMacroLabels.reserve(a->macroFiles.size());
    for (auto &f : a->macroFiles) {
        std::string n = fileName(f);
        size_t dot = n.rfind('.');
        gMacroLabels.push_back(dot == std::string::npos ? n : n.substr(0, dot));
    }
    m.push_back({ NM_TITLE, (STRPTR)"Macros", 0, 0, 0, 0 });
    for (size_t i = 0; i < gMacroLabels.size(); i++)
        m.push_back({ NM_ITEM, (STRPTR)gMacroLabels[i].c_str(), 0, 0, 0, (APTR)(uint32)(CMD_MACRO_FIRST + i) });
    if (gMacroLabels.empty()) m.push_back({ NM_ITEM, (STRPTR)"(no macros in Rexx/)", 0, NM_ITEMDISABLED, 0, 0 });
    m.push_back({ NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 });
    m.push_back({ NM_ITEM, (STRPTR)"Rescan Macros", 0, 0, 0, (APTR)CMD_MACROS_RESCAN });
    m.push_back({ NM_ITEM, (STRPTR)"Open Macros Drawer", 0, 0, 0, (APTR)CMD_MACROS_FOLDER });
    m.push_back({ NM_ITEM, (STRPTR)"ARexx Commands...", 0, 0, 0, (APTR)CMD_REXX_HELP });
}

} // namespace af
