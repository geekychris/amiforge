// --- basic ---
// Boing BASIC projects: Build runs `boing check` and then `boing build` (a native program, with
// #line information so the debugger shows BASIC lines); Run (Ctrl-F5) checks and then starts
// `boing run` - the interpreter, which starts at once - in a console window; Stop Program sends
// it Ctrl-C; a runtime error goes into the build log and the editor jumps to its line. Package
// Release (ReleaseUI.cpp) builds with `boing build` without --debug (optimised).
//
// The boing command: the project's setting (Project > Settings), else ENV:AmiForge/Boing, else
// the first of basicCandidates that exists.
#include "App.h"
#include "BasicModel.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/var.h>

namespace af {

static const char *const basicCandidates[] = {
    "C:boing", "SDK:C/boing", "BoingBASIC:boing", "Boing:boing", "SYS:Utilities/BoingBASIC/boing", nullptr,
};

// A file that exists, without an "insert volume" requester for a volume that isn't mounted.
static bool existsQuietly(const std::string &path)
{
    size_t colon = path.find(':');
    if (colon != std::string::npos && colon > 0) {
        std::string dev = path.substr(0, colon);
        struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
        bool found = IDOS->FindDosEntry(dl, dev.c_str(), LDF_ALL) != nullptr;
        IDOS->UnLockDosList(LDF_ALL | LDF_READ);
        if (!found) return false;
    }
    return fileExists(path);
}

static std::string getVar(const char *name)
{
    char buf[512];
    int32 n = IDOS->GetVar(name, buf, sizeof buf, 0);
    return n >= 0 ? trim(std::string(buf, n)) : std::string();
}

std::string App::boingCommand(std::string *how)
{
    if (!project.interpreter.empty()) {
        if (how) *how = "project setting";
        return project.interpreter;
    }
    std::string env = getVar("AmiForge/Boing");
    if (!env.empty()) {
        if (how) *how = "ENV:AmiForge/Boing";
        return env;
    }
    for (const char *const *c = basicCandidates; *c; c++)
        if (existsQuietly(*c)) {
            if (how) *how = "found";
            return *c;
        }
    return "";
}

static const char *noBoingText =
    "AmiForge can't find the Boing BASIC compiler (boing).\n\n"
    "Set it in Project > Settings (Boing BASIC command), or for every project with\n"
    "    setenv SAVE AmiForge/Boing <path of boing>\n"
    "(it also looks in C:, SDK:C and BoingBASIC:).";

bool App::basicReady()
{
    std::string how;
    std::string boing = boingCommand(&how);
    if (boing.empty() || !existsQuietly(boing)) {
        info(boing.empty() ? std::string(noBoingText)
                           : "The Boing BASIC compiler " + boing + " (" + how + ") isn't there.\n\n" + noBoingText);
        pendingAfterBuild = 0;
        return false;
    }
    if (project.basicTarget().empty()) {
        info("There is nothing to build: the project has no .bbp or .bas file.");
        pendingAfterBuild = 0;
        return false;
    }
    return true;
}

// what: "build", "rebuild", "clean", "check" (Run's quick check)
void App::buildBasic(const std::string &what)
{
    basicLastWhat = what;
    if (!saveAll()) return;
    saveProject();
    diagnostics.clear();
    buildLog.clear();
    refreshMarkers();
    showBottomPage(0);
    std::vector<std::pair<std::string, std::string>> cmds;
    const std::string out = project.outputPath();
    if (what == "clean" || what == "rebuild") {
        if (existsQuietly(out)) cmds.push_back({"Cleaning", "Delete \"" + out + "\" QUIET FORCE"});
        if (what == "clean") {
            if (cmds.empty()) {
                setStatus("Nothing to clean");
                return;
            }
            builder.startCommands(project.dir, cmds);
            setStatus("Cleaning...");
            updateToolbar();
            return;
        }
    }
    if (!basicReady()) return;
    const std::string boing = boingCommand();
    const std::string target = project.basicTarget();
    cmds.push_back({"Checking " + target, basic::checkCommand(boing, target)});
    if (what != "check")
        cmds.push_back({"Compiling " + target + " to " + fileName(project.output) + " (native, with debug information)",
                        basic::buildCommand(boing, target, project.output, false)});
    if (!builder.startCommands(project.dir, cmds)) {
        info("Couldn't start boing.");
        pendingAfterBuild = 0;
        return;
    }
    setStatus(what == "check" ? "Checking " + project.name + "..." : "Building " + project.name + "...");
    updateToolbar();
}

void App::runBasic()
{
    if (!basicReady()) return;
    const std::string boing = boingCommand();
    const std::string target = project.basicTarget();
    int n = ++basicRuns;
    std::string errFile = strprintf("T:AmiForge-bberr.%d", n);
    IDOS->Delete(errFile.c_str());
    std::string con = project.console.empty()
        ? strprintf("CON:40/60/700/320/%s/AUTO/CLOSE/WAIT", project.name.c_str())
        : project.console;
    BPTR in = IDOS->Open(con.c_str(), MODE_OLDFILE);
    if (!in) {
        info("Can't open the console " + con);
        return;
    }
    std::string cmd = project.runcmd.empty()
        ? basic::runCommand(boing, target, project.args, errFile)
        : project.runcmd;
    BPTR dir = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
    BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
    runStack = 256 * 1024;
    int32 rc = runSystem(cmd, in, fileName(target));
    runStack = 0;
    if (dir) {
        IDOS->SetCurrentDir(old);
        IDOS->UnLock(dir);
    }
    if (rc == -1) {
        IDOS->Close(in);
        info("Couldn't start " + boing);
        return;
    }
    if (project.runcmd.empty()) runTrackBasic(errFile);
    setStatus("Running " + target + " (interpreted); Shift-Ctrl-F5 stops it");
}

void App::basicRunEnded(const std::string &errorFile)
{
    std::string text;
    bool have = readFile(errorFile, text) && !trim(text).empty();
    IDOS->Delete(errorFile.c_str());
    if (!have) return;
    if (builder.busy()) {
        setStatus("The program stopped with a runtime error (see its console)");
        return;
    }
    diagnostics.clear();
    buildLog.clear();
    buildLog.detach();
    BuildLine first;
    auto add = [&](const BuildLine &l, int pen) {
        diagnostics.push_back(l);
        struct Node *nd = buildLog.add({l.text}, (void *)(intptr_t)diagnostics.size());
        if (nd && pen >= 0) buildLog.setPens(nd, pen);
        integrationsBuildLine(l, (int)diagnostics.size() - 1);
    };
    BuildLine head;
    head.kind = MsgKind::Info;
    head.text = "== " + project.basicTarget() + " stopped with a runtime error:";
    add(head, penNote);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        BuildLine l;
        l.kind = MsgKind::Output;
        l.text = text.substr(p, e - p);
        p = e + 1;
        if (trim(l.text).empty()) continue;
        std::string file, msg;
        int line = 0;
        int pen = -1;
        if (basic::parseRuntimeError(l.text, file, line, msg)) {
            l.kind = MsgKind::Error;
            l.file = file.find(':') == std::string::npos ? joinPath(project.dir, file) : file;
            l.line = line;
            pen = penError;
            if (first.file.empty()) first = l;
        } else if (parseDiagnostic(l.text, project.dir, l)) {
            pen = l.kind == MsgKind::Error ? penError : l.kind == MsgKind::Warning ? penWarning : -1;
            if (l.kind == MsgKind::Error && first.file.empty()) first = l;
        }
        add(l, pen);
    }
    buildLog.attach(buildLog.count() - 1);
    showBottomPage(0);
    refreshMarkers();
    if (!first.file.empty()) {
        gotoLocation(first.file, first.line);
        setStatus(first.text);
    }
}

} // namespace af
