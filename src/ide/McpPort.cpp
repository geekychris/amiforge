// --- mcp ---
// The ARexx commands for AI assistants (MCP): machine-readable state, file reading and
// editing by line or by exact text, builds, runs and the debugger, without requesters.
// The devbench's amiforge_* MCP tools send them through the amiga-bridge as
// "MCP <command>"; the envelope, escaping and line edits are in McpCmd.cpp (host-tested).
// Every command here returns at once: a build or a debugger step is started, and the
// assistant polls GETSTATE until it has finished.
#include "App.h"
#include "Commands.h"
#include "RexxCmd.h"
#include "McpCmd.h"
#include "ProjectIndex.h"
#include "TemplateEngine.h"
#include "Symbols.h"
#include "GitCore.h"

#include <string.h>
#include <strings.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>

#include "Scintilla.h"

namespace af {

std::string rexxPortName();
std::vector<git::FileStatus> mcpGitStatus(App *a, std::string &repoDir);   // GitPanel.cpp

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static const size_t kChunk = 3500;          // escaped characters per MCP result (the bridge line is 8 KB)
static const char *kRunOutput = "T:AmiForge-mcp-run.txt";

static const char *const kCommands[] = {
    "MCP", "MCPMORE", "MCPBUF", "GETSTATE", "DOCUMENTS", "PROJECTINFO", "PROJECTFILES", "PROJECTS", "TEMPLATES",
    "OPENPROJECT", "ACTIVATE", "NEWPROJECT", "READ", "EDITLINES", "SUBST", "SAVEALL", "REVERT", "STARTBUILD", "STOPBUILD", "ERRORS",
    "BUILDLOG", "RUNPROGRAM", "RUNOUTPUT", "STOPPROGRAM", "DEBUGSTART", "DEBUGSTOP", "CONTINUE", "STEP", "NEXT",
    "STEPOUT", "PAUSE", "RUNTO", "BREAK", "UNBREAK", "BREAKS", "LOCALS", "STACK", "FRAME", "EVAL", "REGS",
    "CRASH", "SYMBOLS", "REFERENCES", "GITSTATUS",
};

bool App::mcpHandles(const std::string &cmd)
{
    for (const char *c : kCommands)
        if (cmd == c) return true;
    return false;
}

static std::string text(SciView *ed)
{
    intptr_t n = S(ed, SCI_GETLENGTH);
    std::vector<char> buf(n + 1);
    struct Sci_TextRangeFull tr = { { 0, n }, buf.data() };
    S(ed, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return std::string(buf.data(), n);
}

static std::string yesNo(bool b) { return b ? "1" : "0"; }

// A file name from the assistant: relative names are in the project's drawer (or the
// current file's). Escapes are undone first.
static std::string resolvePath(App *a, const std::string &raw)
{
    std::string p = mcp::unescape(raw);
    if (p.empty()) return a->cur ? a->cur->path : "";
    if (p.find(':') == std::string::npos) {
        std::string base = !a->project.dir.empty() ? a->project.dir : a->cur && !a->cur->path.empty() ? dirName(a->cur->path) : "";
        if (!base.empty()) p = joinPath(base, p);
    }
    return absolutePath(p);
}

// The document for FILE (opened and made current if it isn't), or the current one.
static Document *documentFor(App *a, const rexx::Args &args, std::string &err)
{
    if (!args.has("FILE") || args.str("FILE").empty()) {
        if (!a->cur) err = "no file is open";
        return a->cur;
    }
    std::string p = resolvePath(a, args.str("FILE"));
    Document *d = a->findDocument(p);
    if (!d) {
        if (!fileExists(p)) { err = "there is no file " + p; return nullptr; }
        d = a->openFile(p, true);
        if (!d) { err = "couldn't open " + p; return nullptr; }
    } else if (d != a->cur) {
        a->activate(d);
    }
    return d;
}

static bool editable(Document *d, std::string &err)
{
    if (d->hex) { err = d->path + " is open in the hex editor"; return false; }
    if (d->readOnly) { err = d->title + " is read-only"; return false; }
    return true;
}

static void replaceRange(SciView *ed, size_t start, size_t end, const std::string &with)
{
    S(ed, SCI_SETTARGETRANGE, start, end);
    S(ed, SCI_REPLACETARGET, with.size(), (intptr_t)with.c_str());
}

// --- lists of things ---

static bool listDir(const std::string &path, std::vector<pidx::DirItem> &out)
{
    APTR old = IDOS->SetProcWindow((APTR)-1);
    BPTR lock = IDOS->Lock(path.c_str(), SHARED_LOCK);
    IDOS->SetProcWindow(old);
    if (!lock) return false;
    APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, TAG_END);
    if (ctx) {
        struct ExamineData *ed;
        while ((ed = IDOS->ExamineDir(ctx))) {
            if (!EXD_IS_FILE(ed) && !EXD_IS_DIRECTORY(ed)) continue;
            out.push_back({ (const char *)ed->Name, (bool)EXD_IS_DIRECTORY(ed) });
        }
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->UnLock(lock);
    return ctx != nullptr;
}

static bool readSmall(const std::string &path, std::string &out)
{
    if (!readFile(path, out)) return false;
    if (out.size() > 16384) out.resize(16384);
    return true;
}

static std::vector<tmpl::Def> templates()
{
    std::vector<tmpl::Def> out;
    std::vector<pidx::DirItem> items;
    if (!listDir("PROGDIR:Templates", items)) return out;
    for (auto &it : items) {
        if (!it.dir) continue;
        std::string dir = joinPath("PROGDIR:Templates", it.name), t, err;
        if (!readFile(joinPath(dir, "template.def"), t)) continue;
        tmpl::Def d;
        if (!tmpl::parseDef(t, d, err)) continue;
        d.dir = absolutePath(dir);
        out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const tmpl::Def &a, const tmpl::Def &b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return out;
}

// --- the debugger ---

void App::mcpDebugEvent(const dbg_event &ev)
{
    if (ev.type == DBG_EV_STOPPED) {
        mcpStopReason = mcp::stopReasonName(ev.reason);
        mcpStopText = ev.text;
        mcpStopFunction = ev.function ? ev.function : "";
        const char *src = ev.file && dbg ? dbg_resolve_source(dbg, ev.file) : nullptr;
        mcpStopFile = src ? absolutePath(src) : ev.file ? ev.file : "";
        mcpStopLine = ev.line;
    } else if (ev.type == DBG_EV_EXITED) {
        mcpExited = true;
        mcpExitCode = ev.exit_code;
        mcpStopText = ev.text[0] ? std::string(ev.text) : strprintf("Program exited with code %d", ev.exit_code);
        mcpStopReason = "exited";
    }
}

static std::string varRecord(const dbg_var *v)
{
    return mcp::record({ v->name ? v->name : "", v->type ? v->type : "", v->value ? v->value : "",
                         v->has_children ? "1" : "0" });
}

static bool runTaskAlive(const std::string &name)
{
    if (name.empty()) return false;
    IExec->Forbid();
    bool alive = IExec->FindTask(name.c_str()) != nullptr;
    IExec->Permit();
    return alive;
}

// --- the commands ---

std::string App::mcpCommand(const std::string &cmd, const rexx::Args &a, int &rc)
{
    rc = 0;
    auto fail = [&](int code, const std::string &why) { rc = code; rexxError = why; return std::string(); };

    if (cmd == "MCP") {
        // run the command without requesters, then wrap its result
        bool wasQuiet = mcpQuiet;
        mcpQuiet = true;
        mcpMessages.clear();
        int inner = 0;
        std::string result = rexxCommand(a.str("COMMAND"), inner);
        mcpQuiet = wasQuiet;
        if (inner != 0) {
            result = rexxError.empty() ? strprintf("the command failed (RC %d)", inner) : rexxError;
            std::string said = trim(mcpMessages);
            if (!said.empty() && result.find(said) == std::string::npos) result += "\n" + said;
        }
        rexxError.clear();
        rc = 0;
        return mcp::envelope(inner, result, kChunk, mcpRest);
    }
    if (cmd == "MCPMORE") return mcp::nextChunk(mcpRest, kChunk);
    if (cmd == "MCPBUF") {
        long slot = a.num("SLOT");
        if (slot < 1 || slot > 9) return fail(10, "SLOT must be 1 to 9");
        if (a.has("CLEAR")) mcpSlots[slot].clear();
        mcpSlots[slot] += mcp::unescape(a.str("TEXT"));
        return std::to_string((long)mcpSlots[slot].size());
    }

    if (cmd == "GETSTATE") {
        std::vector<std::string> l;
        int modified = 0;
        for (auto &d : docs) if (d->modified) modified++;
        l.push_back(mcp::kv("port", rexxPortName()));
        l.push_back(mcp::kv("version", "AmiForge 0.1"));
        l.push_back(mcp::kv("project", project.path));
        l.push_back(mcp::kv("projectname", project.path.empty() ? "" : project.name));
        l.push_back(mcp::kv("projectdir", project.dir));
        l.push_back(mcp::kv("projectsfolder", projectsFolder()));
        l.push_back(mcp::kv("file", cur ? cur->path : ""));
        if (cur && editor && !cur->hex) {
            intptr_t pos = S(editor, SCI_GETCURRENTPOS);
            l.push_back(mcp::kv("line", std::to_string((long)S(editor, SCI_LINEFROMPOSITION, pos) + 1)));
            l.push_back(mcp::kv("col", std::to_string((long)S(editor, SCI_GETCOLUMN, pos) + 1)));
        }
        l.push_back(mcp::kv("documents", std::to_string((long)docs.size())));
        l.push_back(mcp::kv("modified", std::to_string((long)modified)));
        bool built = false;
        for (auto &d : diagnostics) if (d.kind == MsgKind::Error || d.kind == MsgKind::Info) { built = true; break; }
        l.push_back(mcp::kv("build", builder.busy() ? "running" : "idle"));
        l.push_back(mcp::kv("buildresult", builder.busy() || !built ? "none" : builder.lastOk ? "ok" : "failed"));
        l.push_back(mcp::kv("errors", std::to_string(builder.errors)));
        l.push_back(mcp::kv("failures", std::to_string(builder.stepFailures)));   // failed commands
        l.push_back(mcp::kv("warnings", std::to_string(builder.warnings)));
        int st = dbg ? (int)dbg_get_state(dbg) : (int)DBG_NONE;
        l.push_back(mcp::kv("debug", mcp::debugStateName(st)));
        l.push_back(mcp::kv("sessions", std::to_string(mcpSessions)));
        if (st == DBG_STOPPED) {
            l.push_back(mcp::kv("stopreason", mcpStopReason));
            l.push_back(mcp::kv("stopfile", !execFile.empty() ? execFile : mcpStopFile));
            l.push_back(mcp::kv("stopline", std::to_string(execLine ? execLine : mcpStopLine)));
            l.push_back(mcp::kv("stopfunction", mcpStopFunction));
            l.push_back(mcp::kv("frame", std::to_string(selectedFrame)));
        }
        if (!mcpStopText.empty()) l.push_back(mcp::kv("stoptext", mcpStopText));
        if (mcpExited) l.push_back(mcp::kv("exitcode", std::to_string(mcpExitCode)));
        l.push_back(mcp::kv("program", runTaskAlive(mcpRunTask) ? "running" : "idle"));
        l.push_back(mcp::kv("status", statusLeft));
        std::string s;
        for (auto &x : l) s += x + "\n";
        return s;
    }
    if (cmd == "DOCUMENTS") {
        std::string s;
        for (auto &d : docs) {
            long n = 0;
            if (!d->hex) withDocument(d.get(), [&] { n = (long)S(mainView, SCI_GETLINECOUNT); });
            s += mcp::record({ d->path.empty() ? d->title : d->path, yesNo(d->modified), yesNo(d.get() == cur),
                               std::to_string(n) }) + "\n";
        }
        return s;
    }
    if (cmd == "PROJECTINFO") {
        if (project.path.empty()) return fail(10, "no project is open");
        std::vector<std::pair<std::string, std::string>> kvs = {
            {"path", project.path}, {"name", project.name}, {"dir", project.dir}, {"output", project.output},
            {"outputpath", project.outputPath()}, {"cc", project.cc}, {"cxx", project.cxx}, {"cflags", project.cflags},
            {"cxxflags", project.cxxflags}, {"asflags", project.asflags}, {"ldflags", project.ldflags},
            {"libs", project.libs}, {"objdir", project.objdir}, {"args", project.args}, {"console", project.console},
            {"runcmd", project.runcmd}, {"stopatmain", yesNo(project.stopAtMain)},
            {"files", std::to_string((long)project.files.size())},
            {"breakpoints", std::to_string((long)project.breakpoints.size())},
            {"built", yesNo(fileExists(project.outputPath()))},
        };
        std::string s;
        for (auto &p : kvs) s += mcp::kv(p.first, p.second) + "\n";
        return s;
    }
    if (cmd == "PROJECTFILES") {
        if (project.path.empty()) return fail(10, "no project is open");
        std::string s;
        for (auto &f : project.files) s += mcp::record({ f, project.abs(f) }) + "\n";
        return s;
    }
    if (cmd == "PROJECTS") {
        int depth = (int)std::max(1L, std::min(4L, a.num("DEPTH", 2)));
        std::string s;
        for (auto &p : pidx::scan(projectsFolder(), depth, listDir, readSmall))
            s += mcp::record({ p.name, p.afp, p.description }) + "\n";
        return s;
    }
    if (cmd == "TEMPLATES") {
        std::string s;
        for (auto &t : templates()) s += mcp::record({ fileName(t.dir), t.name, t.description }) + "\n";
        return s;
    }
    if (cmd == "OPENPROJECT") {
        std::string p = resolvePath(this, a.str("FILE"));
        if (!fileExists(p)) return fail(10, "there is no project " + p);
        if (extension(p) != "afp") return fail(10, p + " isn't a project (.afp)");
        openProject(p);
        if (!sameFile(project.path, p) || !mcpMessages.empty()) {   // a skipped requester: it was cancelled
            std::string why = trim(mcpMessages);
            bool unsaved = false;
            for (auto &d : docs) if (d->modified) unsaved = true;
            if (why.empty()) why = "the project didn't open";
            if (unsaved) why += " (there are unsaved changes: SAVEALL or REVERT first)";
            return fail(10, why);
        }
        return project.path;
    }
    if (cmd == "ACTIVATE") {
        std::string err;
        Document *d = documentFor(this, a, err);
        if (!d) return fail(10, err);
        if (a.has("LINE") && editor && !d->hex) {
            intptr_t l = std::min<intptr_t>(std::max(1L, a.num("LINE")) - 1, S(editor, SCI_GETLINECOUNT) - 1);
            S(editor, SCI_ENSUREVISIBLEENFORCEPOLICY, l);
            S(editor, SCI_GOTOLINE, l);
        }
        return d->path;
    }
    if (cmd == "NEWPROJECT") {
        std::string want = mcp::unescape(a.str("TEMPLATE")), name = mcp::unescape(a.str("NAME"));
        std::string why;
        if (!tmpl::validName(name, why)) return fail(10, why);
        const tmpl::Def *def = nullptr;
        std::vector<tmpl::Def> all = templates();
        for (auto &t : all)
            if (strcasecmp(fileName(t.dir).c_str(), want.c_str()) == 0 || strcasecmp(t.name.c_str(), want.c_str()) == 0) def = &t;
        if (!def) return fail(10, "there is no template " + want + " (see TEMPLATES)");
        std::string target = joinPath(mcp::unescape(a.str("DRAWER")), name);
        if (fileExists(joinPath(target, name + ".afp"))) return fail(10, target + " already has a project " + name);
        std::string afp, err;
        std::vector<std::string> toOpen;
        if (!createFromTemplate(def->dir, name, target, afp, err, &toOpen)) return fail(10, "the project couldn't be made: " + err);
        openProject(afp);
        for (auto &f : toOpen)
            if (fileExists(f)) openFile(f);
        return afp;
    }

    // --- files ---
    if (cmd == "READ") {
        std::string p, t;
        if (a.has("FILE") && !a.str("FILE").empty()) {
            p = resolvePath(this, a.str("FILE"));
            Document *d = findDocument(p);
            if (d && d->hex) return fail(10, p + " is open in the hex editor");
            if (d) t = documentText(d);
            else if (!readFile(p, t)) return fail(10, "can't read " + p);
        } else {
            if (!cur || cur->hex) return fail(10, "no file is open");
            p = cur->path.empty() ? cur->title : cur->path;
            t = documentText(cur);
        }
        long n = mcp::lineCount(t), from = std::max(1L, a.num("FROMLINE", 1)), to = a.num("TOLINE", n);
        if (to < 0 || to > n) to = n;
        return mcp::record({ std::to_string(n), std::to_string(from), std::to_string(std::max(to, from - 1)), p }) + "\n" +
               mcp::lines(t, from, to);
    }
    if (cmd == "EDITLINES" || cmd == "SUBST") {
        std::string err;
        Document *d = documentFor(this, a, err);
        if (!d) return fail(10, err);
        if (!editable(d, err)) return fail(10, err);
        std::string t = text(editor), out;
        if (cmd == "EDITLINES") {
            std::string repl;
            if (!mcp::resolve(a.str("TEXT"), mcpSlots, repl)) return fail(10, "that MCPBUF slot is empty");
            size_t start, end;
            std::string with;
            if (!mcp::replaceLines(t, a.num("FROMLINE"), a.num("TOLINE"), repl, out, start, end, with, err)) return fail(10, err);
            S(editor, SCI_BEGINUNDOACTION);
            replaceRange(editor, start, end, with);
            S(editor, SCI_ENDUNDOACTION);
            S(editor, SCI_GOTOPOS, start);
            S(editor, SCI_SCROLLCARET);
            return std::to_string((long)S(editor, SCI_GETLINECOUNT));
        }
        std::string oldText, newText;
        if (!mcp::resolve(a.str("OLD"), mcpSlots, oldText) || !mcp::resolve(a.str("NEW"), mcpSlots, newText))
            return fail(10, "that MCPBUF slot is empty");
        int line = 0;
        int n = mcp::substitute(t, oldText, newText, a.has("ALL"), out, line, err);
        if (!n) return fail(5, err);
        std::vector<size_t> at = mcp::occurrences(t, oldText);
        S(editor, SCI_BEGINUNDOACTION);
        for (size_t i = at.size(); i-- > 0;) replaceRange(editor, at[i], at[i] + oldText.size(), newText);
        S(editor, SCI_ENDUNDOACTION);
        S(editor, SCI_GOTOPOS, at[0]);
        S(editor, SCI_SCROLLCARET);
        return mcp::record({ std::to_string(n), std::to_string(line) });
    }
    if (cmd == "SAVEALL") {
        for (auto &d : docs)
            if (d->modified && d->path.empty()) return fail(10, d->title + " has never been saved: SAVEAS it first");
        if (!saveAll()) return fail(10, "saving failed");
        return "";
    }
    if (cmd == "REVERT") {
        std::string err;
        Document *d = documentFor(this, a, err);
        if (!d) return fail(10, err);
        if (!editable(d, err)) return fail(10, err);
        if (d->path.empty()) return fail(10, "the file has never been saved");
        std::string t;
        if (!readFile(d->path, t)) return fail(10, "can't read " + d->path);
        S(editor, SCI_SETTEXT, 0, (intptr_t)t.c_str());
        S(editor, SCI_SETSAVEPOINT);
        d->modified = false;
        rebuildTabs();
        updateTitle();
        return "";
    }

    // --- building ---
    if (cmd == "STARTBUILD") {
        if (builder.busy()) return fail(10, "a build is already running");
        if (debugging()) return fail(10, "stop debugging before building (DEBUGSTOP)");
        if (project.path.empty()) return fail(10, "no project is open");
        bool clean = a.has("CLEAN");
        if (!clean && !sdkInstalled()) return fail(10, "the AmigaOS 4.1 SDK isn't installed (no SDK: assign)");
        for (auto &d : docs)
            if (d->modified && d->path.empty()) return fail(10, d->title + " has never been saved: SAVEAS it first");
        pendingAfterBuild = 0;
        build(clean ? "clean" : a.has("REBUILD") ? "rebuild" : "build");
        if (!builder.busy()) return fail(10, mcpMessages.empty() ? "the build didn't start" : trim(mcpMessages));
        return "";
    }
    if (cmd == "STOPBUILD") {
        if (!builder.busy()) return fail(5, "no build is running");
        builder.stop();
        return "";
    }
    if (cmd == "ERRORS") {
        bool all = a.has("ALL");
        std::string s;
        for (size_t i = 0; i < diagnostics.size(); i++) {
            const BuildLine &d = diagnostics[i];
            const char *kind = d.kind == MsgKind::Error ? "error" : d.kind == MsgKind::Warning ? "warning"
                             : d.kind == MsgKind::Note ? "note" : d.kind == MsgKind::Info ? "info"
                             : d.kind == MsgKind::Command ? "command" : "output";
            if (!all && d.kind != MsgKind::Error && d.kind != MsgKind::Warning) continue;
            if (d.kind == MsgKind::Note && d.text.find("Explain:") != std::string::npos) continue;
            if (d.file.empty() && (d.text.compare(0, 13, "Build failed:") == 0 || d.text.compare(0, 16, "Build succeeded ") == 0))
                continue;   // the summary: GETSTATE has the counts
            std::string ex = i < explanations.size() ? explanations[i] : "";
            s += mcp::record({ kind, d.file, std::to_string(d.line), std::to_string(d.col), d.text, ex }) + "\n";
        }
        return s;
    }
    if (cmd == "BUILDLOG") {
        std::string s;
        for (auto &d : diagnostics) s += d.text + "\n";
        return s;
    }

    // --- running ---
    if (cmd == "RUNPROGRAM") {
        if (project.path.empty()) return fail(10, "no project is open");
        if (builder.busy()) return fail(10, "a build is running");
        // --- lang --- a Python project has no program file: Run starts its main script
        if (project.isPython()) {
            if (a.has("CAPTURE")) return fail(10, "CAPTURE isn't available for Python projects; RUNPROGRAM runs the script in a console");
            runPython();
            return mcpMessages.empty() ? "" : fail(10, trim(mcpMessages));
        }
        if (project.runcmd.empty() && !fileExists(project.outputPath()))
            return fail(10, "the program isn't built: " + project.outputPath());
        if (!a.has("CAPTURE")) {
            runProgram();
            return mcpMessages.empty() ? "" : fail(10, trim(mcpMessages));
        }
        // like runProgram, with the output going to a file
        if (runTaskAlive(mcpRunTask)) return fail(10, "the program is still running (STOPPROGRAM)");
        BPTR out = IDOS->Open(kRunOutput, MODE_NEWFILE);
        BPTR in = IDOS->Open("NIL:", MODE_OLDFILE);
        if (!out || !in) {
            if (out) IDOS->Close(out);
            if (in) IDOS->Close(in);
            return fail(10, std::string("can't open ") + kRunOutput);
        }
        std::string cmdline = project.runcmd.empty()
            ? "\"" + project.outputPath() + "\"" + (project.args.empty() ? "" : " " + project.args) : project.runcmd;
        mcpRunTask = "AmiForge MCP run " + project.name;
        BPTR dir = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
        BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
        int32 r = IDOS->SystemTags(cmdline.c_str(), SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                                   NP_Name, (uint32)mcpRunTask.c_str(), TAG_END);
        if (dir) {
            IDOS->SetCurrentDir(old);
            IDOS->UnLock(dir);
        }
        if (r == -1) {
            IDOS->Close(in);
            IDOS->Close(out);
            mcpRunTask.clear();
            return fail(10, "couldn't start " + cmdline);
        }
        setStatus("Started " + project.output + " (output to " + kRunOutput + ")");
        return kRunOutput;
    }
    if (cmd == "RUNOUTPUT") {
        bool alive = runTaskAlive(mcpRunTask);
        std::string t;
        // while the program runs, the file is open for writing: copy what's there
        BPTR fh = IDOS->Open(kRunOutput, MODE_OLDFILE);
        if (fh) {
            char buf[4096];
            int32 n;
            while ((n = IDOS->Read(fh, buf, sizeof buf)) > 0 && t.size() < 256 * 1024) t.append(buf, n);
            IDOS->Close(fh);
        }
        return mcp::kv("running", yesNo(alive)) + "\n" + t;
    }
    if (cmd == "STOPPROGRAM") {
        IExec->Forbid();
        struct Task *t = mcpRunTask.empty() ? nullptr : IExec->FindTask(mcpRunTask.c_str());
        if (t) IExec->Signal(t, SIGBREAKF_CTRL_C);
        IExec->Permit();
        if (!t && programRunning()) {   // started with Build > Run: Stop Program sends it Ctrl-C
            stopProgram();
            return "";
        }
        if (!t) return fail(5, "the program isn't running");
        return "";
    }

    // --- debugging ---
    if (cmd == "DEBUGSTART") {
        if (debugging()) return fail(10, "already debugging (DEBUGSTOP first)");
        if (builder.busy()) return fail(10, "a build is running");
        if (project.path.empty()) return fail(10, "no project is open");
        if (!fileExists(project.outputPath())) return fail(10, "the program isn't built: " + project.outputPath());
        bool keep = project.stopAtMain;
        if (a.has("MAIN")) project.stopAtMain = true;
        if (a.has("NOMAIN")) project.stopAtMain = false;
        mcpExited = false;
        mcpExitCode = 0;
        mcpStopText.clear();
        mcpStopReason.clear();
        mcpSessions++;
        profileNext = false;
        debugStart();
        project.stopAtMain = keep;
        if (!debugging()) return fail(10, mcpMessages.empty() ? "the debugger didn't start" : trim(mcpMessages));
        return std::to_string(mcpSessions);
    }
    if (cmd == "DEBUGSTOP") {
        if (!debugging()) return fail(5, "not debugging");
        debugStop();
        return "";
    }
    if (cmd == "PAUSE") {
        if (!debugging() || stopped()) return fail(10, "the program isn't running");
        if (!pausePending()) debugPause();   // pending already (in an OS call): don't cancel it
        return "";
    }
    if (cmd == "CONTINUE" && pausePending()) { debugContinue(); return ""; }   // cancels the pending pause
    if (cmd == "CONTINUE" || cmd == "STEP" || cmd == "NEXT" || cmd == "STEPOUT" || cmd == "RUNTO") {
        if (!stopped()) return fail(10, debugging() ? "the program is running, not stopped (PAUSE first)" : "not debugging (DEBUGSTART first)");
        mcpStopText.clear();
        if (cmd == "CONTINUE") debugContinue();
        else if (cmd == "STEP") debugStep(1);
        else if (cmd == "NEXT") debugStep(0);
        else if (cmd == "STEPOUT") debugStep(2);
        else {
            std::string p = resolvePath(this, a.str("FILE"));
            clearExecutionMarkers();
            if (!dbg_run_to(dbg, p.c_str(), (int)a.num("LINE"))) return fail(10, std::string("run to failed: ") + dbg_last_error(dbg));
            updateToolbar();
        }
        return "";
    }
    if (cmd == "BREAK" || cmd == "UNBREAK") {
        if (project.path.empty()) return fail(10, "no project is open (breakpoints belong to the project)");
        harvestBreakpoints(cur);
        long setAt = -1;   // BREAK: index of the breakpoint
        if (cmd == "UNBREAK" && a.has("ALL")) {
            project.breakpoints.clear();
        } else {
            std::string p = resolvePath(this, a.str("FILE"));
            if (p.empty()) return fail(10, "which file?");
            int line = (int)a.num("LINE");
            if (line < 1) return fail(10, "LINE must be 1 or more");
            size_t i = 0;
            for (; i < project.breakpoints.size(); i++)
                if (project.breakpoints[i].line == line && sameFile(project.abs(project.breakpoints[i].file), p)) break;
            if (cmd == "UNBREAK") {
                if (i == project.breakpoints.size()) return fail(5, strprintf("there is no breakpoint at %s:%d", fileName(p).c_str(), line));
                project.breakpoints.erase(project.breakpoints.begin() + i);
            } else {
                std::string cond;
                if (!mcp::resolve(a.str("COND"), mcpSlots, cond)) return fail(10, "that MCPBUF slot is empty");
                if (i == project.breakpoints.size()) project.breakpoints.push_back({ project.rel(p), line, true, cond });
                else { project.breakpoints[i].cond = cond; project.breakpoints[i].enabled = true; }
                setAt = (long)i;
            }
        }
        syncDebuggerBreakpoints();
        refreshMarkers();
        refreshBreakpointList();
        // BREAK: the line it really went on (the first line with code), once the program is loaded
        if (setAt >= 0 && setAt < (long)project.breakpoints.size()) return std::to_string(project.breakpoints[setAt].line);
        return "";
    }
    if (cmd == "BREAKS") {
        dbg_breakpoint list[256];
        int n = dbg && dbg_get_state(dbg) != DBG_NONE ? dbg_bp_list(dbg, list, 256) : 0;
        std::string s;
        for (size_t i = 0; i < project.breakpoints.size(); i++) {
            auto &b = project.breakpoints[i];
            int id = i < dbgBpIds.size() ? dbgBpIds[i] : -1;
            std::string hits, addr;
            for (int k = 0; k < n; k++)
                if (list[k].id == id) { hits = std::to_string(list[k].hits); addr = strprintf("0x%08lx", (unsigned long)list[k].addr); }
            s += mcp::record({ project.abs(b.file), std::to_string(b.line), yesNo(b.enabled), b.cond, hits, addr }) + "\n";
        }
        return s;
    }
    if (cmd == "LOCALS" || cmd == "STACK" || cmd == "FRAME" || cmd == "EVAL" || cmd == "REGS") {
        if (!stopped()) return fail(10, debugging() ? "the program is running (PAUSE, or wait for a breakpoint)" : "not debugging");
        std::string s;
        if (cmd == "LOCALS") {
            dbg_var **vars = nullptr;
            int n = dbg_locals(dbg, &vars);
            for (int i = 0; i < n; i++) s += varRecord(vars[i]) + "\n";
            if (vars) dbg_var_free(dbg, vars, n);
        } else if (cmd == "STACK") {
            dbg_frame fr[64];
            int n = dbg_stack(dbg, fr, 64);
            for (int i = 0; i < n; i++)
                s += mcp::record({ std::to_string(fr[i].level), fr[i].function ? fr[i].function : "??",
                                   fr[i].file ? fr[i].file : "", std::to_string(fr[i].line),
                                   strprintf("0x%08lx", (unsigned long)fr[i].pc), fr[i].module ? fr[i].module : "" }) + "\n";
        } else if (cmd == "FRAME") {
            dbg_frame fr[64];
            int n = dbg_stack(dbg, fr, 64);
            long level = a.num("LEVEL");
            if (level < 0 || level >= n) return fail(10, strprintf("there are %d frames", n));
            selectFrame((int)level);
            s = std::to_string(level);
        } else if (cmd == "EVAL") {
            std::string expr = mcp::unescape(a.str("EXPR"));
            dbg_var *v = dbg_eval(dbg, expr.c_str());
            if (!v) return fail(10, "can't evaluate " + expr);
            s = varRecord(v) + "\n";
            bool error = v->value && strncmp(v->value, "<error", 6) == 0;
            if (a.has("CHILDREN") && v->has_children) {
                dbg_var **kids = nullptr;
                int n = dbg_var_children(dbg, v, &kids);
                for (int i = 0; i < n; i++) s += varRecord(kids[i]) + "\n";
                if (kids) dbg_var_free(dbg, kids, n);
            }
            std::string value = v->value ? v->value : "";
            dbg_var_release(dbg, v);
            if (error) return fail(10, value);
        } else {
            dbg_regs r;
            if (!dbg_get_regs(dbg, &r)) return fail(10, "can't read the registers");
            for (int i = 0; i < 32; i++) s += mcp::record({ strprintf("r%d", i), strprintf("0x%08lx", (unsigned long)r.gpr[i]) }) + "\n";
            const std::pair<const char *, uint32_t> sp[] = { {"pc", r.pc}, {"lr", r.lr}, {"ctr", r.ctr}, {"msr", r.msr},
                                                             {"cr", r.cr}, {"xer", r.xer}, {"dar", r.dar}, {"dsisr", r.dsisr} };
            for (auto &p : sp) s += mcp::record({ p.first, strprintf("0x%08lx", (unsigned long)p.second) }) + "\n";
        }
        return s;
    }
    if (cmd == "CRASH") {
        if (lastCrashExplanation.empty()) return fail(5, "no crash has been explained");
        return lastCrashExplanation;
    }

    // --- code ---
    if (cmd == "SYMBOLS") {
        std::string p, t;
        if (a.has("FILE")) {
            p = resolvePath(this, a.str("FILE"));
            Document *d = findDocument(p);
            if (d && !d->hex) t = documentText(d);
            else if (!readFile(p, t)) return fail(10, "can't read " + p);
        } else {
            if (!cur || cur->hex) return fail(10, "no file is open");
            p = cur->path;
            t = documentText(cur);
        }
        std::string s;
        for (auto &sym : outlineSymbols(t, languageFor(p.empty() ? std::string("untitled.c") : p, t.substr(0, t.find('\n')))))   // --- lang ---
            s += mcp::record({ symKindName(sym.kind), sym.name, std::to_string(sym.line), sym.detail }) + "\n";
        return s;
    }
    if (cmd == "REFERENCES") {
        std::string name = mcp::unescape(a.str("NAME"));
        if (!isIdentifierName(name)) return fail(10, name + " isn't an identifier");
        std::string s;
        int count = 0;
        for (auto &f : sourceFilesForSearch()) {
            Document *d = findDocument(f);
            std::string t;
            if (d && !d->hex) t = documentText(d);
            else if (!readFile(f, t)) continue;
            for (size_t off : findIdentifier(t, name, languageFor(f))) {   // --- lang ---
                int line, col;
                lineColumn(t, off, line, col);
                s += mcp::record({ f, std::to_string(line), std::to_string(col), trim(mcp::lines(t, line, line)) }) + "\n";
                if (++count >= 500) return s;
            }
        }
        return s;
    }
    if (cmd == "GITSTATUS") {
        std::string repo;
        std::vector<git::FileStatus> st = mcpGitStatus(this, repo);
        if (repo.empty()) return fail(5, "the project isn't in a git repository");
        std::string head, branch;
        if (readFile(joinPath(joinPath(repo, ".git"), "HEAD"), head)) {
            head = trim(head);
            branch = head.compare(0, 16, "ref: refs/heads/") == 0 ? head.substr(16) : head.substr(0, 12);
        }
        std::string s = mcp::record({ repo, branch }) + "\n";
        for (auto &f : st) s += mcp::record({ std::string(1, f.staged) + f.work, f.path }) + "\n";
        return s;
    }
    rc = 20;
    rexxError = "not an MCP command: " + cmd;
    return "";
}

} // namespace af
// --- end mcp ---
