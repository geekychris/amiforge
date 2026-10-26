// The debugger in the GUI: start/stop/step, the execution marker, and the panels
// (locals, watches, registers, call stack, breakpoints, memory, disassembly, log).
#include "App.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

void App::debugLog(const std::string &s, int pen)
{
    debugLogView.detach();
    while (debugLogView.count() > 2000) {
        struct Node *n = IExec->RemHead(debugLogView.list);
        IListBrowser->FreeListBrowserNode(n);
    }
    struct Node *n = debugLogView.add({s});
    if (pen >= 0) debugLogView.setPens(n, pen);
    debugLogView.attach(debugLogView.count() - 1);
}

// --- breakpoints <-> debugger ---

void App::syncDebuggerBreakpoints()
{
    if (!dbg) return;
    for (int id : dbgBpIds)
        if (id >= 0) dbg_bp_remove(dbg, id);
    dbgBpIds.assign(project.breakpoints.size(), -1);
    if (dbg_get_state(dbg) == DBG_NONE) return;
    for (size_t i = 0; i < project.breakpoints.size(); i++) {
        auto &b = project.breakpoints[i];
        if (!b.enabled) continue;
        dbgBpIds[i] = dbg_bp_add(dbg, project.abs(b.file).c_str(), b.line, b.cond.empty() ? nullptr : b.cond.c_str());
    }
    // move breakpoints on lines without code to where they really are
    dbg_breakpoint list[256];
    int n = dbg_bp_list(dbg, list, 256);
    bool moved = false;
    for (size_t i = 0; i < dbgBpIds.size(); i++) {
        for (int k = 0; k < n; k++) {
            if (list[k].id != dbgBpIds[i] || list[k].actual_line <= 0) continue;
            if (list[k].actual_line != project.breakpoints[i].line) {
                project.breakpoints[i].line = list[k].actual_line;
                moved = true;
            }
        }
    }
    if (moved) refreshMarkers();
}

void App::setBreakpointEnabled(int index, bool on)
{
    if (index < 0 || index >= (int)project.breakpoints.size()) return;
    harvestBreakpoints(cur);
    project.breakpoints[index].enabled = on;
    syncDebuggerBreakpoints();
    refreshMarkers();
    refreshBreakpointList();
}

void App::refreshBreakpointList()
{
    dbg_breakpoint list[256];
    int n = dbg && dbg_get_state(dbg) != DBG_NONE ? dbg_bp_list(dbg, list, 256) : 0;
    bpList.clear();
    bpList.detach();
    for (size_t i = 0; i < project.breakpoints.size(); i++) {
        auto &b = project.breakpoints[i];
        std::string extra;
        int id = i < dbgBpIds.size() ? dbgBpIds[i] : -1;
        for (int k = 0; k < n; k++)
            if (list[k].id == id) {
                if (list[k].addr == 0) extra = "not resolved";
                else extra = strprintf("%d hit%s", list[k].hits, list[k].hits == 1 ? "" : "s");
            }
        if (!b.cond.empty()) extra += (extra.empty() ? "if " : ", if ") + b.cond;
        bpList.add({"", strprintf("%s:%d", b.file.c_str(), b.line), extra}, (void *)(intptr_t)(i + 1),
                   0, 0x80000000, b.enabled);
    }
    bpList.attach();
}

// --- running ---

void App::debugStart()
{
    if (debugging()) return;
    if (project.isPython()) { debugPython(); return; }   // --- lang ---
    std::string exe = project.outputPath();
    if (!fileExists(exe)) {
        info("Can't find " + exe + " - build the project first.");
        return;
    }
    if (!dbg) dbg = dbg_create();
    if (!dbg) {
        info("The debugger couldn't start.");
        return;
    }
    harvestBreakpoints(cur);
    hotReset();   // --- hotpatch
    dbg_set_source_path(dbg, project.dir.c_str());
    if (!dbg_load(dbg, exe.c_str(), project.args.c_str(), project.dir.c_str(),
                  project.console.empty() ? nullptr : project.console.c_str())) {
        info(std::string("Couldn't load the program for debugging:\n") + dbg_last_error(dbg));
        return;
    }
    profileRun = profileNext;          // --- debugtools ---
    profileNext = false;
    if (!profileRun) syncDebuggerBreakpoints();   // a profile run doesn't stop
    debugLogView.clear();
    debugLog((profileRun ? "Profiling " : "Debugging ") + exe);
    refreshBreakpointList();
    dtBeforeRun();                     // --- debugtools ---
    if (!dbg_run(dbg, profileRun ? 0 : project.stopAtMain)) {
        info(std::string("Couldn't start the program:\n") + dbg_last_error(dbg));
        dbg_unload(dbg);
        return;
    }
    selectedFrame = 0;
    haveLastRegs = false;
    lastValues.clear();
    setStatus("Running " + project.output + "...");
    showSidePage(0);
    updateToolbar();
}

void App::clearExecutionMarkers()
{
    execFile.clear();
    execLine = 0;
    frameFile.clear();
    frameLine = 0;
    refreshMarkers();
}

static void freeVars(App *a)
{
    for (auto &p : a->varAllocs) dbg_var_free(a->dbg, p.first, p.second);
    a->varAllocs.clear();
}

void App::debugContinue()
{
    if (!stopped()) return;
    clearExecutionMarkers();
    if (!dbg_continue(dbg)) debugLog(std::string("Continue failed: ") + dbg_last_error(dbg), penError);
    setStatus("Running...");
    updateToolbar();
}

void App::debugStep(int kind)
{
    if (!stopped()) {
        if (!debugging() && kind != 2) {   // F10/F11 with nothing running: start, stopping at main
            bool keep = project.stopAtMain;
            project.stopAtMain = true;
            buildBeforeRun(2);
            project.stopAtMain = keep;
        }
        return;
    }
    int ok;
    switch (kind) {
    case 0: ok = dbg_step_over(dbg); break;
    case 1: ok = dbg_step_into(dbg); break;
    case 2: ok = dbg_step_out(dbg); break;
    default: ok = dbg_step_instruction(dbg, 0); break;
    }
    if (!ok) debugLog(std::string("Step failed: ") + dbg_last_error(dbg), penError);
    updateToolbar();
}

void App::debugRunToCursor()
{
    if (!cur || cur->path.empty()) return;
    int line = (int)S(editor, SCI_LINEFROMPOSITION, S(editor, SCI_GETCURRENTPOS)) + 1;
    if (!stopped()) {
        info("Run to Cursor works while the program is stopped. Start debugging first (F5 or F10).");
        return;
    }
    clearExecutionMarkers();
    if (!dbg_run_to(dbg, cur->path.c_str(), line))
        debugLog(std::string("Run to cursor failed: ") + dbg_last_error(dbg), penError);
    updateToolbar();
}

void App::debugPause()
{
    if (debugging() && !stopped()) dbg_pause(dbg);
}

void App::debugStop()
{
    if (!dbg) return;
    if (debugging()) dbg_kill(dbg);
    dtSessionEnded();                  // --- debugtools ---
    freeVars(this);
    dbg_unload(dbg);
    dbgBpIds.clear();
    clearExecutionMarkers();
    for (ListView *lv : { &locals, &regs, &stack, &disasm, &memView }) lv->clear();
    refreshWatches();
    refreshBreakpointList();
    setStatus("Debugging stopped");
    debugLog("Stopped.");
    updateToolbar();
}

void App::pollDebugger()
{
    dbg_event ev;
    if (dbg) dtPoll();                 // --- debugtools ---
    while (dbg && dbg_poll(dbg, &ev)) {
        mcpDebugEvent(ev);             // --- mcp ---
        switch (ev.type) {
        case DBG_EV_MESSAGE:
            debugLog(ev.text);
            break;
        case DBG_EV_EXITED:
            debugLog(ev.text[0] ? std::string(ev.text) : strprintf("Program exited with code %d", ev.exit_code), penNote);
            setStatus(strprintf("%s exited with code %d", project.output.c_str(), ev.exit_code));
            dtSessionEnded();          // --- debugtools ---
            freeVars(this);
            dbg_unload(dbg);
            dbgBpIds.clear();
            clearExecutionMarkers();
            for (ListView *lv : { &locals, &regs, &stack, &disasm }) lv->clear();
            refreshWatches();
            refreshBreakpointList();
            updateToolbar();
            break;
        case DBG_EV_STOPPED:
            onStopped(ev);
            break;
        }
    }
}

void App::onStopped(const dbg_event &ev)
{
    selectedFrame = 0;
    dbg_select_frame(dbg, 0);
    const char *src = ev.file ? dbg_resolve_source(dbg, ev.file) : nullptr;
    execFile = src ? absolutePath(src) : "";
    execLine = src ? ev.line : 0;
    frameFile.clear();
    frameLine = 0;
    bool crash = ev.reason == DBG_STOP_EXCEPTION;
    debugLog(ev.text, crash ? penError : -1);
    setStatus(ev.text);
    // the program's console may be in front: come back
    if (win) {
        IIntuition->WindowToFront(win);
        IIntuition->ActivateWindow(win);
    }
    if (!execFile.empty()) {
        gotoLocation(execFile, execLine, 0, true);
        refreshMarkers();   // the document may already have been the current one
    } else {
        refreshMarkers();
        showBottomPage(5);   // no source: show the disassembly
    }
    refreshDebugPanels();
    refreshInspector();                // --- debugtools ---
    if (crash) {
        showBottomPage(2);
        IIntuition->DisplayBeep(nullptr);
        integrationsCrash(ev);   // --- integrations ---
    }
    updateToolbar();
}

void App::refreshDebugPanels()
{
    freeVars(this);
    refreshStack();
    refreshLocals();
    refreshWatches();
    refreshRegisters();
    refreshDisassembly();
    refreshBreakpointList();
    if (memAddr || !getString(GID_MEM_ADDR).empty()) refreshMemory();
}

void App::selectFrame(int level)
{
    if (!stopped()) return;
    dbg_frame frames[64];
    int n = dbg_stack(dbg, frames, 64);
    if (level < 0 || level >= n) return;
    selectedFrame = level;
    dbg_select_frame(dbg, level);
    frameFile.clear();
    frameLine = 0;
    if (frames[level].file) {
        const char *src = dbg_resolve_source(dbg, frames[level].file);
        if (src) {
            std::string f = absolutePath(src);
            if (level > 0) { frameFile = f; frameLine = frames[level].line; }
            gotoLocation(f, frames[level].line);
        }
    }
    refreshMarkers();
    // only the frame-dependent panels; keep the stack list as it is
    for (auto it = varAllocs.begin(); it != varAllocs.end();) {
        dbg_var_free(dbg, it->first, it->second);
        it = varAllocs.erase(it);
    }
    refreshLocals();
    refreshWatches();
    refreshDisassembly();
}

void App::refreshStack()
{
    stack.clear();
    if (!stopped()) return;
    dbg_frame frames[64];
    int n = dbg_stack(dbg, frames, 64);
    stack.detach();
    for (int i = 0; i < n; i++) {
        auto &f = frames[i];
        std::string where = f.file ? strprintf("%s:%d", fileName(f.file).c_str(), f.line)
                                   : strprintf("%s 0x%08lx", f.module ? f.module : "", (unsigned long)f.pc);
        stack.add({strprintf("%d", i), f.function ? f.function : "??", where}, (void *)(intptr_t)i);
    }
    stack.attach(selectedFrame);
}

// A changed value is shown in red: remember last stop's values per panel/name.
static bool changed(App *a, const std::string &key, const std::string &value)
{
    for (auto &p : a->lastValues)
        if (p.first == key) {
            bool c = p.second != value;
            p.second = value;
            return c;
        }
    a->lastValues.push_back({key, value});
    return false;
}

static void addVarRow(App *a, ListView &lv, dbg_var *v, int generation, const std::string &key, struct Node *after = nullptr)
{
    uint32_t flags = v->has_children ? LBFLG_HASCHILDREN : 0;
    struct Node *n = lv.add({v->name ? v->name : "", v->value ? v->value : "", v->type ? v->type : ""},
                            v, generation, flags, false, after);
    if (n && changed(a, key + "/" + (v->name ? v->name : ""), v->value ? v->value : ""))
        lv.setPens(n, a->penChanged);
}

void App::refreshLocals()
{
    locals.clear();
    if (!stopped()) return;
    dbg_var **vars = nullptr;
    int n = dbg_locals(dbg, &vars);
    if (n > 0) varAllocs.push_back({vars, n});
    locals.detach();
    for (int i = 0; i < n; i++) addVarRow(this, locals, vars[i], 0, strprintf("L%d", selectedFrame));
    if (n == 0) locals.add({"(no locals here)", "", ""});
    locals.attach();
}

void App::refreshWatches()
{
    watches.clear();
    watches.detach();
    std::vector<dbg_var *> evaluated;
    for (size_t i = 0; i < project.watches.size(); i++) {
        auto &w = project.watches[i];
        if (stopped()) {
            dbg_var *v = dbg_eval(dbg, w.c_str());
            if (v) {
                // the watch row shows the expression as typed
                dbg_var **arr = (dbg_var **)malloc(sizeof(dbg_var *));
                arr[0] = v;
                varAllocs.push_back({arr, 1});
                uint32_t flags = v->has_children ? LBFLG_HASCHILDREN : 0;
                struct Node *n = watches.add({w, v->value ? v->value : "", v->type ? v->type : ""}, v, 0, flags);
                if (n && changed(this, "W/" + w, v->value ? v->value : "")) watches.setPens(n, penChanged);
                continue;
            }
        }
        watches.add({w, stopped() ? "<error>" : "", ""}, nullptr);
    }
    watches.attach();
}

void App::expandVar(ListView &lv, struct Node *n)
{
    if (!n || !stopped()) return;
    dbg_var *v = (dbg_var *)lv.userData(n);
    if (!v || !v->has_children) return;
    uint32 gen = 0, nextGen = 0;
    IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Generation, &gen, TAG_DONE);
    struct Node *next = n->ln_Succ;
    if (next && next->ln_Succ) {
        IListBrowser->GetListBrowserNodeAttrs(next, LBNA_Generation, &nextGen, TAG_DONE);
        if (nextGen > gen) return;   // already expanded
    }
    dbg_var **kids = nullptr;
    int k = dbg_var_children(dbg, v, &kids);
    if (k <= 0) return;
    varAllocs.push_back({kids, k});
    lv.detach();
    struct Node *after = n;
    for (int i = 0; i < k; i++) {
        uint32_t flags = kids[i]->has_children ? LBFLG_HASCHILDREN : 0;
        after = lv.add({kids[i]->name ? kids[i]->name : "", kids[i]->value ? kids[i]->value : "",
                        kids[i]->type ? kids[i]->type : ""}, kids[i], (int)gen, flags, false, after);
    }
    lv.attach();
}

void App::addWatch(const std::string &expr)
{
    project.watches.push_back(expr);
    saveProject();
    refreshWatches();
    showSidePage(1);
}

void App::removeWatch()
{
    struct Node *n = watches.selected();
    if (!n) return;
    uint32 gen = 0;
    IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Generation, &gen, TAG_DONE);
    // find the top-level row this node belongs to
    int top = -1, i = 0;
    for (struct Node *p = watches.list->lh_Head; p->ln_Succ; p = p->ln_Succ) {
        uint32 g = 0;
        IListBrowser->GetListBrowserNodeAttrs(p, LBNA_Generation, &g, TAG_DONE);
        if (g <= 1) top = i++;
        if (p == n) break;
    }
    if (top >= 0 && top < (int)project.watches.size()) {
        project.watches.erase(project.watches.begin() + top);
        saveProject();
        refreshWatches();
    }
}

void App::refreshRegisters()
{
    regs.clear();
    if (!stopped()) return;
    dbg_regs r;
    if (!dbg_get_regs(dbg, &r)) return;
    regs.detach();
    auto row = [&](const std::string &name, uint32_t now, uint32_t before) {
        struct Node *n = regs.add({name, strprintf("0x%08lx  %ld", (unsigned long)now, (long)(int32_t)now)});
        if (haveLastRegs && now != before) regs.setPens(n, penChanged);
    };
    row("pc", r.pc, lastRegs.pc);
    row("lr", r.lr, lastRegs.lr);
    row("ctr", r.ctr, lastRegs.ctr);
    row("cr", r.cr, lastRegs.cr);
    row("xer", r.xer, lastRegs.xer);
    row("msr", r.msr, lastRegs.msr);
    for (int i = 0; i < 32; i++) row(strprintf(i == 1 ? "r1 (sp)" : "r%d", i), r.gpr[i], lastRegs.gpr[i]);
    for (int i = 0; i < 32; i++) {
        struct Node *n = regs.add({strprintf("f%d", i), strprintf("%.10g", r.fpr[i])});
        if (haveLastRegs && r.fpr[i] != lastRegs.fpr[i]) regs.setPens(n, penChanged);
    }
    regs.attach();
    lastRegs = r;
    haveLastRegs = true;
}

void App::refreshDisassembly()
{
    disasm.clear();
    if (!stopped()) return;
    dbg_frame frames[64];
    int nf = dbg_stack(dbg, frames, 64);
    if (nf <= 0) return;
    uint32_t pc = frames[selectedFrame < nf ? selectedFrame : 0].pc;
    // start a little before the PC so the context is visible
    uint32_t start = pc >= 0x40 ? pc - 0x40 : pc;
    dbg_insn ins[96];
    int n = dbg_disassemble(dbg, start, 96, ins);
    disasm.detach();
    int sel = -1;
    const char *lastFile = nullptr;
    int lastLine = 0;
    for (int i = 0; i < n; i++) {
        if (ins[i].file && ins[i].line && (ins[i].line != lastLine || !lastFile || strcmp(lastFile, ins[i].file))) {
            struct Node *s = disasm.add({strprintf("        %s:%d", fileName(ins[i].file).c_str(), ins[i].line)});
            disasm.setPens(s, penNote);
            lastFile = ins[i].file;
            lastLine = ins[i].line;
        }
        std::string label = ins[i].symbol ? std::string("  <") + ins[i].symbol + ">" : "";
        std::string text = strprintf("%s%s %08lx  %08lx  %s%s", ins[i].addr == pc ? "=>" : "  ",
                                     ins[i].is_breakpoint ? "*" : " ", (unsigned long)ins[i].addr,
                                     (unsigned long)ins[i].word, ins[i].text, label.c_str());
        struct Node *nd = disasm.add({text});
        if (ins[i].addr == pc) {
            sel = disasm.indexOf(nd);
            disasm.setPens(nd, penChanged);
        }
    }
    disasm.attach(sel);
}

void App::refreshMemory()
{
    memView.clear();
    std::string expr = trim(getString(GID_MEM_ADDR));
    if (!stopped()) {
        if (!expr.empty()) memView.add({"(the program isn't stopped)"});
        return;
    }
    uint32_t addr = memAddr;
    if (!expr.empty()) {
        char *end = nullptr;
        unsigned long v = strtoul(expr.c_str(), &end, 0);
        if (end && *end == 0) {
            addr = v;
        } else {
            dbg_var *var = dbg_eval(dbg, expr.c_str());
            if (!var) return;
            // a pointer shows what it points at; anything else shows itself
            const char *val = var->value ? var->value : "";
            const char *hex = strstr(val, "0x");
            if (var->type && strchr(var->type, '*') && hex) addr = strtoul(hex, nullptr, 16);
            else addr = var->addr;
            dbg_var_release(dbg, var);
        }
    }
    memAddr = addr;
    uint8_t buf[512];
    int got = dbg_read_memory(dbg, addr, buf, sizeof buf);
    memView.detach();
    for (int off = 0; off < (int)sizeof buf; off += 16) {
        std::string hexs, ascii;
        for (int i = 0; i < 16; i++) {
            if (off + i < got) {
                hexs += strprintf("%02x%s", buf[off + i], i == 7 ? "  " : " ");
                unsigned char c = buf[off + i];
                ascii += (c >= 32 && c < 127) || c >= 160 ? (char)c : '.';
            } else {
                hexs += i == 7 ? "??  " : "?? ";
                ascii += ' ';
            }
        }
        memView.add({strprintf("%08lx  %s %s", (unsigned long)(addr + off), hexs.c_str(), ascii.c_str())});
        if (off + 16 > got && off >= got) break;
    }
    if (got <= 0) memView.add({strprintf("%08lx  <unreadable>", (unsigned long)addr)});
    memView.attach();
    showBottomPage(4);
}

} // namespace af
