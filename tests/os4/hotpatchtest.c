/*
 * hotpatchtest — Edit and Continue on AmigaOS 4: runs tests/targets/hptarget under the
 * debugger and patches compute() twice while it runs (dbg_hotpatch), checking that the new
 * code runs, the program's data is kept, breakpoints move, stepping and locals work in the
 * new code, and that a change needing a restart is refused.
 *   hotpatchtest [DIR [trace]]
 * DIR holds hptarget and hotpatch/<v1|v2|v3|bad>/compute.o (default DH1:AmiForge).
 */
#undef __USE_INLINE__
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbg/dbg.h"

static dbg_session *S;
static int g_pass, g_fail;
static char g_dir[256] = "DH1:AmiForge";
static struct MsgPort *tport;
static struct TimeRequest *treq;

static void ok(int cond, const char *fmt, ...) __attribute__((format(__printf__, 2, 3)));
static void ok(int cond, const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s %s\n", cond ? "PASS" : "FAIL", buf);
    fflush(stdout);
    if (cond) g_pass++; else g_fail++;
}

static void trace_line(void *ud, const char *line) { printf("    | %s\n", line); }

static int wait_event(dbg_event *ev, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    for (;;) {
        while (dbg_poll(S, ev)) {
            if (ev->type == DBG_EV_MESSAGE) { printf("  message: %s\n", ev->text); continue; }
            printf("  event: %s\n", ev->text);
            fflush(stdout);
            return 1;
        }
        if (waited >= timeout_ms) return 0;
        treq->Request.io_Command = TR_ADDREQUEST;
        treq->Time.Seconds = 0; treq->Time.Microseconds = 20000;
        IExec->SendIO((struct IORequest *)treq);
        IExec->Wait(dbg_signal_mask(S) | (1UL << tport->mp_SigBit));
        if (!IExec->CheckIO((struct IORequest *)treq)) IExec->AbortIO((struct IORequest *)treq);
        IExec->WaitIO((struct IORequest *)treq);
        waited += 20;
    }
}

static int expect_stop(const char *what, const char *func, int line, dbg_event *out)
{
    dbg_event ev;
    if (!wait_event(&ev, 10000)) { ok(0, "%s: timed out", what); return 0; }
    if (ev.type != DBG_EV_STOPPED) { ok(0, "%s: got %s", what, ev.type == DBG_EV_EXITED ? "exit" : "?"); return 0; }
    int good = ev.function && strstr(ev.function, func) && ev.line == line;
    ok(good, "%s: stopped in %s at line %d (want %s:%d)", what, ev.function ? ev.function : "?", ev.line, func, line);
    if (out) *out = ev;
    return good;
}

static long eval_long(const char *expr)
{
    dbg_var *v = dbg_eval(S, expr);
    long r = v && v->value ? strtol(v->value, NULL, 0) : -99999;
    printf("  %s = %s\n", expr, v && v->value ? v->value : "?");
    dbg_var_release(S, v);
    return r;
}

static int patch(const char *version, char *msg, size_t n)
{
    char obj[300];
    snprintf(obj, sizeof obj, "%s/hotpatch/%s/compute.o", g_dir, version);
    int r = dbg_hotpatch(S, obj, "compute.c", msg, n);
    printf("  patch %s: %s\n", version, msg);
    return r;
}

static uint32_t bp_addr(int id)
{
    dbg_breakpoint b[8];
    int n = dbg_bp_list(S, b, 8);
    for (int i = 0; i < n; i++) if (b[i].id == id) return b[i].addr;
    return 0;
}

static void test(void)
{
    char prog[300], msg[1024];
    snprintf(prog, sizeof prog, "%s/hptarget", g_dir);
    IDOS->Delete("T:hptarget.out");
    if (!dbg_load(S, prog, "", g_dir, "T:hptarget.out")) { ok(0, "load %s: %s", prog, dbg_last_error(S)); return; }
    int bp = dbg_bp_add_function(S, "compute");
    ok(bp >= 0, "breakpoint on compute()");
    uint32_t old_fn = dbg_function_addr(S, "compute"), old_bp = bp_addr(bp);   /* the body (after the prologue) */
    /* the entry: back from the body to the prologue's stwu r1,-N(r1) */
    uint32_t entry = 0;
    for (uint32_t a = old_fn; a + 64 > old_fn && a >= 4; a -= 4) {
        uint32_t w = 0;
        dbg_read_memory(S, a, &w, 4);
        if ((w & 0xFFFF0000u) == 0x94210000u) { entry = a; break; }
    }
    ok(entry != 0, "compute()'s entry is at %08lx", (unsigned long)entry);
    ok(dbg_run(S, 0), "run");
    dbg_event ev;
    if (!expect_stop("first call (version 1)", "compute", 7, &ev)) return;
    ok(eval_long("counter") == 7, "counter is 7 before the first call has counted");

    /* a change that needs a restart: refused, nothing changes */
    ok(!patch("bad", msg, sizeof msg), "a new static variable is refused");
    ok(strstr(msg, "extra") != NULL, "the message names it");
    ok(dbg_function_addr(S, "compute") == old_fn, "compute() unchanged after the refusal");
    ok(dbg_hotpatch_count(S) == 0, "no patches yet");

    /* version 2 */
    ok(patch("v2", msg, sizeof msg), "version 2 applied");
    ok(strstr(msg, "running") != NULL, "it says the running call keeps the old code");
    uint32_t v2_fn = dbg_function_addr(S, "compute");
    ok(v2_fn && v2_fn != old_fn, "compute() is now at %08lx (was %08lx)", (unsigned long)v2_fn, (unsigned long)old_fn);
    uint32_t nb = bp_addr(bp);
    ok(nb && nb != old_bp && nb == v2_fn, "the breakpoint moved into the new code (%08lx)", (unsigned long)nb);
    uint32_t word = 0;
    dbg_read_memory(S, entry, &word, 4);
    ok((word >> 26) == 18 || (word & 0xFFFF0000u) == 0x3C000000u, "the old entry is a branch now (%08lx)", (unsigned long)word);
    ok(dbg_hotpatch_count(S) == 1, "one patch");

    /* call 1 finishes in version 1; call 2 runs version 2 */
    ok(dbg_continue(S), "continue");
    if (!expect_stop("second call (version 2)", "compute", 13, &ev)) return;
    ok(ev.pc >= v2_fn && ev.pc < v2_fn + 0x100, "stopped in the new code (%08lx)", (unsigned long)ev.pc);
    ok(eval_long("counter") == 8, "counter kept its value (8)");
    ok(eval_long("x") == 1, "the parameter x is 1");
    dbg_frame fr[8];
    int nf = dbg_stack(S, fr, 8);
    ok(nf >= 2 && fr[0].file && strstr(fr[0].file, "compute.c") && fr[1].function && strstr(fr[1].function, "main"),
       "stack: compute (%s:%d) called from %s", nf ? (fr[0].file ? fr[0].file : "-") : "-", nf ? fr[0].line : 0, nf > 1 ? fr[1].function : "-");
    ok(dbg_step_over(S), "step over");
    expect_stop("step in version 2", "compute", 14, NULL);
    ok(dbg_step_over(S), "step over");
    expect_stop("step in version 2", "compute", 15, NULL);
    ok(eval_long("counter") == 9, "counter++ in the new code (9)");
    ok(dbg_step_into(S), "step into bonus()");
    expect_stop("into the new function bonus()", "bonus", 8, NULL);
    ok(dbg_step_out(S), "step out");
    expect_stop("back in compute", "compute", 15, NULL);

    /* version 3, patched over version 2 while version 2 is running */
    ok(patch("v3", msg, sizeof msg), "version 3 applied");
    uint32_t v3_fn = dbg_function_addr(S, "compute");
    ok(v3_fn && v3_fn != v2_fn && v3_fn != old_fn, "compute() moved again (%08lx)", (unsigned long)v3_fn);
    ok(dbg_continue(S), "continue");
    if (!expect_stop("third call (version 3)", "compute", 7, &ev)) return;
    ok(ev.pc >= v3_fn && ev.pc < v3_fn + 0x100, "stopped in version 3 (%08lx)", (unsigned long)ev.pc);
    ok(eval_long("x") == 2, "x is 2");
    ok(eval_long("counter") == 9, "counter is 9");

    /* run to the end */
    dbg_bp_remove(S, bp);
    ok(dbg_continue(S), "continue to the end");
    if (!wait_event(&ev, 10000) || ev.type != DBG_EV_EXITED) { ok(0, "program exits"); return; }
    ok(ev.exit_code == 0, "exit code 0");

    /* what it printed: call 1 = v1, call 2 = v2, calls 3..10 = v3 */
    BPTR fh = IDOS->Open("T:hptarget.out", MODE_OLDFILE);
    char out[1024] = "";
    if (fh) { int32 n = IDOS->Read(fh, out, sizeof out - 1); if (n > 0) out[n] = 0; IDOS->Close(fh); }
    printf("  output:\n%s", out);
    ok(strstr(out, "i=0 r=0\n") != NULL, "call 1 ran version 1");
    ok(strstr(out, "i=1 r=1003\n") != NULL, "call 2 ran version 2");
    ok(strstr(out, "i=2 r=2010\n") != NULL && strstr(out, "i=9 r=2045\n") != NULL, "calls 3-10 ran version 3");
    ok(strstr(out, "counter=17\n") != NULL, "counter counted every call across the versions (17)");

    /* a new run uses the file on disk again */
    ok(dbg_run(S, 0), "run again");
    if (wait_event(&ev, 10000) && ev.type == DBG_EV_EXITED) {
        fh = IDOS->Open("T:hptarget.out", MODE_OLDFILE);
        out[0] = 0;
        if (fh) { int32 n = IDOS->Read(fh, out, sizeof out - 1); if (n > 0) out[n] = 0; IDOS->Close(fh); }
        ok(strstr(out, "i=1 r=2\n") != NULL && dbg_hotpatch_count(S) == 0, "the second run is version 1 again");
        ok(dbg_function_addr(S, "compute") != v3_fn, "and its debug info is the program's");
    } else ok(0, "second run exits");
}

/* C++: a virtual method patched while objects exist (their vtables lead to the new code) */
static void test_cplus(void)
{
    char prog[300], obj[300], msg[1024];
    snprintf(prog, sizeof prog, "%s/hpcpp", g_dir);
    snprintf(obj, sizeof obj, "%s/hotpatch/cpp2/shape.o", g_dir);
    IDOS->Delete("T:hpcpp.out");
    if (!dbg_load(S, prog, "", g_dir, "T:hpcpp.out")) { ok(0, "load %s: %s", prog, dbg_last_error(S)); return; }
    int bp = dbg_bp_add(S, "shape.cpp", 10, NULL);
    ok(dbg_run(S, 0), "C++: run");
    dbg_event ev;
    if (!expect_stop("C++: first area() call", "Square::area", 10, &ev)) return;
    ok(eval_long("side") == 3, "C++: side is 3");
    int r = dbg_hotpatch(S, obj, "shape.cpp", msg, sizeof msg);
    printf("  patch: %s\n", msg);
    ok(r, "C++: area() patched");
    ok(dbg_continue(S), "C++: continue");
    if (!expect_stop("C++: second area() call (new code)", "Square::area", 10, &ev)) return;
    ok(eval_long("side") == 4, "C++: side is 4 (the object kept its state)");
    ok(eval_long("this->side") == 4, "C++: this->side is 4");
    dbg_bp_remove(S, bp);
    ok(dbg_continue(S), "C++: continue to the end");
    if (!wait_event(&ev, 10000) || ev.type != DBG_EV_EXITED) { ok(0, "C++: program exits"); return; }
    ok(ev.exit_code == 0, "C++: exit code 0");
    BPTR fh = IDOS->Open("T:hpcpp.out", MODE_OLDFILE);
    char out[512] = "";
    if (fh) { int32 n = IDOS->Read(fh, out, sizeof out - 1); if (n > 0) out[n] = 0; IDOS->Close(fh); }
    printf("  output:\n%s", out);
    ok(strstr(out, "area=9\narea=160\narea=250\narea=360\n") != NULL, "C++: the virtual call ran the new area() from the second call on");
}

int main(int argc, char **argv)
{
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    tport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
    if (!treq || IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0)) { printf("no timer\n"); return 20; }
    S = dbg_create();
    if (!S) { printf("dbg_create failed\n"); return 20; }
    if (argc > 2 && !strcmp(argv[2], "trace")) dbg_set_trace(S, trace_line, NULL);
    test();
    if (dbg_get_state(S) != DBG_NONE) dbg_unload(S);
    test_cplus();
    if (dbg_get_state(S) != DBG_NONE) dbg_unload(S);
    dbg_destroy(S);
    IExec->CloseDevice((struct IORequest *)treq);
    IExec->FreeSysObject(ASOT_IOREQUEST, treq);
    IExec->FreeSysObject(ASOT_PORT, tport);
    printf("hotpatchtest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 10 : 0;
}
