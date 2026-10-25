// Git and GitHub, through SimpleGit (sgit, from the AmigaOS 4.1 SDK).
//
// sgit takes paths relative to its current directory (it can't parse "DH1:x"), so commands
// run in the repository's directory. Its built-in certificate list predates GitHub's current
// certificates, so GIT_SSL_NO_VERIFY is set for it (the connection is still encrypted).
// Push and pull need credentials: a GitHub user name and token kept in
// ENVARC:AmiForge/GitHubUser and GitHubToken go into the URL; without them sgit asks for
// them in a console window.
#include "App.h"

#include <string.h>
#include <strings.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <dos/dostags.h>
#include <dos/var.h>

namespace af {

static std::string sgitPath()
{
    for (const char *p : { "SDK:C/sgit", "SDK:Local/C/sgit", "C:sgit" }) {
        struct DosList *dl = IDOS->LockDosList(LDF_ALL | LDF_READ);
        std::string dev = std::string(p).substr(0, std::string(p).find(':'));
        bool have = IDOS->FindDosEntry(dl, dev.c_str(), LDF_ALL) != nullptr;
        IDOS->UnLockDosList(LDF_ALL | LDF_READ);
        if (have && fileExists(p)) return p;
    }
    return "";
}

static std::string getVar(const char *name)
{
    char buf[512];
    if (IDOS->GetVar(name, buf, sizeof buf, GVF_GLOBAL_ONLY) < 0) return "";
    return trim(buf);
}

static void setVar(const char *name, const std::string &value)
{
    // ENV: for now and ENVARC: for the next boot
    IDOS->SetVar(name, value.c_str(), value.size(), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

// The repository the project (or the current file) is in: the nearest drawer with a .git.
std::string App::gitRepoDir()
{
    std::string d = !project.dir.empty() ? project.dir : (cur && !cur->path.empty() ? dirName(cur->path) : "");
    for (int i = 0; i < 16 && !d.empty(); i++) {
        if (fileExists(joinPath(d, ".git"))) return d;
        std::string up = dirName(d);
        if (up == d || up.empty()) break;
        d = up;
    }
    return "";
}

// "ref: refs/heads/main" -> "main"
static std::string currentBranch(const std::string &repo)
{
    std::string head;
    if (!readFile(joinPath(repo, ".git/HEAD"), head)) return "master";
    head = trim(head);
    const char *pfx = "ref: refs/heads/";
    if (head.compare(0, strlen(pfx), pfx) == 0) return head.substr(strlen(pfx));
    return "master";
}

// origin's URL from .git/config
static std::string originUrl(const std::string &repo)
{
    std::string cfg;
    if (!readFile(joinPath(repo, ".git/config"), cfg)) return "";
    size_t sec = cfg.find("[remote \"origin\"]");
    if (sec == std::string::npos) return "";
    size_t url = cfg.find("url", sec);
    size_t nextSec = cfg.find('[', sec + 1);
    if (url == std::string::npos || (nextSec != std::string::npos && url > nextSec)) return "";
    size_t eq = cfg.find('=', url), eol = cfg.find('\n', url);
    return trim(cfg.substr(eq + 1, eol == std::string::npos ? std::string::npos : eol - eq - 1));
}

// https://github.com/x/y -> https://user:token@github.com/x/y
static std::string withCredentials(const std::string &url)
{
    std::string user = getVar("AmiForge/GitHubUser"), token = getVar("AmiForge/GitHubToken");
    if (user.empty() || token.empty() || url.compare(0, 8, "https://") != 0) return "";
    if (url.find('@') != std::string::npos) return url;
    return "https://" + user + ":" + token + "@" + url.substr(8);
}

static std::string q(const std::string &s)
{
    return s.find_first_of(" \t\"") == std::string::npos ? s : "\"" + s + "\"";
}

bool App::gitRun(const std::string &dir, const std::vector<std::pair<std::string, std::string>> &cmds, const std::string &title)
{
    if (builder.busy()) {
        setStatus("A build or git command is already running");
        return false;
    }
    std::string sgit = sgitPath();
    if (sgit.empty()) {
        info("Git support needs SimpleGit (sgit) from the AmigaOS 4.1 SDK: install sgit.lha from the SDK "
             "so that SDK:C/sgit exists.");
        return false;
    }
    IDOS->SetVar("GIT_SSL_NO_VERIFY", "1", 1, GVF_GLOBAL_ONLY);
    std::vector<std::pair<std::string, std::string>> full;
    for (auto &c : cmds) full.push_back({c.first, sgit + " " + c.second});
    diagnostics.clear();
    buildLog.clear();
    showBottomPage(0);
    gitBusy = true;
    if (!builder.startCommands(dir, full)) {
        gitBusy = false;
        return false;
    }
    setStatus(title + "...");
    updateToolbar();
    return true;
}

static std::string redact(std::string s)
{
    // never show a token in the log
    size_t at = s.find('@'), sch = s.find("://");
    if (at != std::string::npos && sch != std::string::npos && at > sch) s = s.substr(0, sch + 3) + "***" + s.substr(at);
    return s;
}

void App::gitStatus()
{
    std::string repo = gitRepoDir();
    if (repo.empty()) { info("This project isn't in a git repository (no .git drawer above it)."); return; }
    gitRun(repo, { {"git status (" + currentBranch(repo) + ")", "status"} }, "git status");
}

void App::gitDiff()
{
    std::string repo = gitRepoDir();
    if (repo.empty()) { info("This project isn't in a git repository."); return; }
    saveAll();
    gitRun(repo, { {"git diff", "diff"} }, "git diff");
}

void App::gitLog()
{
    std::string repo = gitRepoDir();
    if (repo.empty()) { info("This project isn't in a git repository."); return; }
    gitRun(repo, { {"git log", "log"} }, "git log");
}

void App::gitCommit()
{
    std::string repo = gitRepoDir();
    if (repo.empty()) { info("This project isn't in a git repository."); return; }
    if (!saveAll()) return;
    saveProject();
    std::string name = getVar("AmiForge/GitName"), email = getVar("AmiForge/GitEmail");
    if (name.empty() || email.empty()) {
        info("Set your name and email first: Git > GitHub Setup.");
        gitSetup();
        name = getVar("AmiForge/GitName");
        email = getVar("AmiForge/GitEmail");
        if (name.empty() || email.empty()) return;
    }
    std::string msg;
    if (!askString("Commit", "Commit message:", msg) || trim(msg).empty()) return;
    msg = trim(msg);
    for (auto &c : msg) if (c == '"') c = '\'';
    std::vector<std::pair<std::string, std::string>> cmds;
    cmds.push_back({"Setting the author", "config user.name " + q(name)});
    cmds.push_back({"", "config user.email " + q(email)});
    // the project's files (relative to the repository) and the project file itself
    std::vector<std::string> files;
    for (auto &f : project.files) files.push_back(relativeTo(repo, project.abs(f)));
    if (!project.path.empty()) files.push_back(relativeTo(repo, project.path));
    for (auto &f : files) cmds.push_back({"Adding " + f, "add " + q(f)});
    cmds.push_back({"Committing", "commit -m \"" + msg + "\""});
    gitRun(repo, cmds, "git commit");
}

void App::gitPushPull(bool push)
{
    std::string repo = gitRepoDir();
    if (repo.empty()) { info("This project isn't in a git repository."); return; }
    std::string branch = currentBranch(repo), url = originUrl(repo);
    std::string remote = withCredentials(url);
    std::string what = push ? "push" : "pull";
    if (!remote.empty()) {
        std::string args = push ? "push " + q(remote) + " refs/heads/" + branch
                                : "pull " + q(remote) + " refs/heads/" + branch;
        gitRun(repo, { {"git " + what + " " + redact(remote) + " " + branch, args} }, "git " + what);
        return;
    }
    // no stored token: sgit asks for a user name and password (a GitHub token) in a console
    std::string sgit = sgitPath();
    if (sgit.empty()) { gitRun(repo, {}, ""); return; }
    IDOS->SetVar("GIT_SSL_NO_VERIFY", "1", 1, GVF_GLOBAL_ONLY);
    std::string con = "CON:60/60/640/240/git " + what + " - enter your GitHub user name and token/AUTO/CLOSE/WAIT";
    BPTR in = IDOS->Open(con.c_str(), MODE_OLDFILE);
    if (!in) return;
    BPTR dir = IDOS->Lock(repo.c_str(), SHARED_LOCK);
    BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
    std::string cmd = sgit + " " + what + " origin refs/heads/" + branch;
    IDOS->SystemTags(cmd.c_str(), SYS_Input, in, SYS_Output, 0, SYS_Asynch, TRUE,
                     SYS_StackSize, 256 * 1024, NP_Name, (uint32)"AmiForge git", TAG_END);
    if (dir) {
        IDOS->SetCurrentDir(old);
        IDOS->UnLock(dir);
    }
    setStatus("git " + what + " is running in its own window");
}

void App::gitClone()
{
    std::string url;
    if (!askString("Clone a Repository", "Repository URL (e.g. https://github.com/user/project):", url) || trim(url).empty())
        return;
    url = trim(url);
    // default to the drawer above the current project, which is usually where projects live
    std::string start = !project.dir.empty() ? dirName(project.dir) : (lastDir.empty() ? "RAM:" : lastDir);
    std::string parent = requestDrawer("Clone into which drawer?", start);
    if (parent.empty()) return;
    std::string name = fileName(url);
    if (extension(name) == "git") name = name.substr(0, name.size() - 4);
    if (name.empty()) name = "repository";
    if (!askString("Clone a Repository", "Name of the new drawer:", name) || trim(name).empty()) return;
    std::string src = withCredentials(url);
    if (src.empty()) src = url;
    pendingCloneDir = joinPath(parent, trim(name));
    gitRun(parent, { {"git clone " + redact(src) + " " + trim(name), "clone " + q(src) + " " + q(trim(name))} }, "git clone");
}

// After a clone: open the project it contains, or offer to make one.
void App::gitAfterClone(bool ok)
{
    std::string dir = pendingCloneDir;
    pendingCloneDir.clear();
    if (!ok || dir.empty()) return;
    lastDir = dir;
    BPTR lock = IDOS->Lock(dir.c_str(), SHARED_LOCK);
    std::string afp;
    if (lock) {
        APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, EX_DoCurrentDir, TRUE, TAG_END);
        if (ctx) {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx)))
                if (EXD_IS_FILE(ed) && extension((const char *)ed->Name) == "afp") afp = (const char *)ed->Name;
            IDOS->ReleaseDirContext(ctx);
        }
        IDOS->UnLock(lock);
    }
    if (!afp.empty()) {
        openProject(joinPath(dir, afp));
        return;
    }
    if (ask("Clone", "Cloned into " + dir + ".\nMake an AmiForge project for it?", "_Make Project|_Not Now") == 1) {
        lastDir = dir;
        newProject();
    }
}

void App::gitSetup()
{
    std::string name = getVar("AmiForge/GitName"), email = getVar("AmiForge/GitEmail");
    std::string user = getVar("AmiForge/GitHubUser"), token = getVar("AmiForge/GitHubToken");
    if (!askString("GitHub Setup", "Your name (for commits):", name)) return;
    if (!askString("GitHub Setup", "Your email (for commits):", email)) return;
    if (!askString("GitHub Setup", "GitHub user name (for push/pull; empty to be asked each time):", user)) return;
    std::string tok = token.empty() ? "" : "(unchanged)";
    if (!askString("GitHub Setup",
                   "GitHub personal access token with repo access\n(github.com > Settings > Developer settings > Tokens).\n"
                   "It's kept in ENVARC:AmiForge/GitHubToken.", tok)) return;
    setVar("AmiForge/GitName", trim(name));
    setVar("AmiForge/GitEmail", trim(email));
    setVar("AmiForge/GitHubUser", trim(user));
    if (tok != "(unchanged)") setVar("AmiForge/GitHubToken", trim(tok));
    setStatus("Git settings saved");
}

} // namespace af
