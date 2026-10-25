// Builds a project with the native toolchain in a separate process, so the GUI stays live.
//
// The IDE works out which commands are needed (sources newer than their objects, or newer
// headers); the builder process runs them one by one with SystemTags() in the project's
// directory and passes their output back as lines. The builder only uses exec/dos and the
// job's read-only data: output lines are AllocVec'd nodes on a list guarded by a mutex.
#include "App.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/path.h>

namespace af {

struct OutNode {
    struct MinNode node;
    int kind;
    char text[1];
};

struct Builder::Job {
    struct Cmd { std::string label, line; };
    std::vector<Cmd> cmds;
    std::vector<std::string> mkdirs;
    std::string dir;
    APTR mutex = nullptr;
    struct MinList out;
    struct Task *owner = nullptr;
    uint32 sig = 0;
    struct Process *proc = nullptr;
    volatile bool finished = false;
    volatile bool ok = false;
};

static Builder::Job *gJob;   // handed to the builder process at start-up

static void post(Builder::Job *j, MsgKind kind, const char *text, size_t len)
{
    OutNode *n = (OutNode *)IExec->AllocVecTags(sizeof(OutNode) + len, AVT_Type, MEMF_SHARED, TAG_END);
    if (!n) return;
    n->kind = (int)kind;
    memcpy(n->text, text, len);
    n->text[len] = 0;
    IExec->MutexObtain(j->mutex);
    IExec->AddTail((struct List *)&j->out, (struct Node *)n);
    IExec->MutexRelease(j->mutex);
    IExec->Signal(j->owner, j->sig);
}

static void postStr(Builder::Job *j, MsgKind kind, const char *text) { post(j, kind, text, strlen(text)); }

// The SDK's tool drawers on this (CLI) process's command path. gcc runs as, ld and cc1 by
// name, and sdk-startup only adds SDK:gcc/bin to shells started after it ran; AmiForge may have
// been started from one that predates it.
static void addSdkToPath()
{
    struct CommandLineInterface *cli = IDOS->Cli();
    if (!cli) return;
    for (const char *dir : { "SDK:gcc/bin", "SDK:C" }) {
        BPTR lock = IDOS->Lock(dir, SHARED_LOCK);
        if (!lock) continue;
        bool have = false;
        for (struct PathNode *pn = (struct PathNode *)BADDR(cli->cli_PathList); pn && !have;
             pn = (struct PathNode *)BADDR(pn->pn_Next))
            have = pn->pn_Lock && IDOS->SameLock(pn->pn_Lock, lock) == LOCK_SAME;
        if (!have) {
            struct PathNode *old = (struct PathNode *)BADDR(cli->cli_PathList);
            struct PathNode *first = old ? IDOS->AddCmdPathNode(old, lock, ADDCMDPATHNODE_TAIL)
                                         : IDOS->AllocateCmdPathList(IDOS->DupLock(lock));
            if (first) cli->cli_PathList = MKBADDR(first);
        }
        IDOS->UnLock(lock);
    }
}

static int32 builderMain(STRPTR, int32, struct ExecBase *)
{
    Builder::Job *j = gJob;
    addSdkToPath();
    BPTR dirLock = IDOS->Lock(j->dir.c_str(), SHARED_LOCK);
    BPTR oldDir = dirLock ? IDOS->SetCurrentDir(dirLock) : 0;
    bool ok = dirLock != 0;
    if (!dirLock) postStr(j, MsgKind::Error, ("Can't find the project directory " + j->dir).c_str());

    for (auto &d : j->mkdirs) {
        BPTR l = IDOS->Lock(d.c_str(), SHARED_LOCK);
        if (l) IDOS->UnLock(l);
        else if ((l = IDOS->CreateDir(d.c_str()))) IDOS->UnLock(l);
    }

    char tmp[64];
    snprintf(tmp, sizeof tmp, "T:amiforge-build.%lx", (unsigned long)(uint32)j);
    for (size_t i = 0; ok && i < j->cmds.size(); i++) {
        if (IExec->SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            postStr(j, MsgKind::Error, "Build stopped.");
            ok = false;
            break;
        }
        if (!j->cmds[i].label.empty()) postStr(j, MsgKind::Info, j->cmds[i].label.c_str());
        // show the command, but never a password in a URL ("https://user:token@host")
        std::string shown = j->cmds[i].line;
        size_t sch = shown.find("://"), at = sch == std::string::npos ? sch : shown.find('@', sch);
        if (at != std::string::npos && shown.find(' ', sch) > at) shown = shown.substr(0, sch + 3) + "***" + shown.substr(at);
        postStr(j, MsgKind::Command, shown.c_str());
        BPTR out = IDOS->Open(tmp, MODE_NEWFILE);
        BPTR in = IDOS->Open("NIL:", MODE_OLDFILE);
        int32 rc = IDOS->SystemTags(j->cmds[i].line.c_str(),
                                    SYS_Input, in, SYS_Output, out, SYS_Error, out,
                                    SYS_Asynch, FALSE, SYS_Name, (uint32)"AmiForge build",
                                    // cc1plus is happier with a big stack than the shell default
                                    SYS_StackSize, 4 * 1024 * 1024,
                                    TAG_END);
        if (in) IDOS->Close(in);
        if (out) IDOS->Close(out);
        // pass the output on, line by line
        BPTR fh = IDOS->Open(tmp, MODE_OLDFILE);
        if (fh) {
            char line[1024];
            while (IDOS->FGets(fh, line, sizeof line)) {
                size_t n = strlen(line);
                while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
                post(j, MsgKind::Output, line, n);
            }
            IDOS->Close(fh);
        }
        if (rc != 0) {
            char msg[96];
            snprintf(msg, sizeof msg, rc < 0 ? "Couldn't run the command (is the SDK installed?)" : "Failed (return code %ld)", (long)rc);
            postStr(j, MsgKind::Error, msg);
            ok = false;
        }
    }
    IDOS->Delete(tmp);
    if (dirLock) {
        IDOS->SetCurrentDir(oldDir);
        IDOS->UnLock(dirLock);
    }
    j->ok = ok;
    IExec->Forbid();     // the IDE may free the job as soon as it sees `finished`
    j->finished = true;
    postStr(j, MsgKind::Done, ok ? "ok" : "failed");
    IExec->Permit();
    return 0;
}

Builder::Builder() {}

Builder::~Builder()
{
    stop();
    while (running) {
        BuildLine l;
        while (poll(l)) {}
        if (running) IDOS->Delay(5);
    }
    if (sigBit >= 0) IExec->FreeSignal(sigBit);
}

uint32_t Builder::signalMask() const { return sigBit >= 0 ? 1UL << sigBit : 0; }

void Builder::stop()
{
    if (running && job && job->proc && !job->finished)
        IExec->Signal((struct Task *)job->proc, SIGBREAKF_CTRL_C);
}

// Quote an argument for the AmigaDOS shell if it needs it.
static std::string q(const std::string &s)
{
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    return "\"" + s + "\"";
}

// True if NAME: is an assign or volume - checked without letting AmigaDOS put up an
// "insert volume" requester.
static bool deviceExists(const char *name)
{
    struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
    bool found = IDOS->FindDosEntry(dl, name, LDF_ALL) != nullptr;
    IDOS->UnLockDosList(LDF_ALL | LDF_READ);
    return found;
}

// "gcc" -> "SDK:gcc/bin/gcc" when the program isn't on the path but the SDK is installed,
// so building works from a Workbench-started IDE too.
static std::string tool(const std::string &cmd)
{
    if (cmd.find_first_of(":/ ") != std::string::npos || !deviceExists("SDK")) return cmd;
    std::string full = "SDK:gcc/bin/" + cmd;
    return fileExists(full) ? full : cmd;
}

static std::string objectFor(const Project &p, const std::string &src)
{
    std::string base = src;
    for (auto &c : base) if (c == '/' || c == ':') c = '_';
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return joinPath(p.objdir, base + ".o");
}

// --- hotpatch: the command that compiles one source the way a build would
std::string Builder::compileCommand(const Project &p, const std::string &src, const std::string &obj)
{
    std::string cmd;
    if (isAsmFile(src)) cmd = tool(p.cc) + " " + p.asflags;
    else if (isCppFile(src)) cmd = tool(p.cxx) + " " + p.cxxflags;
    else cmd = tool(p.cc) + " " + p.cflags;
    return cmd + " -c " + q(src) + " -o " + q(obj);
}

std::string Builder::objectPath(const Project &p, const std::string &src) { return objectFor(p, src); }
// --- end hotpatch

bool Builder::start(const Project &p, const std::string &what)
{
    if (running) return false;
    if (sigBit < 0 && (sigBit = IExec->AllocSignal(-1)) < 0) return false;
    errors = warnings = stepFailures = 0;
    lastOk = false;

    Job *j = new Job;
    j->dir = p.dir;
    j->owner = IExec->FindTask(nullptr);
    j->sig = 1UL << sigBit;
    j->mutex = IExec->AllocSysObjectTags(ASOT_MUTEX, TAG_END);
    IExec->NewList((struct List *)&j->out);

    bool rebuild = what == "rebuild";
    bool clean = what == "clean" || rebuild;
    std::string only;
    if (what.compare(0, 8, "compile:") == 0) only = what.substr(8);

    std::vector<std::string> objs;
    if (clean) {
        std::string list;
        for (auto &f : p.files)
            if (isSourceFile(f)) list += " " + q(objectFor(p, f));
        j->cmds.push_back({"Cleaning", "Delete" + list + " " + q(p.output) + " QUIET FORCE"});
    }
    if (what != "clean") {
        j->mkdirs.push_back(p.abs(p.objdir));
        // newest header in the project: a changed header recompiles everything
        int64_t newestHeader = -1;
        for (auto &f : p.files) {
            std::string e = extension(f);
            if (e == "h" || e == "hpp" || e == "hh" || e == "i" || e == "inc")
                newestHeader = std::max(newestHeader, fileTime(p.abs(f)));
        }
        bool anyCompiled = false;
        int64_t newestObj = -1;
        for (auto &f : p.files) {
            if (!isSourceFile(f)) continue;
            if (!only.empty() && !sameFile(p.abs(f), only)) continue;
            std::string obj = objectFor(p, f);
            objs.push_back(obj);
            int64_t ot = rebuild ? -1 : fileTime(p.abs(obj));
            int64_t st = fileTime(p.abs(f));
            bool need = rebuild || ot < 0 || st > ot || newestHeader > ot || !only.empty();
            if (need) {
                std::string cmd;
                if (isAsmFile(f)) cmd = tool(p.cc) + " " + p.asflags;
                else if (isCppFile(f)) cmd = tool(p.cxx) + " " + p.cxxflags;
                else cmd = tool(p.cc) + " " + p.cflags;
                cmd += " -c " + q(f) + " -o " + q(obj);
                j->cmds.push_back({"Compiling " + f, cmd});
                anyCompiled = true;
            } else {
                newestObj = std::max(newestObj, ot);
            }
        }
        if (only.empty()) {
            if (objs.empty()) {
                delete j;
                return false;
            }
            int64_t outTime = rebuild ? -1 : fileTime(p.outputPath());
            if (anyCompiled || outTime < 0 || newestObj > outTime) {
                bool cpp = p.usesCpp();
                std::string cmd = tool(cpp ? p.cxx : p.cc) + " " + p.ldflags;
                if (cpp && p.ldflags.find("-athread") == std::string::npos) cmd += " -athread=native";
                cmd += " -o " + q(p.output);
                for (auto &o : objs) cmd += " " + q(o);
                cmd += " " + p.libs;
                j->cmds.push_back({"Linking " + p.output, cmd});
            }
        }
    }
    if (j->cmds.empty()) {
        // nothing to do: report success straight away
        j->finished = true;
        j->ok = true;
        post(j, MsgKind::Info, "Everything is up to date.", 25);
        post(j, MsgKind::Done, "ok", 2);
        job = j;
        running = true;
        return true;
    }

    gJob = j;
    j->proc = IDOS->CreateNewProcTags(NP_Entry, builderMain, NP_Name, "AmiForge builder",
                                      NP_Child, TRUE, NP_StackSize, 64 * 1024,
                                      NP_Cli, TRUE, NP_CopyVars, TRUE, TAG_END);
    if (!j->proc) {
        IExec->FreeSysObject(ASOT_MUTEX, j->mutex);
        delete j;
        return false;
    }
    job = j;
    running = true;
    return true;
}

bool Builder::startCommands(const std::string &dir, const std::vector<std::pair<std::string, std::string>> &cmds)
{
    if (running || cmds.empty()) return false;
    if (sigBit < 0 && (sigBit = IExec->AllocSignal(-1)) < 0) return false;
    errors = warnings = 0;
    lastOk = false;
    Job *j = new Job;
    j->dir = dir;
    j->owner = IExec->FindTask(nullptr);
    j->sig = 1UL << sigBit;
    j->mutex = IExec->AllocSysObjectTags(ASOT_MUTEX, TAG_END);
    IExec->NewList((struct List *)&j->out);
    for (auto &c : cmds) j->cmds.push_back({c.first, c.second});
    gJob = j;
    j->proc = IDOS->CreateNewProcTags(NP_Entry, builderMain, NP_Name, "AmiForge commands",
                                      NP_Child, TRUE, NP_StackSize, 64 * 1024,
                                      NP_Cli, TRUE, NP_CopyVars, TRUE, TAG_END);
    if (!j->proc) {
        IExec->FreeSysObject(ASOT_MUTEX, j->mutex);
        delete j;
        return false;
    }
    job = j;
    running = true;
    return true;
}

bool Builder::poll(BuildLine &out)
{
    if (!job) return false;
    IExec->MutexObtain(job->mutex);
    OutNode *n = (OutNode *)IExec->RemHead((struct List *)&job->out);
    IExec->MutexRelease(job->mutex);
    if (!n) return false;
    out = BuildLine();
    out.kind = (MsgKind)n->kind;
    out.text = n->text;
    IExec->FreeVec(n);
    if (out.kind == MsgKind::Output) {
        if (parseDiagnostic(out.text, job->dir, out)) {
            if (out.kind == MsgKind::Error) errors++;
            else if (out.kind == MsgKind::Warning) warnings++;
        }
    } else if (out.kind == MsgKind::Error) {
        stepFailures++;     // "Failed (return code n)": counted only if no diagnostic explains it
    } else if (out.kind == MsgKind::Done) {
        if (errors == 0) errors = stepFailures;
        lastOk = job->ok && errors == 0;
        // the process has finished (it posted Done inside Forbid after its last access)
        IExec->FreeSysObject(ASOT_MUTEX, job->mutex);
        delete job;
        job = nullptr;
        running = false;
    }
    return true;
}

// "file:line:col: error: msg", "file:line: warning: msg", "file.s:12: Error: msg",
// "file.c:(.text+0x1c): undefined reference to `foo'"
bool parseDiagnostic(const std::string &text, const std::string &cwd, BuildLine &out)
{
    const char *s = text.c_str();
    // the file name runs to the first ':' that is followed by a digit or '(' - but an Amiga
    // device name ("DH1:src/x.c") also has a ':', so skip one that starts the path
    size_t start = 0;
    const char *colon = strchr(s, ':');
    if (!colon) return false;
    if (colon[1] != 0 && !isdigit((unsigned char)colon[1]) && colon[1] != '(' && colon[1] != ' ') {
        start = colon - s + 1;
        colon = strchr(colon + 1, ':');
        if (!colon) return false;
    }
    (void)start;
    std::string file(s, colon - s);
    if (file.empty() || file.find(' ') != std::string::npos) return false;
    const char *p = colon + 1;
    int line = 0, col = 0;
    if (isdigit((unsigned char)*p)) {
        line = strtol(p, (char **)&p, 10);
        if (*p == ':' && isdigit((unsigned char)p[1])) col = strtol(p + 1, (char **)&p, 10);
        if (*p != ':') return false;
        p++;
    } else if (*p == '(') {
        const char *e = strchr(p, ')');
        if (!e || e[1] != ':') return false;
        p = e + 2;
    } else {
        return false;
    }
    while (*p == ' ') p++;
    std::string rest = p;
    std::string lower = rest;
    for (auto &c : lower) c = tolower((unsigned char)c);
    MsgKind kind;
    if (lower.compare(0, 6, "error:") == 0 || lower.compare(0, 12, "fatal error:") == 0) kind = MsgKind::Error;
    else if (lower.compare(0, 8, "warning:") == 0) kind = MsgKind::Warning;
    else if (lower.compare(0, 5, "note:") == 0) kind = MsgKind::Note;
    else if (lower.find("undefined reference") != std::string::npos ||
             lower.find("multiple definition") != std::string::npos) kind = MsgKind::Error;
    else if (line > 0) kind = MsgKind::Note;   // "In function 'main':" style context
    else return false;
    out.kind = kind;
    out.file = joinPath(cwd, file);
    out.line = line;
    out.col = col;
    return true;
}

} // namespace af
