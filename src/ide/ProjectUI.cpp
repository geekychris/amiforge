// Project handling in the GUI: new/open/save/close, the file list, Find in Files.
#include "App.h"
#include "LangTools.h"   // --- lang ---

#include <algorithm>
#include <string.h>
#include <strings.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <workbench/workbench.h>

#include "Scintilla.h"

namespace af {

static const char *helloC =
    "/* %s - created by AmiForge */\n"
    "#include <stdio.h>\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    int i;\n"
    "    printf(\"Hello from %s!\\n\");\n"
    "    for (i = 1; i <= 3; i++)\n"
    "        printf(\"Line %%d\\n\", i);\n"
    "    return 0;\n"
    "}\n";

static std::string stripExt(const std::string &n)
{
    size_t p = n.rfind('.');
    return p == std::string::npos ? n : n.substr(0, p);
}

static void makeProjectIcon(const std::string &afp);

// Before switching projects: close the old project's files and untouched untitled ones.
// Asks about unsaved changes; false if the user cancelled.
static bool closeProjectFiles(App &app)
{
    std::vector<Document *> toClose;
    for (auto &d : app.docs) {
        bool untouchedUntitled = d->path.empty() && !d->modified;
        if (untouchedUntitled || (!app.project.path.empty() && app.project.contains(d->path)))
            toClose.push_back(d.get());
    }
    for (Document *d : toClose)
        if (!app.closeDocument(d)) return false;
    return true;
}

void App::newProject()
{
    std::string afp = requestFile("New Project - choose a name for the project file", true,
                                  joinPath(lastDir, "myproject.afp"), "#?.afp");
    if (afp.empty()) return;
    if (extension(afp) != "afp") afp += ".afp";
    if (!project.path.empty()) saveProject();
    if (!closeProjectFiles(*this)) return;
    project = Project();
    project.path = absolutePath(afp);
    project.dir = dirName(project.path);
    project.name = stripExt(fileName(afp));
    project.output = project.name;
    // adopt sources already in that directory
    BPTR lock = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
    if (lock) {
        APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, EX_DoCurrentDir, TRUE, TAG_END);
        if (ctx) {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx))) {
                if (!EXD_IS_FILE(ed)) continue;
                std::string n = (const char *)ed->Name;
                std::string e = extension(n);
                if (isSourceFile(n) || e == "h" || e == "hpp" || e == "i" || e == "inc" ||
                    lang::isPythonFile(n))   // --- lang --- a drawer of .py files is a Python project
                    project.files.push_back(n);
            }
            IDOS->ReleaseDirContext(ctx);
        }
        IDOS->UnLock(lock);
    }
    std::sort(project.files.begin(), project.files.end());
    if (project.files.empty()) {
        std::string main = project.abs("main.c");
        if (!fileExists(main)) writeFile(main, strprintf(helloC, "main.c", project.name.c_str()));
        project.files.push_back("main.c");
    }
    std::string err;
    if (!project.save(err)) info(err);
    else makeProjectIcon(project.path);
    refreshProjectList();
    refreshBreakpointList();
    for (auto &f : project.files)
        if (isSourceFile(f) || (project.isPython() && f == project.pythonMain())) { openFile(project.abs(f)); break; }   // --- lang ---
    lastDir = project.dir;
    updateTitle();
    addRecent(project.path);
    setStatus("Created project " + project.path);
}

void App::openProject(const std::string &pathIn)
{
    std::string path = pathIn;
    if (path.empty()) path = requestFile("Open Project", false, openProjectStart(), "#?.afp");   // --- projects ---
    if (path.empty()) return;
    if (!project.path.empty()) saveProject();
    Project p;
    std::string err;
    if (!p.load(path, err)) {
        info(err);
        return;
    }
    if (!closeProjectFiles(*this)) return;
    project = p;
    addRecent(project.path);
    lastDir = project.dir;
    refreshProjectList();
    refreshBreakpointList();
    refreshWatches();
    // open the first source file (main.c if there is one)
    std::string first;
    for (auto &f : project.files) {
        if (!isSourceFile(f)) continue;
        if (first.empty() || strncasecmp(fileName(f).c_str(), "main.", 5) == 0) first = f;
    }
    if (!first.empty()) openFile(project.abs(first));
    refreshMarkers();
    updateTitle();
    setStatus("Opened project " + project.path);
}

// Give a project file a Workbench icon that opens it in AmiForge.
static void makeProjectIcon(const std::string &afp)
{
    if (fileExists(afp + ".info")) return;
    char dir[512] = "", tool[1024];
    BPTR progDir = IDOS->GetProgramDir();
    if (!progDir || !IDOS->NameFromLock(progDir, dir, sizeof dir)) return;
    char name[256] = "AmiForge";
    IDOS->GetCliProgramName(name, sizeof name);   // fails from Workbench: keep the default
    if (!name[0]) strcpy(name, "AmiForge");
    strncpy(tool, joinPath(dir, fileName(name)).c_str(), sizeof tool - 1);
    tool[sizeof tool - 1] = 0;
    struct DiskObject *icon = IIcon->GetDiskObjectNew(afp.c_str());   // a default project icon
    if (!icon) return;
    icon->do_Type = WBPROJECT;
    icon->do_DefaultTool = tool;
    icon->do_StackSize = 512 * 1024;
    IIcon->PutDiskObject(afp.c_str(), icon);
    IIcon->FreeDiskObject(icon);
}

bool App::saveProject()
{
    if (project.path.empty()) return false;
    harvestBreakpoints(cur);
    std::string err;
    if (!project.save(err)) {
        info(err);
        return false;
    }
    makeProjectIcon(project.path);
    return true;
}

void App::closeProject()
{
    if (project.path.empty()) return;
    if (debugging()) debugStop();
    saveProject();
    project = Project();
    diagnostics.clear();
    buildLog.clear();
    refreshProjectList();
    refreshBreakpointList();
    refreshWatches();
    refreshMarkers();
    updateTitle();
}

bool App::ensureProject()
{
    if (!project.files.empty()) return true;
    const bool py = cur && lang::isPythonFile(cur->path);   // --- lang ---
    if (!cur || cur->path.empty() || (!isSourceFile(cur->path) && !py)) {
        info("Open or create a project first (Project > New Project), or save this file as a .c, .cpp, .s or .py file.");
        return false;
    }
    // an implicit, unsaved project for a single file
    std::string keepPath = project.path;
    auto bps = project.breakpoints;
    auto watches = project.watches;
    project = Project();
    project.dir = dirName(cur->path);
    project.name = stripExt(fileName(cur->path));
    project.output = project.name;
    project.files.push_back(fileName(cur->path));
    if (py) project.language = "python";   // --- lang ---
    for (auto &b : bps) {
        b.file = project.rel(joinPath(dirName(keepPath.empty() ? cur->path : keepPath), b.file));
        project.breakpoints.push_back(b);
    }
    project.watches = watches;
    refreshProjectList();
    return true;
}

void App::addFilesToProject()
{
    if (project.path.empty()) {
        info("Create or open a project first.");
        return;
    }
    for (auto &f : requestFiles("Add Files to Project", project.dir)) {
        std::string rel = project.rel(absolutePath(f));
        if (std::find(project.files.begin(), project.files.end(), rel) == project.files.end())
            project.files.push_back(rel);
    }
    saveProject();
    refreshProjectList();
}

void App::removeFileFromProject()
{
    struct Node *n = projectList.selected();
    int idx = (int)(intptr_t)projectList.userData(n) - 1;
    if (idx < 0 || idx >= (int)project.files.size()) {
        info("Select a file in the project list first.");
        return;
    }
    if (ask("Remove File", "Remove " + project.files[idx] + " from the project?\n(The file itself is not deleted.)",
            "_Remove|_Cancel") != 1)
        return;
    project.files.erase(project.files.begin() + idx);
    saveProject();
    refreshProjectList();
}

void App::refreshProjectList()
{
    projectList.clear();
    projectList.detach();
    // sources first, then headers, then the rest; each group sorted
    auto rank = [](const std::string &f) {
        std::string e = extension(f);
        if (isSourceFile(f)) return 0;
        if (e == "h" || e == "hpp" || e == "hh" || e == "i" || e == "inc") return 1;
        return 2;
    };
    std::vector<int> order(project.files.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        int ra = rank(project.files[a]), rb = rank(project.files[b]);
        if (ra != rb) return ra < rb;
        return strcasecmp(project.files[a].c_str(), project.files[b].c_str()) < 0;
    });
    for (int i : order) projectList.add({project.files[i]}, (void *)(intptr_t)(i + 1));
    projectList.attach();
}

void App::projectSettings()
{
    if (project.path.empty() && project.files.empty()) {
        info("Create or open a project first.");
        return;
    }
    if (editProjectSettings()) saveProject();
}

void App::findInFiles()
{
    std::string text = findText;
    if (cur) {
        intptr_t a = editor->send(SCI_GETSELECTIONSTART), b = editor->send(SCI_GETSELECTIONEND);
        if (b > a && b - a < 100) {
            std::vector<char> buf(b - a + 1);
            editor->send(SCI_GETSELTEXT, 0, (intptr_t)buf.data());
            if (!strchr(buf.data(), '\n')) text = buf.data();
        }
    }
    if (!askString("Find in Files", project.files.empty() ? "Search the open files for:" : "Search the project's files for:", text) ||
        text.empty())
        return;
    findText = text;
    std::vector<std::string> files;
    if (!project.files.empty())
        for (auto &f : project.files) files.push_back(project.abs(f));
    else
        for (auto &d : docs) if (!d->path.empty()) files.push_back(d->path);

    std::string needle = text;
    if (!findCase) for (auto &c : needle) c = tolower((unsigned char)c);
    findResults.clear();
    findLog.clear();
    findLog.detach();
    busyPointer(true);
    for (auto &f : files) {
        std::string data;
        // an open, edited document is searched as it is in the editor
        Document *d = findDocument(f);
        if (d && d == cur) {
            intptr_t len = editor->send(SCI_GETLENGTH);
            std::vector<char> buf(len + 1);
            editor->send(SCI_GETTEXT, len + 1, (intptr_t)buf.data());
            data.assign(buf.data(), len);
        } else if (!readFile(f, data)) {
            continue;
        }
        int lineNo = 1;
        size_t pos = 0;
        while (pos <= data.size()) {
            size_t nl = data.find('\n', pos);
            std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            std::string hay = line;
            if (!findCase) for (auto &c : hay) c = tolower((unsigned char)c);
            size_t at = hay.find(needle);
            if (at != std::string::npos) {
                BuildLine r;
                r.kind = MsgKind::Note;
                r.file = f;
                r.line = lineNo;
                r.col = (int)at + 1;
                r.text = trim(line);
                findResults.push_back(r);
                findLog.add({strprintf("%s:%d", project.rel(f).c_str(), lineNo), r.text},
                            (void *)(intptr_t)findResults.size());
            }
            if (nl == std::string::npos) break;
            pos = nl + 1;
            lineNo++;
        }
    }
    busyPointer(false);
    findLog.attach();
    showBottomPage(1);
    setStatus(strprintf("%d match%s for \"%s\"", (int)findResults.size(), findResults.size() == 1 ? "" : "es", text.c_str()));
}

} // namespace af

namespace af {

static const char *recentFile = "AmiForge/RecentProjects";

void App::loadRecent()
{
    recentProjects.clear();
    std::string text;
    if (!readFile(std::string("ENV:") + recentFile, text) && !readFile(std::string("ENVARC:") + recentFile, text))
        return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && recentProjects.size() < 10) recentProjects.push_back(line);
    }
}

void App::saveRecent()
{
    std::string text;
    for (auto &p : recentProjects) text += p + "\n";
    for (const char *dir : { "ENV:AmiForge", "ENVARC:AmiForge" }) {
        BPTR l = IDOS->Lock(dir, SHARED_LOCK);
        if (!l) l = IDOS->CreateDir(dir);
        if (l) IDOS->UnLock(l);
        writeFile(joinPath(dir, "RecentProjects"), text);
    }
}

void App::addRecent(const std::string &afp)
{
    std::string p = absolutePath(afp);
    for (size_t i = 0; i < recentProjects.size(); i++)
        if (strcasecmp(recentProjects[i].c_str(), p.c_str()) == 0) {
            recentProjects.erase(recentProjects.begin() + i);
            break;
        }
    recentProjects.insert(recentProjects.begin(), p);
    if (recentProjects.size() > 10) recentProjects.resize(10);
    saveRecent();
    rebuildMenus();
}

} // namespace af
