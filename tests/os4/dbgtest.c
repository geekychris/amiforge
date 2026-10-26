/*
 * dbgtest — exercises the debugger API against the debuggee programs in tests/targets on
 * AmigaOS 4. Prints PASS/FAIL lines and a summary; every wait has a timeout.
 *   dbgtest [DIR [SECTION|all [trace|-] [LOGFILE]]]
 * DIR holds the targets and src/ with their sources (default DH1:AmiForge). SECTION runs one
 * group (exit, basic, crash, pause, oscall, cplus, asm). "trace" prints the runtime trace. LOGFILE gets
 * a copy of the output, closed after every line (it survives a hang).
 */
#undef __USE_INLINE__
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <devices/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbg/dbg.h"

static dbg_session *S;
static int g_pass, g_fail;
static char g_dir[256] = "DH1:AmiForge";
static struct MsgPort *tport;
static struct TimeRequest *treq;

#include <stdarg.h>

/* Output goes to stdout and, line by line, to a log file that is closed after every write
 * so it survives a hang of the machine. */
static char g_log[300];

static void out(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
static void out(const char *fmt, ...)
{
    char buf[600];
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
#define printf out

static void trace_line(void *ud, const char *line) { out("    | %s\n", line); }

/* "memtrace": keep the trace in memory (so it doesn't slow the debugger down and change the
 * timing) and print the last lines when a check fails. */
#define MT_LINES 64
static char mt_buf[MT_LINES][160];
static int mt_next, mt_count;
static void memtrace_line(void *ud, const char *line)
{
    snprintf(mt_buf[mt_next], sizeof mt_buf[0], "%s", line);
    mt_next = (mt_next + 1) % MT_LINES;
    if (mt_count < MT_LINES) mt_count++;
}
static void memtrace_dump(void)
{
    int start = (mt_next - mt_count + MT_LINES) % MT_LINES;
    for (int i = 0; i < mt_count; i++) out("    | %s\n", mt_buf[(start + i) % MT_LINES]);
    mt_count = 0;
}
static int g_memtrace;

static void ok(int cond, const char *fmt, ...) __attribute__((format(__printf__, 2, 3)));
static void ok(int cond, const char *fmt, ...)
{
    char buf[500];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!cond && g_memtrace) memtrace_dump();
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
    if (!f) { printf("  (can't open %s)\n", path); return 0; }
    while (fgets(line, sizeof line, f)) {
        n++;
        char *m = strstr(line, pat);
        if (m) { char c = m[strlen(pat)]; if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) { fclose(f); return n; } }
    }
    fclose(f);
    printf("  (no marker %s in %s)\n", name, src);
    return 0;
}

static const char *reason_name(int r)
{
    static const char *n[] = { "breakpoint", "step", "entry", "pause", "exception" };
    return r >= 0 && r < 5 ? n[r] : "?";
}

/* Milliseconds from the DOS clock (20 ms ticks): waits are measured in real time, since
 * the session's signal can wake Wait() early many times. */
static uint32_t now_ms(void)
{
    struct DateStamp d;
    IDOS->DateStamp(&d);
    return (uint32_t)((d.ds_Days * 1440 + d.ds_Minute) * 60000 + d.ds_Tick * 20);
}

/* Next STOPPED/EXITED event (messages are printed), or 0 after timeout_ms. */
static int wait_event(dbg_event *ev, uint32_t timeout_ms)
{
    uint32_t start = now_ms();
    for (uint32_t waited = 0;; waited = now_ms() - start) {
        while (dbg_poll(S, ev)) {
            if (ev->type == DBG_EV_MESSAGE) { printf("  message: %s\n", ev->text); continue; }
            printf("  event: %s\n", ev->text);
            fflush(stdout);
            return 1;
        }
        if (waited >= timeout_ms) return 0;
        /* the session's signals or a 20 ms tick */
        treq->Request.io_Command = TR_ADDREQUEST;
        treq->Time.Seconds = 0; treq->Time.Microseconds = 20000;
        IExec->SetSignal(0, 1UL << tport->mp_SigBit);   /* WaitIO leaves the reply's signal set */
        IExec->SendIO((struct IORequest *)treq);
        IExec->Wait(dbg_signal_mask(S) | (1UL << tport->mp_SigBit));
        if (!IExec->CheckIO((struct IORequest *)treq)) IExec->AbortIO((struct IORequest *)treq);
        IExec->WaitIO((struct IORequest *)treq);
    }
}

static void print_stack(void)
{
    dbg_frame fr[16];
    int n = dbg_stack(S, fr, 16);
    for (int i = 0; i < n; i++)
        printf("  #%d %08lx %s %s:%d [%s]\n", fr[i].level, (unsigned long)fr[i].pc, fr[i].function,
               fr[i].file ? fr[i].file : "-", fr[i].line, fr[i].module);
}

static int expect_stop(const char *what, int reason, const char *func, const char *file, int line)
{
    dbg_event ev;
    if (!wait_event(&ev, 8000)) {
        ok(0, "%s: timed out", what);
        if (dbg_get_state(S) == DBG_RUNNING && dbg_pause(S) && wait_event(&ev, 2000)) { printf("  (it was at %08lx %s)\n", (unsigned long)ev.pc, ev.text); print_stack(); }
        return 0;
    }
    if (ev.type != DBG_EV_STOPPED) { ok(0, "%s: got %s", what, ev.type == DBG_EV_EXITED ? "exit" : "?"); return 0; }
    int good = ev.reason == reason;
    if (func) good = good && ev.function && strstr(ev.function, func);
    if (file) good = good && ev.file && strstr(ev.file, file);
    if (line) good = good && ev.line == line;
    ok(good, "%s: %s in %s at %s:%d (want %s %s:%d)", what, reason_name(ev.reason), ev.function ? ev.function : "?",
       ev.file ? ev.file : "?", ev.line, func ? func : "", file ? file : "", line);
    return good;
}

static int expect_exit(const char *what, int code)
{
    dbg_event ev;
    if (!wait_event(&ev, 8000)) {
        ok(0, "%s: timed out", what);
        if (dbg_get_state(S) == DBG_RUNNING && dbg_pause(S) && wait_event(&ev, 2000)) { printf("  (it was at %08lx %s)\n", (unsigned long)ev.pc, ev.text); print_stack(); }
        return 0;
    }
    int good = ev.type == DBG_EV_EXITED && (code < 0 || ev.exit_code == code);
    ok(good, "%s: %s code %d (want %d)", what, ev.type == DBG_EV_EXITED ? "exited" : "stopped", ev.exit_code, code);
    return good;
}

static void expect_eval(const char *expr, const char *want)
{
    dbg_var *v = dbg_eval(S, expr);
    ok(!strcmp(v->value, want), "eval %s = %s (want %s)", expr, v->value, want);
    dbg_var_free(S, &v, 1);
}

static void expect_eval_suffix(const char *expr, const char *want)
{
    dbg_var *v = dbg_eval(S, expr);
    size_t a = strlen(v->value), b = strlen(want);
    ok(a >= b && !strcmp(v->value + a - b, want), "eval %s = %s (want ...%s)", expr, v->value, want);
    dbg_var_free(S, &v, 1);
}

static void cleanup(void)
{
    if (dbg_get_state(S) == DBG_RUNNING || dbg_get_state(S) == DBG_STOPPED) {
        dbg_kill(S);
        dbg_event ev;
        while (dbg_poll(S, &ev)) {}
    }
    dbg_unload(S);
    dbg_breakpoint bl[32];
    int n = dbg_bp_list(S, bl, 32);
    for (int i = 0; i < n; i++) dbg_bp_remove(S, bl[i].id);
}

/* ------------------------------------------------------------------ basic.c */

static void test_basic(void)
{
    printf("== basic: breakpoints, conditions, stepping, inspection\n");
    ok(dbg_load(S, P("basic"), "", NULL, "T:dbgtest-basic.txt"), "load basic (%s)", dbg_last_error(S));
    if (dbg_get_state(S) != DBG_LOADED) return;
    dbg_set_source_path(S, P("src"));
    const char *src = dbg_resolve_source(S, "tests/targets/basic.c");
    ok(src && strstr(src, "src/basic.c"), "resolve source: %s", src ? src : "(null)");

    int loop = L("basic.c", "loop_body");
    int bp1 = dbg_bp_add(S, "basic.c", loop, "i == 3");
    dbg_breakpoint bl[8];
    int nb = dbg_bp_list(S, bl, 8);
    ok(nb == 1 && bl[0].addr && bl[0].actual_line == loop, "bp at basic.c:%d resolved to %08lx line %d", loop, (unsigned long)bl[0].addr, bl[0].actual_line);
    int unres = dbg_bp_add(S, "nosuch.c", 10, NULL);
    nb = dbg_bp_list(S, bl, 8);
    ok(nb == 2 && bl[1].addr == 0, "bp in an unknown file stays unresolved");
    dbg_bp_remove(S, unres);
    ok(dbg_function_addr(S, "main") != 0, "function address of main");

    ok(dbg_run(S, 1), "run, stop at main (%s)", dbg_last_error(S));
    if (!expect_stop("stop at main", DBG_STOP_ENTRY, "main", "basic.c", 0)) return;

    ok(dbg_continue(S), "continue");
    if (!expect_stop("conditional breakpoint", DBG_STOP_BREAKPOINT, "main", "basic.c", loop)) return;
    expect_eval("i", "3");
    nb = dbg_bp_list(S, bl, 8);
    ok(nb == 1 && bl[0].hits == 1, "hit count %d", bl[0].hits);

    dbg_var **vars;
    int nv = dbg_locals(S, &vars), seen_total = 0, seen_argc = 0;
    for (int i = 0; i < nv; i++) {
        if (!strcmp(vars[i]->name, "total") && !strcmp(vars[i]->value, "3")) seen_total = 1;
        if (!strcmp(vars[i]->name, "argc") && !strcmp(vars[i]->value, "1")) seen_argc = 1;
    }
    ok(nv >= 10 && seen_total && seen_argc, "locals: %d, total = 3, argc = 1", nv);
    dbg_var_free(S, vars, nv);

    dbg_frame fr[16];
    int nf = dbg_stack(S, fr, 16);
    print_stack();
    ok(nf >= 2 && !strcmp(fr[0].function, "main") && fr[0].line == loop && !strcmp(fr[0].module, "program"), "stack: %d frames, #0 main at line %d", nf, fr[0].line);

    expect_eval("a.name", "\"first\"");
    expect_eval("head->next->name", "\"second\"");
    expect_eval("a.pos.x + a.pos.y", "7");
    expect_eval("a.col", "GREEN");
    expect_eval("buf", "\"buffer\"");
    expect_eval("g_counter", "3");
    expect_eval("s_hidden", "11");
    expect_eval("g_table[2]", "30");
    expect_eval("g_table", "{10, 20, 30, 40}");
    expect_eval_suffix("g_greeting", " \"hello\"");
    expect_eval("g_ratio", "2.5");
    expect_eval("g_flags.c", "-3");
    expect_eval("g_num.f", "1");
    expect_eval("sizeof(struct node)", "28");
    expect_eval("(char)65", "65 'A'");
    expect_eval_suffix("argv[0]", "\"basic\"");
    expect_eval("*(int *)0", "<unreadable>");

    dbg_var *va = dbg_eval(S, "a"), **kids;
    int nk = dbg_var_children(S, va, &kids);
    ok(nk == 5 && !strcmp(kids[1]->value, "\"first\"") && kids[4]->has_children, "children of a: %d", nk);
    if (nk == 5) {
        dbg_var **gk;
        int ng = dbg_var_children(S, kids[4], &gk);
        ok(ng == 5 && !strcmp(gk[1]->value, "\"second\""), "a.next expands to b");
        dbg_var_free(S, gk, ng);
    }
    dbg_var_free(S, kids, nk);
    dbg_var_free(S, &va, 1);

    nv = dbg_globals(S, &vars);
    int seen_g = 0;
    for (int i = 0; i < nv; i++) if (!strcmp(vars[i]->name, "g_counter") && !strcmp(vars[i]->value, "3")) seen_g = 1;
    ok(nv == 7 && seen_g, "globals: %d, g_counter = 3", nv);
    dbg_var_free(S, vars, nv);

    dbg_regs r;
    ok(dbg_get_regs(S, &r) && r.pc == fr[0].pc && r.gpr[1] == fr[0].sp, "registers: pc %08lx r1 %08lx", (unsigned long)r.pc, (unsigned long)r.gpr[1]);
    dbg_insn ins[4];
    int ni = dbg_disassemble(S, r.pc, 4, ins);
    for (int i = 0; i < ni; i++) printf("  %08lx %08lx %-28s %s%s\n", (unsigned long)ins[i].addr, (unsigned long)ins[i].word, ins[i].text,
                                        ins[i].symbol ? ins[i].symbol : "", ins[i].is_breakpoint ? " [bp]" : "");
    ok(ni == 4 && ins[0].is_breakpoint && ins[0].word != 0x7FE00008 && ins[0].text[0] && ins[0].line == loop,
       "disassembly shows the original instruction under the breakpoint");
    uint8_t mem[4];
    ok(dbg_read_memory(S, r.pc, mem, 4) == 4 && !(mem[0] == 0x7f && mem[1] == 0xe0 && mem[3] == 0x08), "memory reads hide breakpoints");
    ok(dbg_read_memory(S, 0, mem, 4) == 0, "page 0 is unreadable");

    dbg_bp_remove(S, bp1);
    int twice = L("basic.c", "main_twice");
    dbg_bp_add(S, "basic.c", twice, NULL);
    dbg_continue(S);
    if (!expect_stop("breakpoint after removing the first", DBG_STOP_BREAKPOINT, "main", "basic.c", twice)) return;

    dbg_step_into(S);
    if (!expect_stop("step into twice", DBG_STOP_STEP, "twice", "basic.c", L("basic.c", "twice_call"))) return;
    dbg_step_into(S);
    if (!expect_stop("step into add", DBG_STOP_STEP, "add", "basic.c", L("basic.c", "add_body"))) return;
    expect_eval("a", "10");
    expect_eval("b", "10");
    nf = dbg_stack(S, fr, 16);
    print_stack();
    ok(nf >= 3 && !strcmp(fr[0].function, "add") && !strcmp(fr[1].function, "twice") && !strcmp(fr[2].function, "main") &&
       fr[1].line == L("basic.c", "twice_call") && fr[2].line == twice, "stack add <- twice <- main with call lines");
    ok(dbg_select_frame(S, 1), "select frame 1");
    expect_eval("v", "10");
    dbg_select_frame(S, 2);
    expect_eval("total", "10");
    dbg_select_frame(S, 0);

    dbg_step_out(S);
    if (!expect_stop("step out of add", DBG_STOP_STEP, "twice", "basic.c", L("basic.c", "twice_call"))) return;
    dbg_step_over(S);
    if (!expect_stop("step over in twice", DBG_STOP_STEP, "twice", "basic.c", L("basic.c", "twice_ret"))) return;
    expect_eval("r", "20");
    dbg_step_out(S);
    if (!expect_stop("step out of twice", DBG_STOP_STEP, "main", "basic.c", twice)) return;
    dbg_step_over(S);
    if (!expect_stop("step over to the next line", DBG_STOP_STEP, "main", "basic.c", L("basic.c", "main_fact"))) return;
    expect_eval("total", "30");

    int fact = dbg_bp_add(S, "basic.c", L("basic.c", "fact_test"), "n == 1");
    dbg_continue(S);
    if (!expect_stop("recursion breakpoint n == 1", DBG_STOP_BREAKPOINT, "factorial", "basic.c", L("basic.c", "fact_test"))) return;
    expect_eval("n", "1");
    nf = dbg_stack(S, fr, 16);
    int depth = 0;
    while (depth < nf && !strcmp(fr[depth].function, "factorial")) depth++;
    ok(depth == 5 && !strcmp(fr[5].function, "main"), "5 factorial frames above main (%d)", depth);
    dbg_bp_remove(S, fact);
    dbg_step_out(S);
    if (!expect_stop("step out of the innermost recursion", DBG_STOP_STEP, "factorial", "basic.c", 0)) return;
    expect_eval("n", "2");

    int ret = L("basic.c", "main_ret");
    dbg_run_to(S, "basic.c", ret);
    if (!expect_stop("run to cursor", DBG_STOP_STEP, "main", "basic.c", ret)) return;
    expect_eval("total & 0x3F", "23");
    dbg_get_regs(S, &r);
    uint32_t pc0 = r.pc;
    dbg_step_instruction(S, 1);
    if (expect_stop("step instruction", DBG_STOP_STEP, "main", NULL, 0)) {
        dbg_get_regs(S, &r);
        ok(r.pc == pc0 + 4, "pc advanced by one instruction (%08lx -> %08lx)", (unsigned long)pc0, (unsigned long)r.pc);
    }
    dbg_var *vt = dbg_eval(S, "total");
    ok(vt->editable && dbg_var_set(S, vt, "100"), "set total = 100 (%s)", dbg_last_error(S));
    dbg_var_free(S, &vt, 1);
    expect_eval("total", "100");
    /* the instruction just stepped over (lwz r9,12(r31)) already loaded total into r9, which
     * the rest of the line uses: change the register too */
    ok(dbg_set_reg(S, "r9", 100), "set r9 = 100 (%s)", dbg_last_error(S));
    dbg_continue(S);
    expect_exit("exit code after editing a variable and a register", 36);

    FILE *f = fopen("T:dbgtest-basic.txt", "r");
    char line[200] = "";
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    ok(!strncmp(line, "hello total=215", 15), "program output went to the console file: %s", line);

    /* run it again: fresh data, breakpoints still there */
    dbg_breakpoint all[8];
    nb = dbg_bp_list(S, all, 8);
    for (int i = 0; i < nb; i++) dbg_bp_remove(S, all[i].id);
    ok(dbg_run(S, 0), "run again (%s)", dbg_last_error(S));
    expect_exit("second run exits normally", 23);
}

static void test_crash(void)
{
    printf("== crash: NULL write reported with PC and line, then kill\n");
    if (!dbg_load(S, P("basic"), "crash", NULL, "NIL:")) { ok(0, "load basic crash: %s", dbg_last_error(S)); return; }
    dbg_set_source_path(S, P("src"));
    dbg_run(S, 0);
    dbg_event ev;
    if (!wait_event(&ev, 8000)) {
        ok(0, "crash: timed out");
        if (dbg_pause(S) && wait_event(&ev, 2000)) { printf("  (it was at %08lx %s)\n", (unsigned long)ev.pc, ev.text); print_stack(); }
        return;
    }
    ok(ev.type == DBG_EV_STOPPED && ev.reason == DBG_STOP_EXCEPTION && ev.trap_type == 0x300 && ev.fault_addr == 0 &&
       ev.function && !strcmp(ev.function, "crash") && ev.line == L("basic.c", "crash_write"),
       "crash: trap %lx fault %08lx pc %08lx in %s line %d", (unsigned long)ev.trap_type, (unsigned long)ev.fault_addr,
       (unsigned long)ev.pc, ev.function ? ev.function : "?", ev.line);
    dbg_frame fr[8];
    int nf = dbg_stack(S, fr, 8);
    ok(nf >= 2 && !strcmp(fr[1].function, "main") && fr[1].line == L("basic.c", "main_crash"), "crash stack: crash <- main:%d", nf > 1 ? fr[1].line : 0);
    ok(dbg_kill(S), "kill the crashed program");
    expect_exit("killed program exits", 20);
}

static void test_pause(void)
{
    printf("== pause and kill a running program\n");
    if (!dbg_load(S, P("basic"), "spin", NULL, "NIL:")) { ok(0, "load basic spin: %s", dbg_last_error(S)); return; }
    dbg_run(S, 0);
    dbg_event ev;
    ok(!wait_event(&ev, 400), "spinning program produces no event");
    ok(dbg_pause(S), "pause");
    if (!expect_stop("paused in the loop", DBG_STOP_PAUSE, "spin", "basic.c", 0)) return;
    dbg_continue(S);
    ok(!wait_event(&ev, 300), "continues after pause");
    ok(dbg_kill(S), "kill while running");
    expect_exit("killed while running", 20);
}

/* ------------------------------------------------------------------ pausetest.c */

/* Is Intuition usable? A helper process takes LockIBase and lets go; if the program being
 * debugged holds an Intuition (or console/layers) lock while it is stopped, the helper
 * blocks. Waits up to timeout_ms; on a timeout the program is continued (to let the helper
 * finish) and 0 is returned. */
static volatile int g_ibase_done;
static void ibase_probe(void)
{
    uint32 l = IIntuition->LockIBase(0);
    IIntuition->UnlockIBase(l);
    g_ibase_done = 1;
}

static int intuition_ok(uint32_t timeout_ms)
{
    g_ibase_done = 0;
    struct Process *p = IDOS->CreateNewProcTags(NP_Entry, ibase_probe, NP_Name, "dbgtest LockIBase probe",
                                                NP_Output, 0, NP_Input, 0, NP_Error, 0, TAG_DONE);
    if (!p) { printf("  (can't start the LockIBase probe)\n"); return 0; }
    uint32_t start = now_ms();
    while (!g_ibase_done && now_ms() - start < timeout_ms) IDOS->Delay(1);
    if (g_ibase_done) { IDOS->Delay(2); return 1; }     /* give the helper time to end */
    printf("  LockIBase blocked: continuing the program to release Intuition\n");
    if (dbg_get_state(S) == DBG_STOPPED) dbg_continue(S);
    while (!g_ibase_done) IDOS->Delay(5);
    IDOS->Delay(2);
    return 0;
}

static int in_user_code(const dbg_event *ev)
{
    return ev->type == DBG_EV_STOPPED && dbg_addr_in_program(S, ev->pc) && ev->file && strstr(ev->file, "pausetest.c");
}

/* the next STOPPED/EXITED event, or none within ms */
static int no_event(uint32_t ms) { dbg_event ev; return !wait_event(&ev, ms); }

static void signal_program(uint32_t sig)
{
    struct Task *t = dbg_task(S);
    if (t) IExec->Signal(t, sig);
}

static void test_oscall(void)
{
    printf("== pause while the program is in AmigaOS calls (console output, Delay, Wait)\n");
    ok(intuition_ok(2000), "Intuition is usable before the test");
    /* printf into a CON: window + Delay(), in a loop */
    if (!dbg_load(S, P("pausetest"), "", NULL, NULL)) { ok(0, "load pausetest: %s", dbg_last_error(S)); return; }
    dbg_set_source_path(S, P("src"));
    dbg_run(S, 0);
    ok(no_event(500), "printing program runs");
    int good = 0, deferred = 0, ibase = 0;
    const int ROUNDS = 12;
    for (int i = 0; i < ROUNDS; i++) {
        dbg_event ev;
        if (!dbg_pause(S)) { ok(0, "pause %d: %s", i, dbg_last_error(S)); break; }
        if (!wait_event(&ev, 3000)) {
            ok(0, "pause %d: no stop within 3 s (pending %d)", i, dbg_pause_pending(S));
            break;
        }
        if (in_user_code(&ev) && ev.reason == DBG_STOP_PAUSE) good++;
        else printf("  pause %d stopped outside the program: %08lx %s\n", i, (unsigned long)ev.pc, ev.text);
        if (strstr(ev.text, "AmigaOS call returned")) deferred++;
        if (intuition_ok(3000)) ibase++;
        else print_stack();
        if (dbg_get_state(S) == DBG_STOPPED) dbg_continue(S);
        no_event(100 + 37 * (uint32_t)i);
    }
    ok(good == ROUNDS, "printing program: %d of %d pauses stopped in its own code", good, ROUNDS);
    ok(ibase == ROUNDS, "printing program: Intuition usable at %d of %d pauses", ibase, ROUNDS);
    printf("  (%d of the pauses waited for an AmigaOS call to return)\n", deferred);
    ok(deferred > 0, "some pauses landed in AmigaOS calls (%d)", deferred);
    signal_program(SIGBREAKF_CTRL_E);
    expect_exit("printing program ends on CTRL-E", 7);
    cleanup();

    /* Wait() on signals: the pause stays pending */
    if (!dbg_load(S, P("pausetest"), "wait", NULL, NULL)) { ok(0, "load pausetest wait: %s", dbg_last_error(S)); return; }
    dbg_set_source_path(S, P("src"));
    dbg_run(S, 0);
    ok(no_event(500), "waiting program runs");
    ok(dbg_pause(S) && no_event(1000), "pause in Wait(): no stop");
    ok(dbg_pause_pending(S) && dbg_get_state(S) == DBG_RUNNING, "pause in Wait(): pending, still running");
    ok(intuition_ok(3000), "pause pending: Intuition usable");
    ok(dbg_pause(S) && !dbg_pause_pending(S) && dbg_get_state(S) == DBG_RUNNING, "pause again cancels it");
    ok(no_event(300), "cancelled: keeps waiting");
    signal_program(SIGBREAKF_CTRL_F);                 /* woken without a pause: goes round again */
    ok(no_event(300), "woken without a pause pending: no stop");
    for (int i = 0; i < 4; i++) {
        ok(dbg_pause(S) && no_event(300) && dbg_pause_pending(S), "pause %d in Wait(): pending", i);
        signal_program(SIGBREAKF_CTRL_F);             /* it returns to its own code: now it stops */
        if (!expect_stop("woken with a pause pending: stops", DBG_STOP_PAUSE, NULL, "pausetest.c", 0)) return;
        dbg_var *v = dbg_eval(S, "g_wakes");
        printf("  g_wakes = %s\n", v->value);
        dbg_var_free(S, &v, 1);
        ok(intuition_ok(3000), "stopped after Wait(): Intuition usable");
        dbg_continue(S);
        ok(no_event(200), "continued: waits again");
    }
    ok(dbg_pause(S) && dbg_pause_pending(S), "pause pending again");
    ok(dbg_continue(S) && !dbg_pause_pending(S) && dbg_get_state(S) == DBG_RUNNING, "Continue cancels it");
    signal_program(SIGBREAKF_CTRL_F);
    ok(no_event(300), "cancelled by Continue: woken, no stop");
    ok(dbg_pause(S) && dbg_pause_pending(S), "pause pending before the end");
    signal_program(SIGBREAKF_CTRL_E);                 /* it returns to main and ends: stops on the way */
    if (expect_stop("ending with a pause pending: stops in its code", DBG_STOP_PAUSE, NULL, "pausetest.c", 0)) dbg_continue(S);
    expect_exit("waiting program ends on CTRL-E", 7);
    /* (Stop with a pause pending isn't tried here: dbg_kill can't unwind a program that is in
     * Wait() at all, pause or not, and leaves it suspended; see docs/debugger-notes.md.) */
    ok(intuition_ok(2000), "Intuition usable after the test");
}

static void test_cplus(void)
{
    printf("== C++: demangled names, members, std::string\n");
    if (!dbg_load(S, P("cplus"), "", NULL, "NIL:")) { ok(0, "load cplus: %s", dbg_last_error(S)); return; }
    dbg_set_source_path(S, P("src"));
    int id = dbg_bp_add_function(S, "shapes::Rect::area");
    dbg_breakpoint bl[2];
    ok(dbg_bp_list(S, bl, 2) == 1 && bl[0].addr, "function breakpoint resolved (%d)", id);
    dbg_run(S, 0);
    if (!expect_stop("breakpoint in a method", DBG_STOP_BREAKPOINT, "shapes::Rect::area() const", "cplus.cpp", L("cplus.cpp", "area_body"))) return;
    expect_eval("w_ * h_", "20");
    expect_eval("this->h_", "5");
    expect_eval("label_", "\"rect\"");
    dbg_frame fr[8];
    int nf = dbg_stack(S, fr, 8);
    print_stack();
    ok(nf >= 3 && !strncmp(fr[1].function, "shapes::measure(", 16) && !strcmp(fr[2].function, "main"), "C++ stack names");
    dbg_select_frame(S, 2);
    expect_eval("s", "\"abcrect\"");
    expect_eval("r.w_", "4");
    dbg_select_frame(S, 0);
    dbg_continue(S);
    expect_exit("C++ program exits", 26);
}

static void test_asm(void)
{
    printf("== assembler: stepping through a .S function\n");
    if (!dbg_load(S, P("asmtest"), "", NULL, "NIL:")) { ok(0, "load asmtest: %s", dbg_last_error(S)); return; }
    dbg_set_source_path(S, P("src"));
    int call = L("asmtest.c", "asm_call"), first = L("asmtest_add3.S", "add3_first");
    dbg_bp_add(S, "asmtest.c", call, NULL);
    dbg_run(S, 0);
    if (!expect_stop("breakpoint before the call", DBG_STOP_BREAKPOINT, "main", "asmtest.c", call)) return;
    dbg_step_into(S);
    if (!expect_stop("step into add3", DBG_STOP_STEP, "add3", "asmtest_add3.S", first)) return;
    dbg_regs r;
    dbg_get_regs(S, &r);
    ok(r.gpr[3] == 1 && r.gpr[4] == 2 && r.gpr[5] == 3, "arguments in r3-r5: %lu %lu %lu", (unsigned long)r.gpr[3], (unsigned long)r.gpr[4], (unsigned long)r.gpr[5]);
    dbg_step_over(S);
    if (!expect_stop("asm step", DBG_STOP_STEP, "add3", "asmtest_add3.S", first + 1)) return;
    dbg_get_regs(S, &r);
    ok(r.gpr[3] == 3, "r3 = 1 + 2 = %lu", (unsigned long)r.gpr[3]);
    expect_eval("$r3", "3");
    dbg_step_over(S);
    if (!expect_stop("asm step 2", DBG_STOP_STEP, "add3", "asmtest_add3.S", first + 2)) return;
    dbg_step_over(S);
    if (!expect_stop("step back out to C", DBG_STOP_STEP, "main", "asmtest.c", call)) return;
    dbg_continue(S);
    expect_exit("asm program exits", 6);
}

static void exit_runs(const char *mode)
{
    int good = 0;
    for (int i = 0; i < 4; i++) {
        if (i == 0 || strcmp(mode, "same")) {
            if (!strcmp(mode, "fresh")) {
                dbg_destroy(S);
                S = dbg_create();
                if (g_memtrace) dbg_set_trace(S, memtrace_line, NULL);
            }
            else dbg_unload(S);
            if (!dbg_load(S, P("asmtest"), "", NULL, "NIL:")) { ok(0, "load asmtest: %s", dbg_last_error(S)); return; }
        }
        dbg_run(S, 0);
        dbg_event ev;
        if (wait_event(&ev, 4000) && ev.type == DBG_EV_EXITED && ev.exit_code == 6) { good++; continue; }
        if (g_memtrace) memtrace_dump();
        printf("  run %d didn't exit\n", i);
        {   /* who has the CPU? */
            char line[160];
            struct ExecBase *eb = (struct ExecBase *)SysBase;
            IExec->Forbid();
            for (struct Node *n = eb->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ) {
                snprintf(line, sizeof line, "  ready: %-32s pri %d state %d", n->ln_Name ? n->ln_Name : "?", n->ln_Pri, ((struct Task *)n)->tc_State);
                out("%s\n", line);
            }
            struct Task *me = IExec->FindTask(NULL);
            snprintf(line, sizeof line, "  me: pri %d", me->tc_Node.ln_Pri);
            IExec->Permit();
            out("%s\n", line);
        }
        if (dbg_get_state(S) == DBG_RUNNING && dbg_pause(S) && wait_event(&ev, 2000)) printf("  (it was at %08lx %s)\n", (unsigned long)ev.pc, ev.text);
        ok(0, "runs to the end (%s session): run %d didn't exit", mode, i);
        return;   /* don't pile up stuck processes */
    }
    ok(good == 4, "runs to the end (%s session): %d of 4", mode, good);
}

static void test_exit(void)
{
    printf("== exit: runs to the end, again and again\n");
    exit_runs("same");
    exit_runs("reload");
    exit_runs("fresh");
}

int main(int argc, char **argv)
{
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    int tracing = argc > 3 && !strcmp(argv[3], "trace");
    if (argc > 4) {                                   /* log file */
        snprintf(g_log, sizeof g_log, "%s", argv[4]);
        BPTR fh = IDOS->Open(g_log, MODE_NEWFILE);
        if (fh) IDOS->Close(fh);
    }
    tport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
    if (!treq || IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0)) { printf("no timer\n"); return 20; }
    S = dbg_create();
    if (!S) { printf("FAIL dbg_create\ndbgtest: 0 passed, 1 failed\n"); return 20; }
    if (argc > 3 && !strcmp(argv[2], "dis")) {       /* dbgtest DIR dis ADDR [N]: disassemble memory */
        uint32_t a = strtoul(argv[3], NULL, 16);
        int n = argc > 4 ? atoi(argv[4]) : 16;
        dbg_insn ins[64];
        if (n > 64) n = 64;
        n = dbg_disassemble(S, a, n, ins);
        for (int i = 0; i < n; i++) printf("%08lx %08lx %-32s %s\n", (unsigned long)ins[i].addr, (unsigned long)ins[i].word, ins[i].text, ins[i].symbol ? ins[i].symbol : "");
        dbg_destroy(S);
        return 0;
    }
    if (tracing) dbg_set_trace(S, trace_line, NULL);
    if (argc > 3 && !strcmp(argv[3], "memtrace")) { g_memtrace = 1; dbg_set_trace(S, memtrace_line, NULL); }

    struct { const char *name; void (*fn)(void); } tests[] = {
        { "exit", test_exit }, { "basic", test_basic }, { "crash", test_crash }, { "pause", test_pause }, { "oscall", test_oscall }, { "cplus", test_cplus }, { "asm", test_asm },
    };
    const char *only = argc > 2 && strcmp(argv[2], "all") ? argv[2] : NULL;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (only && strcmp(only, tests[i].name)) continue;
        tests[i].fn();
        cleanup();
    }
    dbg_destroy(S);
    printf("dbgtest: %d passed, %d failed\n", g_pass, g_fail);
    IExec->CloseDevice((struct IORequest *)treq);
    IExec->FreeSysObject(ASOT_IOREQUEST, treq);
    IExec->FreeSysObject(ASOT_PORT, tport);
    return g_fail ? 5 : 0;
}
