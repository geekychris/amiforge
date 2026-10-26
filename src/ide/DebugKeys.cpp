// --- lang ---
// The debugger commands that came with the modern key set (KeyBindings.cpp): restart, quick
// watch, enable/disable and condition of the breakpoint on the line, Set Next Statement and Run
// to Function.
#include "App.h"

#include "Scintilla.h"

#include <cstring>

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static int caretLine(SciView *editor)
{
    return (int)S(editor, SCI_LINEFROMPOSITION, S(editor, SCI_GETCURRENTPOS)) + 1;
}

// Shift-Alt-F5: stop, build what changed, and start again.
void App::debugRestart()
{
    if (project.isPython()) { debugPython(); return; }
    if (debugging()) {
        debugStop();
        debugLog("Restarting...");
    }
    buildBeforeRun(2);
}

// Shift-F9: watch the selection, or the word at the cursor; while stopped, show its value too.
void App::quickWatch()
{
    if (!cur || !editor) return;
    std::string e;
    intptr_t a = S(editor, SCI_GETSELECTIONSTART), b = S(editor, SCI_GETSELECTIONEND);
    if (b > a && b - a < 200) {
        std::vector<char> buf(b - a + 1);
        S(editor, SCI_GETSELTEXT, 0, (intptr_t)buf.data());
        e = trim(buf.data());
    } else {
        e = wordAt(S(editor, SCI_GETCURRENTPOS));
    }
    if (e.empty()) {
        setStatus("Put the cursor on a name (or select an expression) to watch it");
        return;
    }
    bool have = false;
    for (auto &w : project.watches) have = have || w == e;
    if (!have) addWatch(e);
    else showSidePage(1);
    if (stopped()) {
        dbg_var *v = dbg_eval(dbg, e.c_str());
        setStatus(e + " = " + (v && v->value ? v->value : "?"));
        if (v) dbg_var_release(dbg, v);
    } else {
        setStatus("Watching " + e + (debugging() ? "" : " (its value shows while the program is stopped)"));
    }
}

int App::breakpointAtCaret()
{
    if (!cur || cur->path.empty()) return -1;
    harvestBreakpoints(cur);
    int line = caretLine(editor);
    for (size_t i = 0; i < project.breakpoints.size(); i++)
        if (project.breakpoints[i].line == line && sameFile(project.abs(project.breakpoints[i].file), cur->path))
            return (int)i;
    return -1;
}

// Alt-F9
void App::toggleBreakpointEnabledAtCaret()
{
    int i = breakpointAtCaret();
    if (i < 0) {
        setStatus("There's no breakpoint on this line (F9 sets one)");
        return;
    }
    bool on = !project.breakpoints[i].enabled;
    setBreakpointEnabled(i, on);
    saveProject();
    setStatus(strprintf("Breakpoint at line %d %s", project.breakpoints[i].line, on ? "enabled" : "disabled"));
}

// Shift-Ctrl-F9 (index < 0: the line at the cursor, setting a breakpoint there if needed), and a
// double-click in the Breakpoints tab.
void App::editBreakpointCondition(int index)
{
    if (index < 0) {
        if (!cur || cur->path.empty()) {
            info("Save the file before setting breakpoints in it.");
            return;
        }
        index = breakpointAtCaret();
        if (index < 0) {
            toggleBreakpoint(cur, caretLine(editor));
            index = breakpointAtCaret();
            if (index < 0) return;
        }
    }
    if (index >= (int)project.breakpoints.size()) return;
    Project::Bp &b = project.breakpoints[index];
    std::string cond = b.cond;
    std::string where = strprintf("%s:%d", b.file.c_str(), b.line);
    if (!askString("Breakpoint Condition",
                   "Stop at " + where + " only when this is true (empty: always stop).\n"
                   "C expressions such as  i == 10  or  p->next != 0  work; for a Python\n"
                   "script, pdb uses a Python expression.", cond))
        return;
    project.breakpoints[index].cond = trim(cond);
    project.breakpoints[index].enabled = true;
    syncDebuggerBreakpoints();
    refreshMarkers();
    refreshBreakpointList();
    saveProject();
    setStatus(project.breakpoints[index].cond.empty() ? "Breakpoint at " + where + " always stops"
                                                      : "Breakpoint at " + where + " stops if " + project.breakpoints[index].cond);
}

// Shift-Ctrl-F10: move the program counter to the cursor's line, inside the function where the
// program is stopped (jumping into another function would leave its stack frame wrong).
void App::setNextStatement()
{
    if (!stopped()) {
        info("Set Next Statement works while the program is stopped.");
        return;
    }
    if (!cur || cur->path.empty()) return;
    if (selectedFrame != 0) {
        info("Set Next Statement works in the innermost frame: select the top line of the Stack tab first.");
        return;
    }
    int line = caretLine(editor), actual = 0;
    uint32_t addr = dbg_line_to_addr(dbg, cur->path.c_str(), line, &actual);
    if (!addr) {
        setStatus("There's no code on this line");
        return;
    }
    dbg_regs r;
    if (!dbg_get_regs(dbg, &r)) return;
    const char *f1 = nullptr, *f2 = nullptr, *fn1 = nullptr, *fn2 = nullptr;
    int l1 = 0, l2 = 0;
    bool here = dbg_addr_to_line(dbg, r.pc, &f1, &l1, &fn1) != 0;
    bool there = dbg_addr_to_line(dbg, addr, &f2, &l2, &fn2) != 0;
    if (!here || !there || !fn1 || !fn2 || strcmp(fn1, fn2) != 0) {
        info(strprintf("Line %d is in %s, but the program is stopped in %s.\n\n"
                       "Set Next Statement only moves within the current function.",
                       line, fn2 ? fn2 : "no known function", fn1 ? fn1 : "code without line information"));
        return;
    }
    if (addr == r.pc) {
        setStatus("The program is already stopped on that line");
        return;
    }
    if (!dbg_set_reg(dbg, "pc", addr)) {
        info(std::string("Couldn't move the program counter: ") + dbg_last_error(dbg));
        return;
    }
    execFile = cur->path;
    execLine = actual > 0 ? actual : line;
    refreshMarkers();
    refreshDebugPanels();
    debugLog(strprintf("Next statement: line %d (0x%08lx). Variables keep their values.", execLine, (unsigned long)addr));
    setStatus(strprintf("The program will go on from line %d", execLine));
}

// Alt-F10: run until a function is called.
void App::runToFunction()
{
    if (!stopped()) {
        info("Run to Function works while the program is stopped. Start debugging first (F5 or F10).");
        return;
    }
    std::string name = cur && editor ? wordAt(S(editor, SCI_GETCURRENTPOS)) : "";
    if (!askString("Run to Function", "Run until this function is called:", name)) return;
    name = trim(name);
    if (name.empty()) return;
    uint32_t addr = dbg_function_addr(dbg, name.c_str());
    const char *file = nullptr, *fn = nullptr;
    int line = 0;
    if (!addr || !dbg_addr_to_line(dbg, addr, &file, &line, &fn) || !file || line <= 0) {
        info("There's no function called " + name + " with line information in the program.");
        return;
    }
    clearExecutionMarkers();
    if (!dbg_run_to(dbg, file, line))
        debugLog(std::string("Run to function failed: ") + dbg_last_error(dbg), penError);
    else
        setStatus("Running to " + name + "()");
    updateToolbar();
}

} // namespace af
