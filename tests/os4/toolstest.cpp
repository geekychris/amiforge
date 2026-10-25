/*
 * toolstest — exercises the debugger tools on AmigaOS 4 against tests/targets: the leak
 * tracker (leaky), the profiler (hot) and the Intuition inspector (windows). It drives the
 * dbg API the way the IDE does and checks the models' results. Prints PASS/FAIL lines.
 *   toolstest [DIR [leaks|profile|inspect|all [LOGFILE [trace]]]]
 * DIR holds the targets and src/ with their sources (default DH1:AmiForge).
 */
#undef __USE_INLINE__
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/timer.h>
#include <exec/debug.h>
#include <exec/tasks.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>

#include "dbg/dbg.h"
#include "ide/LeakModel.h"
#include "ide/ProfileModel.h"
#include "ide/InspectSnapshot.h"

using namespace af;

static dbg_session *S;
static int g_pass, g_fail;
static char g_dir[256] = "DH1:AmiForge";
static char g_log[300];
static struct MsgPort *tport;
static struct TimeRequest *treq;

static void out(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
static void out(const char *fmt, ...)
{
    char buf[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (!g_log[0]) return;
    BPTR fh = IDOS->Open(g_log, MODE_READWRITE);
    if (fh) { IDOS->ChangeFilePosition(fh, 0, OFFSET_END); IDOS->Write(fh, buf, (int32)strlen(buf)); IDOS->Close(fh); }
}

static void ok(bool cond, const char *fmt, ...) __attribute__((format(__printf__, 2, 3)));
static void ok(bool cond, const char *fmt, ...)
{
    char buf[500];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out("%s %s\n", cond ? "PASS" : "FAIL", buf);
    if (cond) g_pass++; else g_fail++;
}

static const char *P(const char *name) { static char b[4][300]; static int k; char *r = b[k++ & 3]; snprintf(r, 300, "%s/%s", g_dir, name); return r; }

/* Line of the "@name" marker in a target source. */
static int L(const char *src, const char *name)
{
    char path[300], line[512], pat[64];
    snprintf(path, sizeof path, "%s/src/%s", g_dir, src);
    snprintf(pat, sizeof pat, "@%s", name);
    FILE *f = fopen(path, "r");
    int n = 0;
    if (!f) { out("  (can't open %s)\n", path); return 0; }
    while (fgets(line, sizeof line, f)) {
        n++;
        char *m = strstr(line, pat);
        if (m) { char c = m[strlen(pat)]; if (c != '_' && !(c >= 'a' && c <= 'z')) { fclose(f); return n; } }
    }
    fclose(f);
    out("  (no marker %s in %s)\n", name, src);
    return 0;
}

static void tick(uint32_t ms)
{
    treq->Request.io_Command = TR_ADDREQUEST;
    treq->Time.Seconds = 0;
    treq->Time.Microseconds = ms * 1000;
    IExec->SendIO((struct IORequest *)treq);
    IExec->Wait(dbg_signal_mask(S) | (1UL << tport->mp_SigBit));
    if (!IExec->CheckIO((struct IORequest *)treq)) IExec->AbortIO((struct IORequest *)treq);
    IExec->WaitIO((struct IORequest *)treq);
}

/* Next STOPPED/EXITED event, calling drain() on every poll (as the IDE does). */
static uint32_t now_ms()
{
    struct DateStamp d;
    IDOS->DateStamp(&d);
    return (uint32_t)((d.ds_Days * 1440 + d.ds_Minute) * 60000 + d.ds_Tick * 20);
}

template <class F> static int wait_event(dbg_event *ev, uint32_t timeout_ms, F drain)
{
    uint32_t start = now_ms();
    for (uint32_t waited = 0;; waited = now_ms() - start) {
        drain();
        while (dbg_poll(S, ev)) {
            if (ev->type == DBG_EV_MESSAGE) { out("  message: %s\n", ev->text); continue; }
            out("  event: %s\n", ev->text);
            return 1;
        }
        if (waited >= timeout_ms) return 0;
        tick(20);
    }
}

static CodeLoc resolve(uint32_t a)
{
    CodeLoc c;
    if (dbg_addr_in_program(S, a)) {
        c.inProgram = true;
        const char *f = nullptr, *fn = nullptr;
        int l = 0;
        if (dbg_addr_to_line(S, a, &f, &l, &fn)) {
            if (f) c.file = f;
            if (fn) c.function = fn;
            c.line = l;
        }
    } else {
        char buf[160];
        c.function = dbg_addr_module(S, a, buf, sizeof buf) ? buf : "?";
    }
    return c;
}

/* Where a stuck debuggee is: state, priority and a few IP samples. */
static void probe_task()
{
    struct Task *t = (struct Task *)dbg_task(S);
    if (!t) { out("  (no task)\n"); return; }
    struct DebugIFace *idebug = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, NULL);
    for (int i = 0; i < 5 && idebug; i++) {
        struct ExceptionContext c;
        uint8 st = t->tc_State;
        if (st == TS_READY || st == TS_WAIT || st == TS_SUSPENDED) idebug->ReadTaskContext(t, &c, RTCF_STATE | RTCF_SPECIAL);
        else c.ip = c.lr = 0;
        char m[160] = "?";
        dbg_addr_module(S, c.ip, m, sizeof m);
        out("  (task state %d pri %d ip %08lx lr %08lx %s; sigwait %08lx)\n", st, t->tc_Node.ln_Pri, (unsigned long)c.ip,
            (unsigned long)c.lr, m, (unsigned long)t->tc_SigWait);
        tick(100);
    }
    if (idebug) IExec->DropInterface((struct Interface *)idebug);
}

static void trace_line(void *, const char *line) { out("    | %s\n", line); }

static void cleanup()
{
    out("  (cleanup: state %d)\n", (int)dbg_get_state(S));
    if (dbg_get_state(S) == DBG_RUNNING || dbg_get_state(S) == DBG_STOPPED) {
        dbg_kill(S);
        dbg_event ev;
        while (dbg_poll(S, &ev)) {}
    }
    dbg_unload(S);
    dbg_breakpoint bl[32];
    int n = dbg_bp_list(S, bl, 32);
    for (int i = 0; i < n; i++) dbg_bp_remove(S, bl[i].id);
    dbg_track_resources(S, 0);
    dbg_profile(S, 0, 0);
}

static bool endsWith(const std::string &s, const char *suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/* ------------------------------------------------------------------ leaks */

static void test_leaks()
{
    out("-- leaks\n");
    LeakModel m;
    static dbg_res_event evs[256];
    auto drain = [&]() { int n; while ((n = dbg_resource_events(S, evs, 256)) > 0) m.addAll(evs, n); };
    if (!dbg_load(S, P("leaky"), "", g_dir, "NIL:")) { ok(false, "load leaky: %s", dbg_last_error(S)); return; }
    dbg_track_resources(S, 1);
    ok(dbg_run(S, 0), "run leaky with resource tracking");
    dbg_event ev;
    int got = wait_event(&ev, 30000, drain);
    ok(got && ev.type == DBG_EV_EXITED && ev.exit_code == 0, "leaky ran to the end (exit code %d)", got ? ev.exit_code : -1);
    if (!got) {
        struct Task *t = (struct Task *)dbg_task(S);
        out("  (state %d, task %p state %d)\n", (int)dbg_get_state(S), (void *)t, t ? t->tc_State : -1);
        if (dbg_pause(S) && wait_event(&ev, 3000, drain)) {
            dbg_frame fr[16];
            int n = dbg_stack(S, fr, 16);
            for (int i = 0; i < n; i++)
                out("  #%d %08lx %s %s:%d [%s]\n", fr[i].level, (unsigned long)fr[i].pc, fr[i].function,
                    fr[i].file ? fr[i].file : "-", fr[i].line, fr[i].module);
        }
    }
    drain();
    const char *note = dbg_resources_note(S);
    ok(!note[0], "malloc tracked (note: \"%s\")", note);
    ok(dbg_resources_lost(S) == 0, "no events lost (%lu)", (unsigned long)dbg_resources_lost(S));
    auto groups = m.report(resolve);
    out("  %lu events, %lu outstanding, %lu unknown releases\n", (unsigned long)m.events(), (unsigned long)m.outstanding(),
        (unsigned long)m.unknownReleases());
    for (auto &g : groups)
        out("  leak: %-28s %-14s %s:%d %s\n", LeakModel::describe(g).c_str(), dbg_res_kind_name(g.kind),
            g.loc.file.c_str(), g.loc.line, g.loc.function.c_str());
    struct Want { const char *marker; int kind; const char *fn; };
    const Want want[] = {
        { "leak_malloc", DBG_RES_MALLOC, "leak_memory" }, { "leak_vec", DBG_RES_VEC, "leak_memory" },
        { "leak_realloc", DBG_RES_MALLOC, "leak_memory" }, { "leak_file", DBG_RES_FILE, "leak_dos" },
        { "leak_lock", DBG_RES_LOCK, "leak_dos" }, { "leak_lib", DBG_RES_LIBRARY, "leak_exec" },
        { "leak_port", DBG_RES_SYSOBJ, "leak_exec" },
    };
    for (auto &w : want) {
        int line = L("leaky.c", w.marker);
        const LeakGroup *found = nullptr;
        for (auto &g : groups)
            if (g.kind == w.kind && g.loc.line == line && endsWith(g.loc.file, "leaky.c")) found = &g;
        ok(found && found->items.size() == 1 && found->loc.function == w.fn, "%s reported at leaky.c:%d in %s (%s)",
           w.marker, line, w.fn, found ? LeakModel::describe(*found).c_str() : "missing");
    }
    // the released ones aren't reported: only the 7 above come from leaky.c
    int fromProgram = 0;
    for (auto &g : groups) if (endsWith(g.loc.file, "leaky.c")) fromProgram += (int)g.items.size();
    ok(fromProgram == 7, "exactly 7 leaks from the program's code (%d)", fromProgram);
    for (auto &g : groups)
        if (g.kind == DBG_RES_MALLOC && g.loc.line == L("leaky.c", "leak_realloc"))
            ok(g.bytes == 4000, "realloc'd block reported with its new size (%lu)", (unsigned long)g.bytes);
    ok(m.problems(resolve).empty(), "no misuse reported");
    cleanup();
    // the patches are gone: a second, untracked run records nothing
    if (!dbg_load(S, P("leaky"), "", g_dir, "NIL:")) { ok(false, "reload leaky"); return; }
    LeakModel m2;
    auto drain2 = [&]() { int n; while ((n = dbg_resource_events(S, evs, 256)) > 0) m2.addAll(evs, n); };
    ok(dbg_run(S, 0), "run leaky without tracking");
    got = wait_event(&ev, 30000, drain2);
    drain2();
    ok(got && ev.type == DBG_EV_EXITED, "untracked run ended");
    ok(m2.events() == 0, "untracked run recorded nothing (%lu)", (unsigned long)m2.events());
    cleanup();
}

/* ------------------------------------------------------------------ profile */

static void test_profile()
{
    out("-- profile\n");
    {   /* baseline: hot under the debugger without the profiler */
        if (!dbg_load(S, P("hot"), "", g_dir, "NIL:")) { ok(false, "load hot: %s", dbg_last_error(S)); return; }
        uint32_t t0 = now_ms();
        dbg_run(S, 0);
        dbg_event ev;
        auto none = []() {};
        int got = wait_event(&ev, 20000, none);
        ok(got && ev.type == DBG_EV_EXITED, "baseline: hot without the profiler ended (%lu ms)", (unsigned long)(now_ms() - t0));
        if (!got) probe_task();
        cleanup();
    }
    uint32_t t0 = now_ms();
    ProfileModel p;
    static dbg_sample smp[512];
    auto drain = [&]() { int n; while ((n = dbg_profile_samples(S, smp, 512)) > 0) p.addAll(smp, n); };
    if (!dbg_load(S, P("hot"), "", g_dir, "NIL:")) { ok(false, "load hot: %s", dbg_last_error(S)); return; }
    dbg_profile(S, 1, 1000);
    ok(dbg_run(S, 0), "run hot with the profiler");
    dbg_event ev;
    int got = wait_event(&ev, 20000, drain);
    ok(got && ev.type == DBG_EV_EXITED && ev.exit_code == 0, "hot ran to the end (%lu ms)", (unsigned long)(now_ms() - t0));
    if (!got) probe_task();
    drain();
    p.finish(resolve);
    out("  %lu samples over %lu ms, %lu ms waiting, %lu lost; %s\n", (unsigned long)p.samples(),
        (unsigned long)(p.time() / 1000), (unsigned long)(p.waiting() / 1000),
        (unsigned long)dbg_profile_lost(S), dbg_profile_note(S));
    int shown = 0;
    for (auto &f : p.functions())
        if (shown++ < 8)
            out("  %-36s self %5.1f%%  inOS %5.1f%%  total %5.1f%%  %s:%d\n", f.name.c_str(), p.percent(f.self),
                p.percent(f.inOS), p.percent(f.total), f.file.c_str(), f.line);
    ok(p.samples() >= 50, "enough samples (%lu)", (unsigned long)p.samples());
    ok(p.time() > 1500000 && p.time() < 4000000, "the samples cover the run (%lu ms)", (unsigned long)(p.time() / 1000));
    ok(p.waiting() > 150000 && p.waiting() < 900000, "some 0.4 s of it waiting (%lu ms)", (unsigned long)(p.waiting() / 1000));
    const ProfFunction *hot = nullptr, *cold = nullptr, *snooze = nullptr, *mn = nullptr;
    for (auto &f : p.functions()) {
        if (f.name == "hot_loop") hot = &f;
        if (f.name == "cold_loop") cold = &f;
        if (f.name == "snooze") snooze = &f;
        if (f.name == "main") mn = &f;
    }
    ok(hot && p.functions().front().name == "hot_loop", "hot_loop is the hottest function");
    ok(hot && hot->self > (cold ? cold->self : 0) * 3, "hot_loop >> cold_loop (%lu vs %lu)",
       (unsigned long)(hot ? hot->self : 0), (unsigned long)(cold ? cold->self : 0));
    ok(hot && hot->line == L("hot.c", "hot"), "hot_loop's hottest line is the loop body (%d, want %d)",
       hot ? hot->line : 0, L("hot.c", "hot"));
    ok(snooze && snooze->waiting > 0 && snooze->inOS >= snooze->waiting,
       "snooze's wait is OS time attributed to it (%lu in OS, %lu waiting)",
       (unsigned long)(snooze ? snooze->inOS : 0), (unsigned long)(snooze ? snooze->waiting : 0));
    ok(mn && hot && mn->total >= hot->self, "main's total includes its callees (%lu >= %lu)",
       (unsigned long)(mn ? mn->total : 0), (unsigned long)(hot ? hot->self : 0));
    auto files = p.lines();
    bool lineHeat = false;
    for (auto &f : files)
        if (endsWith(f.first, "hot.c")) {
            auto it = f.second.find(L("hot.c", "hot"));
            lineHeat = it != f.second.end() && it->second.self > 0;
        }
    ok(lineHeat, "the @hot line has samples");
    cleanup();
}

/* hot under the debugger without any tool: how long it takes, and that it ends */
static void test_baseline()
{
    out("-- baseline\n");
    if (!dbg_load(S, P("hot"), "", g_dir, "NIL:")) { ok(false, "load hot: %s", dbg_last_error(S)); return; }
    uint32_t t0 = now_ms();
    dbg_run(S, 0);
    dbg_event ev;
    auto none = []() {};
    int got = wait_event(&ev, 30000, none);
    ok(got && ev.type == DBG_EV_EXITED, "hot without tools ended (%lu ms)", (unsigned long)(now_ms() - t0));
    if (!got) {
        struct Task *t = (struct Task *)dbg_task(S);
        out("  (task %p state %d pri %d)\n", (void *)t, t ? t->tc_State : -1, t ? t->tc_Node.ln_Pri : 0);
    }
}

/* ------------------------------------------------------------------ inspect */

static void test_inspect()
{
    out("-- inspect\n");
    if (!dbg_load(S, P("windows"), "", g_dir, "NIL:")) { ok(false, "load windows: %s", dbg_last_error(S)); return; }
    int line = L("windows.c", "ready");
    dbg_bp_add(S, "windows.c", line, nullptr);
    std::set<uint32_t> before = currentScreens();
    ok(dbg_run(S, 0), "run windows");
    dbg_event ev;
    auto none = []() {};
    int got = wait_event(&ev, 15000, none);
    ok(got && ev.type == DBG_EV_STOPPED && ev.line == line, "stopped at @ready (line %d)", got ? ev.line : 0);
    InspectSnapshot snap;
    takeInspectSnapshot((struct Task *)dbg_task(S), before, snap);
    ok(snap.task.valid, "task found (%s)", snap.task.name.c_str());
    ok(snap.windows.size() == 1, "one window of the program (%lu)", (unsigned long)snap.windows.size());
    if (snap.windows.size() == 1) {
        const WindowInfo &w = snap.windows[0];
        ok(w.title == "AmiForge inspector test", "title '%s'", w.title.c_str());
        ok(w.left == 60 && w.top == 80 && w.width == 260 && w.height == 90, "box %d,%d %dx%d", w.left, w.top, w.width, w.height);
        std::string idcmp = decodeIdcmp(w.idcmp);
        ok(idcmp.find("CLOSEWINDOW") != std::string::npos && idcmp.find("GADGETUP") != std::string::npos,
           "IDCMP %s", idcmp.c_str());
        ok(w.sigBit >= 0 && (snap.task.sigAlloc & (1u << w.sigBit)), "UserPort signal bit %d is allocated", w.sigBit);
        const GadgetInfo *okg = nullptr, *cancel = nullptr;
        int sys = 0;
        for (auto &g : w.gadgets) {
            if (g.id == 1) okg = &g;
            if (g.id == 2) cancel = &g;
            if (g.type & 0x8000) sys++;
        }
        ok(okg && okg->text == "OK" && okg->left == 20 && okg->width == 60, "gadget 1 'OK' at 20 (%s)", okg ? okg->text.c_str() : "missing");
        ok(cancel && cancel->text == "Cancel", "gadget 2 'Cancel'");
        ok(sys >= 2, "system gadgets listed too (%d)", sys);
        auto rows = inspectRows(snap);
        bool row = false;
        for (auto &r : rows) if (r.kind == InspKind::Gadget && r.name == "Gadget 1 (BOOLGADGET) 'OK'") row = true;
        ok(row, "panel row for the OK gadget");
        struct Screen *scr = nullptr;
        struct Window *win = nullptr;
        int box[4];
        ok(findWindowBox(w.addr, -1, &scr, &win, box) && box[0] == 60 && box[1] == 80 && box[2] == 60 + 259 && box[3] == 80 + 89,
           "findWindowBox: window %d,%d-%d,%d", box[0], box[1], box[2], box[3]);
        int gi = -1;
        for (size_t i = 0; i < w.gadgets.size(); i++) if (w.gadgets[i].id == 2) gi = (int)i;
        ok(findWindowBox(w.addr, gi, &scr, &win, box) && box[0] == 60 + 100 && box[1] == 80 + 40 && box[2] == 160 + 69,
           "findWindowBox: gadget %d,%d-%d,%d", box[0], box[1], box[2], box[3]);
    }
    ok(dbg_continue(S), "continue");
    got = wait_event(&ev, 15000, none);
    ok(got && ev.type == DBG_EV_EXITED && ev.exit_code == 0, "windows ended");
    InspectSnapshot after;
    takeInspectSnapshot(nullptr, before, after);
    ok(!after.task.valid && after.windows.empty(), "nothing to show once it has ended");
    cleanup();
}

int main(int argc, char **argv)
{
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    if (argc > 3) {
        snprintf(g_log, sizeof g_log, "%s", argv[3]);
        BPTR fh = IDOS->Open(g_log, MODE_NEWFILE);
        if (fh) IDOS->Close(fh);
    }
    tport = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    treq = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                           ASOIOR_ReplyPort, tport, TAG_DONE);
    if (!treq || IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0)) { out("no timer\n"); return 20; }
    S = dbg_create();
    if (!S) { out("FAIL dbg_create\ntoolstest: 0 passed, 1 failed\n"); return 20; }
    dbg_set_source_path(S, P("src"));
    if (argc > 4 && !strcmp(argv[4], "trace")) dbg_set_trace(S, trace_line, nullptr);
    struct { const char *name; void (*fn)(); } tests[] = {
        { "leaks", test_leaks }, { "profile", test_profile }, { "inspect", test_inspect }, { "baseline", test_baseline },
    };
    const char *only = argc > 2 && strcmp(argv[2], "all") ? argv[2] : nullptr;
    for (auto &t : tests) {
        if (only && strcmp(only, t.name)) continue;
        t.fn();
        cleanup();
    }
    dbg_destroy(S);
    out("toolstest: %d passed, %d failed\n", g_pass, g_fail);
    IExec->CloseDevice((struct IORequest *)treq);
    IExec->FreeSysObject(ASOT_IOREQUEST, treq);
    IExec->FreeSysObject(ASOT_PORT, tport);
    return g_fail ? 5 : 0;
}
