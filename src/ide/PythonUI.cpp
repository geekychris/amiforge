// --- lang ---
// Python projects: Build checks the syntax of every .py file with the interpreter, Run starts the
// main script in a console window through a small runner (LangTools.h) that makes Stop Program
// work and hands an uncaught exception's traceback back to the build log, and Debug explains that
// the debugger is for compiled programs and offers pdb instead.
//
// The interpreter: the project's setting, else ENV:AmiForge/Python, else the first of
// lang::interpreterCandidates() that exists. The AmigaOS 4 port needs PYTHONHOME (and
// PYTHONPATH) or it can't find its standard library and quits without a word; when they aren't
// set and the interpreter's drawer has lib/os.py, AmiForge sets them for the programs it starts.
#include "App.h"
#include "LangTools.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/var.h>

namespace af {

// A file that exists, checked without an "insert volume" requester for a volume that isn't there.
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

// The device a volume is mounted on ("chris" -> "DH1"), "" if `name` isn't a volume.
static std::string deviceOfVolume(const std::string &name)
{
    std::string out;
    struct DosList *dl = IDOS->LockDosList(LDF_VOLUMES | LDF_DEVICES | LDF_READ);
    struct DosList *v = IDOS->FindDosEntry(dl, name.c_str(), LDF_VOLUMES);
    if (v && v->dol_Port) {
        for (struct DosList *d = dl; (d = IDOS->NextDosEntry(d, LDF_DEVICES)) != nullptr;)
            if (d->dol_Port == v->dol_Port && d->dol_Name) {
                const unsigned char *b = (const unsigned char *)BADDR(d->dol_Name);
                out.assign((const char *)b + 1, b[0]);
                break;
            }
    }
    IDOS->UnLockDosList(LDF_VOLUMES | LDF_DEVICES | LDF_READ);
    return out;
}

// Paths handed to Python name the device ("DH1:"), not the volume ("chris:"), which older builds
// of the AmigaOS port can't open (see lang::deviceForm).
static std::string forPython(const std::string &path)
{
    return lang::deviceForm(path, deviceOfVolume);
}

static std::string getVar(const char *name)
{
    char buf[512];
    int32 n = IDOS->GetVar(name, buf, sizeof buf, 0);
    return n >= 0 ? std::string(buf, n) : std::string();
}

std::string App::pythonInterpreter(std::string *how)
{
    return forPython(lang::findInterpreter(project.interpreter, getVar("AmiForge/Python"), existsQuietly, how));
}

std::vector<std::pair<std::string, std::string>> App::pythonEnv(const std::string &interpreter)
{
    std::vector<std::pair<std::string, std::string>> env;
    if (!getVar("PYTHONHOME").empty()) return env;   // the user's own setting wins
    std::string home = forPython(lang::pythonHomeFor(interpreter, existsQuietly));
    if (home.empty()) return env;
    env.push_back({"PYTHONHOME", home});
    if (getVar("PYTHONPATH").empty()) env.push_back({"PYTHONPATH", lang::pythonPathFor(home)});
    return env;
}

static const char *noInterpreterText =
    "AmiForge can't find a Python interpreter.\n\n"
    "Set it in Project > Settings (Python interpreter), or for every project with\n"
    "    setenv SAVE AmiForge/Python <path of python>\n"
    "(it also looks in C:, Python:, Python3: and DH1:python-os4).";

void App::buildPython(const std::string &what)
{
    if (!saveAll()) return;
    saveProject();
    diagnostics.clear();
    buildLog.clear();
    refreshMarkers();
    showBottomPage(0);
    std::vector<std::pair<std::string, std::string>> cmds;
    if (what == "clean" || what == "rebuild") {
        // the only build products: the bytecode caches Python leaves next to imported modules
        std::string cache = project.abs("__pycache__");
        if (existsQuietly(cache)) cmds.push_back({"Cleaning", "Delete __pycache__ ALL QUIET FORCE"});
        if (what == "clean") {
            if (cmds.empty()) {
                setStatus("Nothing to clean: a Python project isn't compiled");
                return;
            }
            builder.startCommands(project.dir, cmds);
            setStatus("Cleaning...");
            updateToolbar();
            return;
        }
    }
    std::string how;
    std::string interp = pythonInterpreter(&how);
    if (interp.empty() || !existsQuietly(interp)) {
        info(interp.empty() ? std::string(noInterpreterText)
                            : "The Python interpreter " + interp + " (" + how + ") isn't there.\n\n" + noInterpreterText);
        pendingAfterBuild = 0;
        return;
    }
    std::vector<std::string> files;
    for (auto &f : project.files)
        if (lang::isPythonFile(f)) files.push_back(f);
    if (files.empty()) {
        info("There is nothing to check: the project has no .py files.");
        pendingAfterBuild = 0;
        return;
    }
    const char *checker = "T:AmiForge-pycheck.py";
    if (!writeFile(checker, lang::pyCheckScript)) {
        info("Can't write the syntax checker to T:.");
        return;
    }
    cmds.push_back({strprintf("Checking %d Python file%s with %s", (int)files.size(), files.size() == 1 ? "" : "s",
                              interp.c_str()),
                    lang::checkCommand(interp, checker, files)});
    if (!builder.startPython(project.dir, cmds, pythonEnv(interp))) {
        info("Couldn't start the syntax check.");
        return;
    }
    setStatus("Checking " + project.name + " (Python starts slowly: give it a moment)...");
    updateToolbar();
}

// Set local variables for the next program this process starts (SystemTags copies them), and
// take them away again afterwards.
namespace {
struct LocalVars {
    std::vector<std::string> names;
    explicit LocalVars(const std::vector<std::pair<std::string, std::string>> &env)
    {
        for (auto &v : env)
            if (IDOS->SetVar(v.first.c_str(), v.second.c_str(), v.second.size(), GVF_LOCAL_ONLY))
                names.push_back(v.first);
    }
    ~LocalVars()
    {
        for (auto &n : names) IDOS->DeleteVar(n.c_str(), GVF_LOCAL_ONLY);
    }
};
}

void App::runPython()
{
    std::string how;
    std::string interp = pythonInterpreter(&how);
    if (interp.empty() || !existsQuietly(interp)) {
        info(interp.empty() ? std::string(noInterpreterText)
                            : "The Python interpreter " + interp + " (" + how + ") isn't there.\n\n" + noInterpreterText);
        return;
    }
    std::string main = project.pythonMain();
    if (main.empty() || !existsQuietly(project.abs(main))) {
        info(main.empty() ? std::string("The project has no .py file to run.")
                          : "Can't find the main script " + project.abs(main) + ".\nSet it in Project > Settings.");
        return;
    }
    const char *runner = "T:AmiForge-pyrun.py";
    if (!writeFile(runner, lang::pyRunScript)) {
        info("Can't write the Python runner to T:.");
        return;
    }
    int n = ++pythonRuns;
    std::string stopFile = strprintf("T:AmiForge-pystop.%d", n), tbFile = strprintf("T:AmiForge-pytb.%d", n);
    IDOS->Delete(stopFile.c_str());
    IDOS->Delete(tbFile.c_str());
    std::string con = project.console.empty()
        ? strprintf("CON:40/60/700/320/%s/AUTO/CLOSE/WAIT", project.name.c_str())
        : project.console;
    BPTR in = IDOS->Open(con.c_str(), MODE_OLDFILE);
    if (!in) {
        info("Can't open the console " + con);
        return;
    }
    // the script by its full device-name path: its drawer goes first on sys.path, so its
    // sibling modules import, and tracebacks name files the editor can open
    std::string cmd = project.runcmd.empty() ? lang::runCommand(interp, runner, stopFile, tbFile, forPython(project.abs(main)), project.args)
                                             : project.runcmd;
    BPTR dir = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
    BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
    int32 rc;
    {
        LocalVars vars(pythonEnv(interp));
        runStack = 2 * 1024 * 1024;   // Python's recursion needs more than a shell's default stack
        rc = runSystem(cmd, in, fileName(main));
        runStack = 0;
    }
    if (dir) {
        IDOS->SetCurrentDir(old);
        IDOS->UnLock(dir);
    }
    if (rc == -1) {
        IDOS->Close(in);
        info("Couldn't start " + interp);
        return;
    }
    if (project.runcmd.empty()) runTrackPython(stopFile, tbFile);
    setStatus("Running " + main + " (Python takes a while to start); Shift-Ctrl-F5 stops it");
}

void App::pythonRunEnded(const std::string &tracebackFile)
{
    std::string text;
    if (!readFile(tracebackFile, text) || trim(text).empty()) {
        IDOS->Delete(tracebackFile.c_str());
        return;
    }
    IDOS->Delete(tracebackFile.c_str());
    if (builder.busy()) {   // a build owns the log: just say so
        setStatus("The script stopped with an exception (see its console)");
        return;
    }
    diagnostics.clear();
    buildLog.clear();
    buildLog.detach();
    lang::TracebackParser parser;
    BuildLine first;
    auto add = [&](const BuildLine &l, int pen) {
        diagnostics.push_back(l);
        struct Node *nd = buildLog.add({l.text}, (void *)(intptr_t)diagnostics.size());
        if (nd && pen >= 0) buildLog.setPens(nd, pen);
        integrationsBuildLine(l, (int)diagnostics.size() - 1);   // explanations
    };
    BuildLine head;
    head.kind = MsgKind::Info;
    head.text = "== " + project.pythonMain() + " stopped with an exception:";
    add(head, penNote);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        BuildLine l;
        l.kind = MsgKind::Output;
        l.text = text.substr(p, e - p);
        p = e + 1;
        lang::Diag d = parser.feed(l.text);
        int pen = -1;
        if (d.kind == lang::DiagKind::Location || d.kind == lang::DiagKind::Error) {
            if (!d.file.empty()) l.file = joinPath(project.dir, d.file);
            l.line = d.line;
            l.kind = d.kind == lang::DiagKind::Error ? MsgKind::Error : MsgKind::Note;
            pen = d.kind == lang::DiagKind::Error ? penError : -1;
            if (l.kind == MsgKind::Error && !l.file.empty() && first.file.empty()) first = l;
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

void App::debugPython()
{
    std::string how;
    std::string interp = pythonInterpreter(&how);
    int c = ask("Debug",
                "The AmiForge debugger works on compiled programs (C, C++ and assembler): it can't\n"
                "step through Python code.\n\n"
                "It can run the script under pdb, Python's own debugger, in a console window, with\n"
                "the project's breakpoints set (and it runs to the first one). In pdb:\n"
                "    n  next line      s  step into      r  return from the function\n"
                "    c  continue       b 12  breakpoint  p x  print x   l  list   q  quit",
                "Run with _pdb|Just _Run|_Cancel");
    if (c == 2) { runPython(); return; }
    if (c != 1) return;
    if (interp.empty() || !existsQuietly(interp)) {
        info(noInterpreterText);
        return;
    }
    std::string main = project.pythonMain();
    if (main.empty()) {
        info("The project has no .py file to run.");
        return;
    }
    harvestBreakpoints(cur);
    std::vector<lang::PyBreakpoint> bps;
    for (auto &b : project.breakpoints)
        if (b.enabled && lang::isPythonFile(b.file)) bps.push_back({forPython(project.abs(b.file)), b.line, b.cond});
    std::string con = strprintf("CON:40/60/760/360/%s (pdb)/AUTO/CLOSE/WAIT", project.name.c_str());
    BPTR in = IDOS->Open(con.c_str(), MODE_OLDFILE);
    if (!in) {
        info("Can't open the console " + con);
        return;
    }
    std::string cmd = lang::pdbCommand(interp, forPython(project.abs(main)), project.args, lang::pdbBreakCommands(bps));
    BPTR dir = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
    BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
    int32 rc;
    {
        LocalVars vars(pythonEnv(interp));
        runStack = 2 * 1024 * 1024;
        rc = runSystem(cmd, in, fileName(main) + " (pdb)");
        runStack = 0;
    }
    if (dir) {
        IDOS->SetCurrentDir(old);
        IDOS->UnLock(dir);
    }
    if (rc == -1) {
        IDOS->Close(in);
        info("Couldn't start " + interp);
        return;
    }
    setStatus(strprintf("pdb is starting in its own window%s", bps.empty() ? "" : " (it runs to the first breakpoint)"));
}

} // namespace af
