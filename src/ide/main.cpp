// AmiForge entry point. Starts from the Shell (AmiForge [project.afp | files...]) or from
// Workbench (project or source icons passed as arguments).
#include "App.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <workbench/startup.h>

#include <strings.h>   // --- mcp ---

static const char version[] __attribute__((used)) = "$VER: AmiForge 0.1 (24.10.2026)";
static const char stack[] __attribute__((used)) = "$STACK: 524288";

namespace af {

App *gApp;

void initPens(App *a);
void freePens(App *a);

App::App() { gApp = this; }

App::~App()
{
    if (dbg) {
        if (debugging()) dbg_kill(dbg);
        dbg_destroy(dbg);
    }
    gApp = nullptr;
}

int App::run(int argc, char **argv)
{
    // --- mcp --- REXXPORT=name: the ARexx port's name (default AMIFORGE), e.g. for a second copy
    for (int i = 1; argc > 0 && i < argc; i++)
        if (strncasecmp(argv[i], "REXXPORT=", 9) == 0) mcpPortName = argv[i] + 9;
    // --- end mcp ---
    loadRecent();   // before the window opens: the Open Recent menu is built from it
    if (!openMainWindow()) {
        closeMainWindow();
        IDOS->PutStr("AmiForge: can't open the main window\n");
        return 20;
    }
    initPens(this);
    integrationsInit();   // --- integrations ---

    std::vector<std::string> args;
    if (argc == 0) {
        // Workbench: the tool itself is argument 0
        struct WBStartup *wb = (struct WBStartup *)argv;
        for (int i = 1; i < wb->sm_NumArgs; i++) {
            char dir[1024] = "";
            IDOS->NameFromLock(wb->sm_ArgList[i].wa_Lock, dir, sizeof dir);
            args.push_back(joinPath(dir, wb->sm_ArgList[i].wa_Name));
        }
    } else {
        for (int i = 1; i < argc; i++)
            if (strncasecmp(argv[i], "REXXPORT=", 9) != 0) args.push_back(argv[i]);   // --- mcp ---
    }
    for (auto &a : args) {
        if (extension(a) == "afp") openProject(a);
        else openFile(a);
    }
    // started on its own: carry on with the last project
    bool haveLast = !recentProjects.empty() && fileExists(recentProjects.front());
    if (args.empty() && haveLast)
        openProject(recentProjects.front());
    // --- projects --- nothing to carry on with: offer the projects folder
    else if (args.empty() && showBrowserAtStartup())
        browseProjects();
    if (docs.empty()) newDocument();
    // the AutoDoc index (for F1 and completion): read it now rather than on the first keystroke
    setStatus("Reading the AutoDocs index...");
    loadDocIndex();
    setStatus("Ready - F1 docs, Ctrl-Space complete, right-click for more; F7 build, F5 debug");

    eventLoop();

    integrationsCleanup();   // --- integrations ---
    closeBrowser();          // --- robust ---
    runCleanup();
    closeFindWindow();
    closeDocWindow();
    closeDesigner();   // designer
    freePens(this);
    closeMainWindow();
    return 0;
}

} // namespace af

int main(int argc, char **argv)
{
    af::App app;
    return app.run(argc, argv);
}
