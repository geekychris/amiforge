// Build and run from the GUI: the build log, error markers, running the program.
#include "App.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <intuition/screens.h>
#include <graphics/view.h>
#include <dos/dostags.h>

namespace af {

void initPens(App *a)
{
    if (!a->win) return;
    struct ColorMap *cm = a->win->WScreen->ViewPort.ColorMap;
    auto pen = [cm](uint32 r, uint32 g, uint32 b) {
        return (int)IGraphics->ObtainBestPen(cm, r * 0x01010101, g * 0x01010101, b * 0x01010101,
                                             OBP_Precision, PRECISION_GUI, TAG_END);
    };
    a->penError = pen(0xC0, 0x10, 0x10);
    a->penWarning = pen(0xB0, 0x60, 0x00);
    a->penNote = pen(0x20, 0x50, 0xA0);
    a->penDim = pen(0x70, 0x70, 0x70);
    a->penChanged = pen(0xD0, 0x00, 0x00);
}

void freePens(App *a)
{
    if (!a->win) return;
    struct ColorMap *cm = a->win->WScreen->ViewPort.ColorMap;
    for (int *p : { &a->penError, &a->penWarning, &a->penNote, &a->penDim, &a->penChanged })
        if (*p >= 0) { IGraphics->ReleasePen(cm, *p); *p = -1; }
}

void App::build(const std::string &what)
{
    if (builder.busy()) {
        setStatus("A build is already running");
        return;
    }
    if (debugging()) {
        info("Stop debugging before building.");
        return;
    }
    if (!ensureProject()) return;
    if (what != "clean" && !sdkInstalled()) {
        info("Building needs the AmigaOS 4.1 SDK with GCC 11, and it isn't installed here\n"
             "(there is no SDK: assign).\n\n"
             "Install the SDK (from Hyperion's AmigaOS 4.1 SDK archive, including GCC 11), then\n"
             "make sure S:User-Startup has:\n"
             "    assign SDK: <where it is installed>\n"
             "    execute SDK:S/sdk-startup");
        pendingAfterBuild = 0;
        return;
    }
    if (!saveAll()) return;
    saveProject();
    diagnostics.clear();
    buildLog.clear();
    refreshMarkers();
    showBottomPage(0);
    if (!builder.start(project, what)) {
        info("There is nothing to build: the project has no C, C++ or assembler files.");
        pendingAfterBuild = 0;
        return;
    }
    setStatus(what == "clean" ? "Cleaning..." : "Building " + project.name + "...");
    updateToolbar();
}

void App::pollBuild()
{
    BuildLine l;
    bool any = false;
    buildLog.detach();
    while (builder.poll(l)) {
        any = true;
        if (l.kind == MsgKind::Done && gitBusy) {
            gitBusy = false;
            bool ok = builder.lastOk;
            std::string summary = ok ? "Done." : "The git command failed.";
            BuildLine s;
            s.kind = ok ? MsgKind::Info : MsgKind::Error;
            s.text = summary;
            diagnostics.push_back(s);
            struct Node *n = buildLog.add({summary}, (void *)(intptr_t)diagnostics.size());
            if (n) buildLog.setPens(n, ok ? penNote : penError);
            buildLog.attach(buildLog.count() - 1);
            setStatus(summary);
            updateToolbar();
            if (!pendingCloneDir.empty()) gitAfterClone(ok);
            return;
        }
        if (l.kind == MsgKind::Done) {
            bool ok = builder.lastOk;
            std::string summary = ok ? strprintf("Build succeeded (%d warning%s).", builder.warnings, builder.warnings == 1 ? "" : "s")
                                     : strprintf("Build failed: %d error%s, %d warning%s.", builder.errors, builder.errors == 1 ? "" : "s",
                                                 builder.warnings, builder.warnings == 1 ? "" : "s");
            BuildLine s;
            s.kind = ok ? MsgKind::Info : MsgKind::Error;
            s.text = summary;
            diagnostics.push_back(s);
            struct Node *n = buildLog.add({summary}, (void *)(intptr_t)diagnostics.size());
            if (n) buildLog.setPens(n, ok ? penNote : penError);
            setStatus(summary);
            int after = pendingAfterBuild;
            pendingAfterBuild = 0;
            buildLog.attach(buildLog.count() - 1);
            refreshMarkers();
            updateToolbar();
            if (ok && after == 1) runProgram();
            else if (ok && after == 2) debugStart();
            // --- designer ---
            else if (after == 3) { continuePackaging(ok); return; }
            // --- end designer ---
            else if (!ok) {
                // jump to the first error
                for (auto &d : diagnostics)
                    if (d.kind == MsgKind::Error && !d.file.empty() && d.line > 0) {
                        gotoLocation(d.file, d.line, d.col);
                        break;
                    }
            }
            return;
        }
        std::string text = l.text;
        int pen = -1;
        switch (l.kind) {
        case MsgKind::Info: text = "== " + text; pen = penNote; break;
        case MsgKind::Command: text = "   " + text; pen = penDim; break;
        case MsgKind::Error: pen = penError; break;
        case MsgKind::Warning: pen = penWarning; break;
        default: break;
        }
        diagnostics.push_back(l);
        struct Node *n = buildLog.add({text}, (void *)(intptr_t)diagnostics.size());
        if (n && pen >= 0) buildLog.setPens(n, pen);
        if (l.kind == MsgKind::Output && l.text.find("Unknown command") != std::string::npos) {
            BuildLine hint;
            hint.kind = MsgKind::Error;
            hint.text = "A build tool wasn't found: check that the SDK is installed and that SDK:gcc/bin contains it.";
            diagnostics.push_back(hint);
            struct Node *h = buildLog.add({hint.text}, (void *)(intptr_t)diagnostics.size());
            if (h) buildLog.setPens(h, penError);
        }
    }
    buildLog.attach(any ? buildLog.count() - 1 : -1);
}

bool App::buildBeforeRun(int after)
{
    if (!ensureProject()) return false;
    pendingAfterBuild = after;
    build("build");
    return true;
}

void App::runProgram()
{
    std::string exe = project.outputPath();
    // --- designer ---
    if (!project.runcmd.empty()) exe.clear();    // a run command instead of the program
    else
    // --- end designer ---
    if (!fileExists(exe)) {
        info("Can't find " + exe + " - build the project first.");
        return;
    }
    std::string con = project.console.empty()
        ? strprintf("CON:40/60/700/320/%s/AUTO/CLOSE/WAIT", project.name.c_str())
        : project.console;
    BPTR in = IDOS->Open(con.c_str(), MODE_OLDFILE);
    if (!in) {
        info("Can't open the console " + con);
        return;
    }
    std::string cmd = "\"" + exe + "\"" + (project.args.empty() ? "" : " " + project.args);
    // --- designer ---
    if (!project.runcmd.empty()) cmd = project.runcmd;
    // --- end designer ---
    // the program starts in the project's directory
    BPTR dir = IDOS->Lock(project.dir.c_str(), SHARED_LOCK);
    BPTR old = dir ? IDOS->SetCurrentDir(dir) : 0;
    int32 rc = IDOS->SystemTags(cmd.c_str(), SYS_Input, in, SYS_Output, 0, SYS_Asynch, TRUE,
                                NP_Name, (uint32)project.name.c_str(), TAG_END);
    if (dir) {
        IDOS->SetCurrentDir(old);
        IDOS->UnLock(dir);
    }
    if (rc == -1) {
        IDOS->Close(in);
        info("Couldn't start " + exe);
        return;
    }
    setStatus("Started " + (project.runcmd.empty() ? project.output : project.runcmd));   // runcmd: designer
}

} // namespace af
