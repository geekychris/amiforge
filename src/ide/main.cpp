// AmiForge entry point. Starts from the Shell (AmiForge [project.afp | files...]) or from
// Workbench (project or source icons passed as arguments).
#include "App.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <workbench/startup.h>

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
    if (!openMainWindow()) {
        closeMainWindow();
        IDOS->PutStr("AmiForge: can't open the main window\n");
        return 20;
    }
    initPens(this);

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
        for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    }
    for (auto &a : args) {
        if (extension(a) == "afp") openProject(a);
        else openFile(a);
    }
    if (docs.empty()) newDocument();
    setStatus("Ready - F5 debug, F7 build, Ctrl-F5 run, F9 breakpoint");

    eventLoop();

    closeFindWindow();
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
