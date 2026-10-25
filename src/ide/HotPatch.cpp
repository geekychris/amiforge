// Edit and Continue: Debug > Apply Code Changes (Ctrl-F9). While the program is stopped,
// the C and C++ sources changed since it was built (or since their last apply) are compiled
// with the project's flags into obj/<name>.hot.o, and the debugger patches them into the
// running program (dbg_hotpatch: new code, same data). See docs/features/hotpatch.md.
#include "App.h"
#include "dbg/dbg.h"

#include <proto/dos.h>

namespace af {

static std::string hotObject(const Project &p, const std::string &src)
{
    std::string o = Builder::objectPath(p, src);
    if (o.size() > 2 && o.compare(o.size() - 2, 2, ".o") == 0) o = o.substr(0, o.size() - 2);
    return o + ".hot.o";
}

void App::applyChanges()
{
    if (!stopped()) {
        info("Apply Code Changes works while the program is stopped in the debugger:\n"
             "at a breakpoint, after a step, or after Pause.");
        return;
    }
    if (builder.busy()) {
        setStatus("Wait for the running build or git command to finish");
        return;
    }
    if (!saveAll()) return;
    int64_t exeTime = fileTime(project.outputPath());
    std::vector<std::pair<std::string, std::string>> cmds;
    std::vector<std::string> files, skipped;
    for (auto &f : project.files) {
        if (!isSourceFile(f)) continue;
        std::string a = project.abs(f);
        int64_t t = fileTime(a), since = exeTime;
        auto it = hotApplied.find(a);
        if (it != hotApplied.end() && it->second > since) since = it->second;
        if (t <= since) continue;
        if (isAsmFile(f)) { skipped.push_back(f); continue; }
        files.push_back(f);
        cmds.push_back({"Compiling " + f + " for Edit and Continue", Builder::compileCommand(project, f, hotObject(project, f))});
    }
    if (files.empty()) {
        if (!skipped.empty()) info("Assembler files can't be patched into a running program:\nrestart it to use the changes to " + skipped[0] + ".");
        else setStatus("No source file has changed since the program was built");
        return;
    }
    diagnostics.clear();
    buildLog.clear();
    refreshMarkers();
    showBottomPage(0);
    hotFiles = files;
    hotBusy = true;
    if (!builder.startCommands(project.dir, cmds)) {
        hotBusy = false;
        info("Couldn't start the compiler.");
        return;
    }
    setStatus("Compiling the changes...");
    updateToolbar();
}

void App::hotAfterCompile(bool ok)
{
    hotBusy = false;
    updateToolbar();
    auto logLine = [&](const std::string &text, MsgKind kind) {
        BuildLine l;
        l.kind = kind;
        l.text = text;
        diagnostics.push_back(l);
        buildLog.detach();
        struct Node *n = buildLog.add({text}, (void *)(intptr_t)diagnostics.size());
        if (n) buildLog.setPens(n, kind == MsgKind::Error ? penError : kind == MsgKind::Warning ? penWarning : penNote);
        buildLog.attach(buildLog.count() - 1);
    };
    if (!ok) {
        logLine("The changes don't compile: nothing was applied.", MsgKind::Error);
        setStatus("The changes don't compile: nothing was applied");
        refreshMarkers();
        for (auto &d : diagnostics)
            if (d.kind == MsgKind::Error && !d.file.empty() && d.line > 0) { gotoLocation(d.file, d.line, d.col); break; }
        return;
    }
    if (!stopped()) {
        logLine("The program isn't stopped any more: nothing was applied.", MsgKind::Error);
        return;
    }
    int applied = 0;
    std::string last;
    for (auto &f : hotFiles) {
        std::string src = project.abs(f), obj = project.abs(hotObject(project, f));
        int64_t srcTime = fileTime(src);
        char msg[1024];
        int r = dbg_hotpatch(dbg, obj.c_str(), f.c_str(), msg, sizeof msg);
        logLine(msg, r ? MsgKind::Info : MsgKind::Error);
        debugLog(msg, r ? penNote : penError);
        if (r) {
            applied++;
            hotApplied[src] = srcTime;
        }
        last = msg;
        IDOS->Delete(obj.c_str());
    }
    hotFiles.clear();
    if (applied) {
        // breakpoints moved to the new code; frames and locals are looked up again
        refreshDebugPanels();
        refreshMarkers();
        setStatus(applied == 1 ? "Code changes applied: continue with F5 or step" : strprintf("Code changes applied to %d files", applied));
    } else {
        setStatus("Code changes not applied - see the build log");
        if (!last.empty()) info(last);
    }
}

} // namespace af
