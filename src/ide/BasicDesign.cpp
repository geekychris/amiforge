// --- basic ---
// The GUI designer in a Boing BASIC project works like Visual Basic's form designer: a form
// main.afg has its code in main.bas beside it, double-clicking a control (or choosing one of
// its events) opens main.bas at Sub <id>_<Event>() - made, empty, if it isn't there yet - and
// renaming a control offers to rename its event subs. View Form / View Code (Alt-F7) switches
// between a form and its code. This file is the editor side; Designer.cpp calls it, and the
// code-behind logic itself is in BasicModel.h.
#include "App.h"
#include "BasicModel.h"
#include "Scintilla.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <dos/var.h>

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static std::string getVar(const char *name)
{
    char buf[512];
    int32 n = IDOS->GetVar(name, buf, sizeof buf, 0);
    return n >= 0 ? trim(std::string(buf, n)) : std::string();
}

bool App::basicFormMode(const std::string &afg)
{
    if (project.isBasic()) return true;
    return fileExists(basic::codeBehindOf(afg));
}

// Boing BASIC's docs/forms-events.md (the control/event contract), once: ENV:AmiForge/BoingDocs
// (its drawer), else next to the boing command, else PROGDIR:Docs. The built-in table otherwise.
void App::loadFormsEvents()
{
    if (formsEventsTried) return;
    formsEventsTried = true;
    std::vector<std::string> where;
    std::string env = getVar("AmiForge/BoingDocs");
    if (!env.empty()) where.push_back(joinPath(env, "forms-events.md"));
    std::string boing = boingCommand();
    if (!boing.empty() && boing.find_first_of(":/") != std::string::npos) {
        where.push_back(joinPath(dirName(boing), "docs/forms-events.md"));
        where.push_back(joinPath(dirName(dirName(boing)), "docs/forms-events.md"));
    }
    where.push_back("PROGDIR:Docs/forms-events.md");
    for (auto &w : where) {
        size_t colon = w.find(':');
        if (colon != std::string::npos) {
            // no "insert volume" requester for a volume or assign that isn't there
            std::string dev = w.substr(0, colon);
            struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
            bool found = IDOS->FindDosEntry(dl, dev.c_str(), LDF_ALL) != nullptr;
            IDOS->UnLockDosList(LDF_ALL | LDF_READ);
            if (!found) continue;
        }
        std::string text;
        if (!readFile(w, text)) continue;
        std::vector<basic::ControlInfo> t;
        if (basic::parseFormsEvents(text, t)) {
            basic::setControls(t);
            formsEventsFile = w;
            return;
        }
    }
}

std::string App::basicCodeText(const std::string &bas)
{
    if (Document *d = findDocument(bas)) return documentText(d);
    std::string t;
    readFile(bas, t);
    return t;
}

// Replace a document's text with `text`, as one edit of the part that changed (undo works).
void App::basicSetText(Document *d, const std::string &text)
{
    std::string old = documentText(d);
    if (old == text) return;
    size_t a = 0;
    while (a < old.size() && a < text.size() && old[a] == text[a]) a++;
    size_t eo = old.size(), en = text.size();
    while (eo > a && en > a && old[eo - 1] == text[en - 1]) { eo--; en--; }
    withDocument(d, [&] {
        S(mainView, SCI_SETTARGETRANGE, a, eo);
        S(mainView, SCI_REPLACETARGET, en - a, (intptr_t)(text.data() + a));
    });
    d->modified = true;
    rebuildTabs();
}

// The code-behind module, open in the editor (made with Option Explicit, and added to the
// project and its .bbp, if it's new).
Document *App::basicOpenCode(const std::string &afg, bool activate)
{
    std::string bas = basic::codeBehindOf(afg);
    if (!fileExists(bas)) {
        std::string base = fileName(afg);
        if (!writeFile(bas, "' The code behind " + base + ".\nOption Explicit\n")) {
            info("Can't write " + bas);
            return nullptr;
        }
    }
    if (!project.path.empty()) {
        bool changed = false;
        for (const std::string &f : { afg, bas }) {
            if (project.contains(f)) continue;
            std::string rel = project.rel(f);
            if (rel == f) continue;   // not under the project's drawer
            project.files.push_back(rel);
            changed = true;
        }
        if (changed) {
            std::string err;
            if (project.isBasic() && !project.syncBbp(err)) info(err);
            saveProject();
            refreshProjectList();
        }
    }
    return openFile(bas, activate);
}

static void bringMainWindowForward(App &app)
{
    if (!app.win) return;
    IIntuition->WindowToFront(app.win);
    IIntuition->ActivateWindow(app.win);
    app.editorFocus = true;
    if (app.editor) app.editor->setFocus(true);
}

bool App::basicGotoEvent(const std::string &afg, const std::string &id, const std::string &kind, const std::string &event)
{
    loadFormsEvents();
    const bool isForm = kind == "window";
    const basic::ControlInfo *ci = basic::controlFor(kind);
    const basic::Member *ev = ci ? ci->event(event) : nullptr;
    if (!ev) {
        setStatus(event.empty() ? "This object has no events" : "No event " + event + " for a " + kind);
        return false;
    }
    Document *d = basicOpenCode(afg, true);
    if (!d) return false;
    std::string name = basic::eventSubName(id, ev->name, isForm);
    std::string text = documentText(d);
    basic::SubPlace at;
    int indent = S(mainView, SCI_GETINDENT);
    if (indent <= 0) indent = 4;
    if (basic::ensureSub(text, name, ev->params, at, indent)) basicSetText(d, text);
    gotoLocation(d->path, at.line, 0, false);
    // the cursor at the body's indentation (an empty body line gets it)
    intptr_t line = at.line - 1;
    intptr_t ls = S(editor, SCI_POSITIONFROMLINE, line), le = S(editor, SCI_GETLINEENDPOSITION, line);
    if (ls == le) S(editor, SCI_SETLINEINDENTATION, line, at.column);
    S(editor, SCI_GOTOPOS, S(editor, SCI_GETLINEINDENTPOSITION, line));
    S(editor, SCI_SCROLLCARET);
    bringMainWindowForward(*this);
    setStatus(at.created ? "Added Sub " + name + "() to " + fileName(d->path) : "Sub " + name + "() in " + fileName(d->path));
    mcpLastSub = name;
    return true;
}

std::vector<int> App::basicEventStates(const std::string &afg, const std::string &id, const std::string &kind)
{
    loadFormsEvents();
    std::vector<int> out;
    const basic::ControlInfo *ci = basic::controlFor(kind);
    if (!ci) return out;
    std::string bas = basic::codeBehindOf(afg);
    std::vector<basic::Proc> procs = basic::procedures(basicCodeText(bas));
    const bool isForm = kind == "window";
    std::set<std::string> subs = basic::eventsWithSubs(procs, id, *ci, isForm);
    std::set<std::string> code = basic::eventsWithCode(procs, id, *ci, isForm);
    for (auto &e : ci->events) out.push_back(code.count(e.name) ? 2 : subs.count(e.name) ? 1 : 0);
    return out;
}

// After the designer renamed a control: offer to rename its event subs (and the code's uses
// of the old name). `ask` false: just do it (MCP). Returns how many subs were renamed.
int App::basicRenameControl(const std::string &afg, const std::string &oldId, const std::string &newId,
                            const std::string &kind, bool askFirst)
{
    if (oldId.empty() || newId.empty() || basic::equalsNoCase(oldId, newId)) return 0;
    std::string bas = basic::codeBehindOf(afg);
    if (!fileExists(bas) && !findDocument(bas)) return 0;
    std::string text = basicCodeText(bas);
    std::vector<basic::Proc> procs = basic::procedures(text);
    std::vector<std::string> subs = basic::controlSubs(procs, oldId, nullptr);
    int refs = (int)basic::findIdentifier(text, oldId).size();
    if (subs.empty() && refs == 0) return 0;
    if (askFirst) {
        std::string list;
        for (size_t i = 0; i < subs.size() && i < 6; i++) list += "    " + subs[i] + " -> " + newId + subs[i].substr(oldId.size()) + "\n";
        if (subs.size() > 6) list += "    ...\n";
        std::string body = fileName(bas) + " has code for " + oldId + ":\n" + list +
                           (refs ? strprintf("and %d use%s of %s in the code.\n", refs, refs == 1 ? "" : "s", oldId.c_str()) : "") +
                           "\nRename them to " + newId + " as well?";
        if (ask("Rename Control", body, "_Rename|_Leave the Code") != 1) return 0;
    }
    int nSubs = 0, nRefs = 0;
    std::string out = basic::renameControl(text, oldId, newId, true, &nSubs, &nRefs);
    Document *d = findDocument(bas);
    if (!d) d = openFile(bas, false);
    if (d) basicSetText(d, out);
    else writeFile(bas, out);
    setStatus(strprintf("Renamed %d event sub%s and %d use%s of %s in %s", nSubs, nSubs == 1 ? "" : "s", nRefs,
                        nRefs == 1 ? "" : "s", oldId.c_str(), fileName(bas).c_str()));
    (void)kind;
    return nSubs;
}

// The project's forms' controls (id -> kind), for completion: every .afg in the project (the
// open designer's model as saved). In a form's code-behind, Me is that form.
std::map<std::string, std::string> App::basicControlMap()
{
    std::map<std::string, std::string> out;
    std::vector<std::string> forms;
    for (auto &f : project.files)
        if (extension(f) == "afg") forms.push_back(project.abs(f));
    if (cur && basic::isBasicFile(cur->path)) {
        std::string mine = cur->path.substr(0, cur->path.rfind('.')) + ".afg";
        if (fileExists(mine)) {
            forms.push_back(mine);
            out["Me"] = "window";
        }
    }
    for (auto &f : forms) {
        int64_t t = fileTime(f);
        auto &c = formCache[f];
        if (c.first != t) {
            std::string text;
            readFile(f, text);
            c.first = t;
            c.second = basic::formControls(text);
        }
        for (auto &kv : c.second) out.insert(kv);
    }
    return out;
}

// View Form / View Code (Alt-F7): from a code-behind module to its form in the designer, from
// the designer (or a form file) to the code.
void App::basicViewToggle()
{
    std::string afg;
    if (cur && !cur->path.empty() && extension(cur->path) == "afg") afg = cur->path;
    else if (cur && basic::isBasicFile(cur->path)) {
        std::string f = cur->path.substr(0, cur->path.rfind('.')) + ".afg";
        if (fileExists(f)) {
            if (designerPath() == f) designerToFront();
            else openDesigner(f);
            return;
        }
        setStatus(fileName(cur->path) + " isn't the code of a form");
        return;
    }
    if (afg.empty()) afg = designerPath();
    if (afg.empty()) {
        // the project's form, if it has one
        for (auto &f : project.files)
            if (extension(f) == "afg") { afg = project.abs(f); break; }
    }
    if (afg.empty()) {
        setStatus("No form: open one with Tools > GUI Designer");
        return;
    }
    if (basicOpenCode(afg, true)) bringMainWindowForward(*this);
}

} // namespace af
