/*
 * os4_session.c — the AmigaOS 4 runtime of the debugger: implements dbg.h on top of the
 * exec debug interface. See docs/DESIGN.md ("Debugger") and docs/debugger-notes.md for
 * the behaviour this relies on, which was established on the QEMU sam460ex guest:
 *
 *  - The debug hook only copies the exception context and returns 1 (suspend). It must
 *    not Signal()/Cause()/PutMsg(): any of those from the hook leaves the debuggee unable
 *    to Wait() after it is restarted (it spins in the kernel at its next DOS call). The
 *    IDE side therefore polls a counter the hook bumps, on a short timer while running.
 *  - RestartTask() resumes a hook-suspended task at its context's ip (the trap address);
 *    WriteTaskContext() changes are honoured.
 *  - Code pages are EXECUTE|READ_ONLY (0x2C0). Breakpoints are written by switching only
 *    the access bits to READ_WRITE (keeping EXECUTE and the cache bits) in supervisor
 *    state, then restoring them; SetMemoryAttrs(addr, n, MEMATTRF_READ_WRITE) as such
 *    drops EXECUTE from the whole page and hangs the machine.
 *  - The exit code comes from NP_NotifyOnDeathMessage. Kill restores the context captured
 *    at the program's ELF entry point with ip = its return address into dos.library and
 *    r3 = the exit code, so the process unwinds through DOS's normal exit path.
 */
#undef __USE_INLINE__           /* explicit IExec->/IDOS-> calls */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/elf.h>
#include <exec/exectags.h>
#include <exec/interrupts.h>
#include <exec/debug.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "dbg_int.h"
#include "os4_tools.h"   /* --- debugtools --- */
#include "hotpatch.h"     /* --- hotpatch */

#define POLL_MS        10       /* hit polling while the program runs */
#define QUICK_WAIT_MS  40       /* how long an internal resume waits for its next hit */
#define KILL_WAIT_MS   2000
#define DEFAULT_STACK  (256 * 1024)
#define EXIT_KILLED    20       /* RETURN_FAIL */
#define MAX_FRAMES     64
#define SETTLE_MS      10       /* after a hook suspension, before touching the task */

struct KernelDebugMessage { uint32_t type; union { struct ExceptionContext *context; struct Library *library; } message; };

/* trap kinds (bit flags: one address can serve several purposes) */
enum {
    TK_USER    = 1 << 0,        /* enabled user breakpoint(s) */
    TK_STEPOFF = 1 << 1,        /* successors of an instruction we're stepping off */
    TK_LINE    = 1 << 2,        /* step over/into: another line of the current function */
    TK_RET     = 1 << 3,        /* return address (step over/out, finish) */
    TK_FNENTRY = 1 << 4,        /* step into: start of a function with line info */
    TK_RUNTO   = 1 << 5,
    TK_STEP    = 1 << 6,        /* instruction step successor */
    TK_PENTRY  = 1 << 7,        /* the program's ELF entry point (kill context) */
    TK_MAIN    = 1 << 8         /* stop at main */
};
#define TK_TEMP (TK_STEPOFF | TK_LINE | TK_RET | TK_FNENTRY | TK_RUNTO | TK_STEP)

typedef struct {
    uint32_t addr, orig;
    uint32_t kinds;
    uint8_t inserted, suppressed;
} trap;

typedef struct {
    dbg_breakpoint pub;
    char *file, *function, *condition;
    uint32_t req_addr;            /* dbg_bp_add_address */
} bpoint;

enum { OP_NONE, OP_CONT, OP_STEPI, OP_INTO, OP_OVER, OP_OUT, OP_RUNTO };

struct dbg_session {
    dbg_state state;
    char err[256];
    char *path, *args, *cwd, *console, *srcpath;
    dbg_info *di;
    BPTR seg;
    int ran;                    /* the loaded seglist has been run (data is dirty) */

    struct DebugIFace *idebug;
    struct MMUIFace *immu;

    /* process */
    struct Process *proc;
    struct Hook hook;
    struct DeathMessage dm;
    int just_exited;            /* a process ended recently: give its cleanup time first */
    struct MsgPort *dport;
    int con_is_file;
    char *run_args;             /* "args\n", read by the program while it runs */
    int32_t run_arglen;
    BPTR run_cwd, run_home;     /* handed to the launcher, which owns them from then on */

    /* written by the hook (debuggee context) */
    volatile uint32_t exc_count;
    volatile int launcher_ready;
    uint32_t last_pid;
    uint32_t stall_ip; int stall;   /* ready at the same address poll after poll: see kick() */               /* the previous run's process, until DOS has finished with it */     /* the launcher has hooked itself and waits for the go signal */
    volatile uint32_t remtask;
    struct ExceptionContext exc;
    uint32_t seen;

    /* timers: async poll + sync short delays */
    struct MsgPort *tport, *sport;
    struct TimeRequest *treq, *sreq;
    int timer_busy;

    /* current stop */
    struct ExceptionContext ctx;
    dbg_regs regs;
    frame_info frames[MAX_FRAMES];
    int nframes, sel;
    arena stop_mem;             /* strings for frames etc., reset at every stop */
    unsigned generation;
    varstore *vs;

    /* breakpoints and traps */
    vec bps;                    /* bpoint */
    int next_bp;
    vec traps;                  /* trap */
    uint32_t entry_addr, main_addr;
    struct ExceptionContext entry_ctx;
    int have_entry, stop_at_main;

    /* operation in progress */
    int op;
    int over_calls;
    int stepping_off;           /* a TK_STEPOFF trap set is planted */
    int quick;                  /* the last resume expects a quick hit (step / step-off) */
    uint32_t stepoff_pc;
    uint32_t start_cfa, ret_sp;
    int start_file, start_line;
    uint32_t fn_lo, fn_hi;

    vec events;                 /* dbg_event queue */
    size_t ev_head;
    char *resolved[32];         /* dbg_resolve_source cache (owned) */
    int nresolved;
    void (*trace_fn)(void *ud, const char *line);
    void *trace_ud;
    /* --- debugtools --- */
    void *dt;                   /* os4_tools.c state (resource tracking, profiler) */
    volatile int returned;      /* RunCommand has returned: DOS is cleaning the process up */
    int returned_polls;         /* polls since then (debugtools: see dbg_poll) */
    /* --- hotpatch: Edit and Continue (dbg_hotpatch) --- */
    hp_elf *hp_exe;             /* the program's ELF, read on the first patch */
    vec hp_mem;                 /* APTR: memory holding this run's patches */
    vec hp_funcs;               /* hp_entry: code of every patched function, for later patches */
    int hp_count;               /* patches applied to the loaded program */
};

typedef struct { char *source, *name; uint32_t addr, size; } hp_entry;   /* --- hotpatch */
static void hp_release(dbg_session *s);

/* --- debugtools: accessors for os4_tools.c --- */
struct Task *dbg__proc(dbg_session *s) { return (struct Task *)s->proc; }
struct DebugIFace *dbg__idebug(dbg_session *s) { return s->idebug; }
dbg_info *dbg__info(dbg_session *s) { return s->di; }
void **dbg__dt(dbg_session *s) { return &s->dt; }
volatile uint32_t *dbg__exc_count(dbg_session *s) { return &s->exc_count; }
/* --- end debugtools --- */


/* ======================================================================== utilities */

static char *sdup(const char *s) { if (!s) return NULL; char *d = malloc(strlen(s) + 1); if (d) strcpy(d, s); return d; }

static int set_err(dbg_session *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->err, sizeof s->err, fmt, ap);
    va_end(ap);
    return 0;
}

static void trace(dbg_session *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void trace(dbg_session *s, const char *fmt, ...)
{
    if (!s->trace_fn) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s->trace_fn(s->trace_ud, buf);
}

void dbg_set_trace(dbg_session *s, void (*fn)(void *ud, const char *line), void *ud)
{
    s->trace_fn = fn;
    s->trace_ud = ud;
}

static void push_event(dbg_session *s, const dbg_event *ev)
{
    dbg_event *e = vec_push(&s->events);
    if (e) *e = *ev;
}

static void message(dbg_session *s, const char *fmt, ...)
{
    dbg_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = DBG_EV_MESSAGE;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev.text, sizeof ev.text, fmt, ap);
    va_end(ap);
    push_event(s, &ev);
}

/* ======================================================================== timers */

static void arm_timer(dbg_session *s)
{
    if (s->timer_busy || !s->treq) return;
    s->treq->Request.io_Command = TR_ADDREQUEST;
    s->treq->Time.Seconds = 0;
    s->treq->Time.Microseconds = POLL_MS * 1000;
    IExec->SendIO((struct IORequest *)s->treq);
    s->timer_busy = 1;
}

static void reap_timer(dbg_session *s, int abort)
{
    if (!s->timer_busy) return;
    if (abort && !IExec->CheckIO((struct IORequest *)s->treq)) IExec->AbortIO((struct IORequest *)s->treq);
    if (abort || IExec->CheckIO((struct IORequest *)s->treq)) {
        IExec->WaitIO((struct IORequest *)s->treq);
        s->timer_busy = 0;
    }
}

static void delay_ms(dbg_session *s, uint32_t ms)
{
    s->sreq->Request.io_Command = TR_ADDREQUEST;
    s->sreq->Time.Seconds = ms / 1000;
    s->sreq->Time.Microseconds = (ms % 1000) * 1000;
    IExec->DoIO((struct IORequest *)s->sreq);
}

/* ======================================================================== debug hook */

static uint32_t debug_hook(struct Hook *h, struct Task *task, struct KernelDebugMessage *m)
{
    dbg_session *s = h->h_Data;
    if (task != (struct Task *)s->proc) return 0;   /* children inherit the hook: leave them alone */
    switch (m->type) {
    case DBHMT_EXCEPTION:
        s->exc = *m->message.context;
        s->exc_count++;
        return 1;                                    /* suspend; the IDE polls exc_count */
    case DBHMT_REMTASK:
        s->remtask = 1;
        return 0;
    default:
        return 0;
    }
}

/* ======================================================================== memory */

static uint32_t page_attrs(dbg_session *s, uint32_t addr)
{
    APTR st = IExec->SuperState();
    uint32_t a = s->immu->GetMemoryAttrs((APTR)addr, 0);
    if (st) IExec->UserState(st);
    return a;
}

static int user_readable(uint32_t attrs)
{
    return !(attrs & MEMATTRF_NOT_MAPPED) && (attrs & MEMATTRF_RW_MASK) != MEMATTRF_SUPER_RW;
}

static trap *find_trap(dbg_session *s, uint32_t addr)
{
    for (size_t i = 0; i < s->traps.n; i++) {
        trap *t = &VEC_AT(&s->traps, trap, i);
        if (t->addr == addr) return t;
    }
    return NULL;
}

/* Reads target memory page by page; unreadable bytes stop it. Trap words read back as
 * the original instructions. */
static int mem_read(dbg_session *s, uint32_t addr, void *buf, uint32_t len)
{
    uint32_t done = 0;
    while (done < len) {
        uint32_t a = addr + done;
        if (a < 0x1000) break;                        /* never touch page 0 */
        uint32_t in_page = 4096 - (a & 4095), n = len - done < in_page ? len - done : in_page;
        if (!user_readable(page_attrs(s, a))) break;
        memcpy((uint8_t *)buf + done, (const void *)a, n);
        done += n;
    }
    for (size_t i = 0; i < s->traps.n; i++) {
        trap *t = &VEC_AT(&s->traps, trap, i);
        if (!t->inserted || t->addr + 4 <= addr || t->addr >= addr + done) continue;
        uint8_t w[4];
        put_be32(w, t->orig);
        for (int k = 0; k < 4; k++) {
            uint32_t a = t->addr + k;
            if (a >= addr && a < addr + done) ((uint8_t *)buf)[a - addr] = w[k];
        }
    }
    return (int)done;
}

/* Writes one word of program code (attributes switched to RW around the store). Only
 * ever used inside the debuggee's own sections. */
static int code_write(dbg_session *s, uint32_t addr, uint32_t word)
{
    if ((addr & 3) || !info_in_sections(s->di, addr, 4)) return 0;
    APTR st = IExec->SuperState();
    uint32_t old = s->immu->GetMemoryAttrs((APTR)addr, 0);
    int ok = !(old & MEMATTRF_NOT_MAPPED);
    if (ok) {
        uint32_t rw = ((old & ~(uint32_t)MEMATTRF_RW_MASK) | MEMATTRF_READ_WRITE) &
                    ~(uint32_t)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED | MEMATTRF_NOT_MAPPED);
        s->immu->SetMemoryAttrs((APTR)addr, 4, rw);
        *(volatile uint32_t *)addr = word;
        s->immu->SetMemoryAttrs((APTR)addr, 4, old & ~(uint32_t)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED));
    }
    if (st) IExec->UserState(st);
    IExec->CacheClearE((APTR)addr, 4, CACRF_ClearI | CACRF_ClearD);
    return ok;
}

static int mem_write(dbg_session *s, uint32_t addr, const void *buf, uint32_t len)
{
    uint32_t done = 0;
    while (done < len) {
        uint32_t a = addr + done;
        if (a < 0x1000) break;
        trap *t = find_trap(s, a & ~3u);
        if (info_in_program(s->di, a) || (t && t->inserted)) {
            /* code: word at a time through the attribute dance; keep traps in place */
            uint32_t w = a & ~3u, cur;
            uint8_t b[4];
            if (mem_read(s, w, b, 4) != 4) break;
            uint32_t k = a - w, n = 4 - k < len - done ? 4 - k : len - done;
            memcpy(b + k, (const uint8_t *)buf + done, n);
            cur = be32(b);
            if (t && t->inserted) t->orig = cur;
            else if (!code_write(s, w, cur)) break;
            done += n;
            continue;
        }
        uint32_t attrs = page_attrs(s, a);
        if ((attrs & MEMATTRF_NOT_MAPPED) || (attrs & MEMATTRF_RW_MASK) != MEMATTRF_SUPER_RW_USER_RW) break;
        uint32_t in_page = 4096 - (a & 4095), n = len - done < in_page ? len - done : in_page;
        memcpy((void *)a, (const uint8_t *)buf + done, n);
        IExec->CacheClearE((APTR)a, n, CACRF_ClearD);
        done += n;
    }
    return (int)done;
}

/* dbg_mem callbacks for frames/evaluation */
static int cb_read(void *ud, uint32_t addr, void *buf, uint32_t len) { return mem_read(ud, addr, buf, len); }
static int cb_write(void *ud, uint32_t addr, const void *buf, uint32_t len) { return mem_write(ud, addr, buf, len); }

static void regs_from_ctx(dbg_session *s)
{
    struct ExceptionContext *c = &s->ctx;
    dbg_regs *r = &s->regs;
    memcpy(r->gpr, c->gpr, sizeof r->gpr);
    r->pc = c->ip; r->msr = c->msr; r->cr = c->cr; r->xer = c->xer; r->ctr = c->ctr; r->lr = c->lr;
    r->dsisr = c->dsisr; r->dar = c->dar;
    memcpy(r->fpr, c->fpr, sizeof r->fpr);
    r->fpscr = c->fpscr;
}

static int write_ctx(dbg_session *s, uint32_t flags)
{
    if (!s->proc || s->state != DBG_STOPPED) return 0;
    s->idebug->WriteTaskContext((struct Task *)s->proc, &s->ctx, flags);
    regs_from_ctx(s);
    return 1;
}

static int cb_set_reg(void *ud, int reg, const void *val, int size)
{
    dbg_session *s = ud;
    const uint8_t *b = val;
    if (reg >= REG_GPR && reg < REG_GPR + 32 && size <= 4) {
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v = v << 8 | b[i];
        if (size < 4 && (b[0] & 0x80) && size) v |= ~0u << (size * 8);   /* sign extend; harmless for unsigned */
        s->ctx.gpr[reg] = v;
        return write_ctx(s, RTCF_GENERAL);
    }
    if (reg >= REG_FPR && reg < REG_FPR + 32) {
        double d;
        if (size == 4) { uint32_t w = be32(b); float f; memcpy(&f, &w, 4); d = f; }
        else if (size == 8) { uint64_t u = (uint64_t)be32(b) << 32 | be32(b + 4); memcpy(&d, &u, 8); }
        else return 0;
        s->ctx.fpr[reg - REG_FPR] = d;
        return write_ctx(s, RTCF_FPU);
    }
    return 0;
}

static dbg_mem session_mem(dbg_session *s)
{
    dbg_mem m = { s, cb_read, cb_write, cb_set_reg };
    return m;
}

/* ======================================================================== traps */

static trap *get_trap(dbg_session *s, uint32_t addr)
{
    trap *t = find_trap(s, addr);
    if (t) return t;
    if (!info_in_program(s->di, addr) || (addr & 3)) return NULL;   /* only our own code */
    t = vec_push(&s->traps);
    if (t) t->addr = addr;
    return t;
}

static void add_kind(dbg_session *s, uint32_t addr, uint32_t kind)
{
    trap *t = get_trap(s, addr);
    if (t) t->kinds |= kind;
}

static void clear_kinds(dbg_session *s, uint32_t kinds)
{
    for (size_t i = 0; i < s->traps.n; i++) VEC_AT(&s->traps, trap, i).kinds &= ~kinds;
}

/* Brings memory in line with the wanted traps; drops entries nobody needs. */
static void sync_traps(dbg_session *s)
{
    size_t w = 0;
    for (size_t i = 0; i < s->traps.n; i++) {
        trap *t = &VEC_AT(&s->traps, trap, i);
        int want = t->kinds && !t->suppressed && s->seg;
        if (want && !t->inserted) {
            uint32_t cur;
            if (mem_read32(&(dbg_mem){ s, cb_read, NULL, NULL }, t->addr, &cur)) {
                t->orig = cur;
                if (code_write(s, t->addr, PPC_TRAP_INSN)) t->inserted = 1;
                trace(s, "  trap +%08lx (orig %08lx, kinds %lx)%s", (unsigned long)t->addr, (unsigned long)cur, (unsigned long)t->kinds, t->inserted ? "" : " FAILED");
            }
        } else if (!want && t->inserted) {
            if (!s->seg || code_write(s, t->addr, t->orig)) t->inserted = 0;
            trace(s, "  trap -%08lx (orig %08lx)", (unsigned long)t->addr, (unsigned long)t->orig);
        }
        if (t->kinds || t->inserted) VEC_AT(&s->traps, trap, w++) = *t;
    }
    s->traps.n = w;
}

static void remove_all_traps(dbg_session *s)
{
    for (size_t i = 0; i < s->traps.n; i++) VEC_AT(&s->traps, trap, i).kinds = 0;
    sync_traps(s);
    s->traps.n = 0;
}

static void refresh_user_traps(dbg_session *s)
{
    clear_kinds(s, TK_USER);
    for (size_t i = 0; i < s->bps.n; i++) {
        bpoint *b = &VEC_AT(&s->bps, bpoint, i);
        if (b->pub.enabled && b->pub.addr) add_kind(s, b->pub.addr, TK_USER);
    }
}

/* ======================================================================== breakpoints */

static void resolve_bp(dbg_session *s, bpoint *b)
{
    b->pub.addr = 0;
    b->pub.actual_line = 0;
    if (!s->di) return;
    if (b->file) {
        b->pub.addr = info_line_addr(s->di, b->file, b->pub.line, &b->pub.actual_line);
    } else if (b->function) {
        const dfunc *f = info_func_named(s->di, b->function);
        if (f) {
            b->pub.addr = info_l2r(s->di, f->body);
            const dline *l = info_line_at(s->di, b->pub.addr);
            b->pub.actual_line = l ? l->line : 0;
            if (l) b->pub.file = info_file_name(s->di, l->file);
        } else {
            uint32_t a = info_symbol_addr(s->di, b->function);
            if (a && info_in_program(s->di, a)) b->pub.addr = a;
        }
    } else if (b->req_addr && info_in_program(s->di, b->req_addr)) {
        b->pub.addr = b->req_addr;
        const dline *l = info_line_at(s->di, b->req_addr);
        b->pub.actual_line = l ? l->line : 0;
    }
}

static void resolve_all_bps(dbg_session *s)
{
    for (size_t i = 0; i < s->bps.n; i++) resolve_bp(s, &VEC_AT(&s->bps, bpoint, i));
    refresh_user_traps(s);
}

static bpoint *find_bp(dbg_session *s, int id)
{
    for (size_t i = 0; i < s->bps.n; i++)
        if (VEC_AT(&s->bps, bpoint, i).pub.id == id) return &VEC_AT(&s->bps, bpoint, i);
    return NULL;
}

static int bp_new(dbg_session *s, bpoint *proto)
{
    bpoint *b = vec_push(&s->bps);
    if (!b) return -1;
    *b = *proto;
    b->pub.id = ++s->next_bp;
    b->pub.enabled = 1;
    b->pub.file = b->file;
    b->pub.condition = b->condition;
    resolve_bp(s, b);
    if (s->state == DBG_LOADED || s->state == DBG_STOPPED || s->state == DBG_EXITED) {
        refresh_user_traps(s);
        sync_traps(s);
    } else if (s->state == DBG_RUNNING) {
        /* traps can't be written while it runs: pause, plant, resume */
        refresh_user_traps(s);
        IExec->SuspendTask((struct Task *)s->proc, 0);
        if (s->exc_count == s->seen) { sync_traps(s); IExec->RestartTask((struct Task *)s->proc, 0); }
        else sync_traps(s);                       /* a hit is pending: dbg_poll handles it */
    }
    return b->pub.id;
}

int dbg_bp_add(dbg_session *s, const char *file, int line, const char *condition)
{
    bpoint b;
    memset(&b, 0, sizeof b);
    b.file = sdup(file);
    b.condition = condition && *condition ? sdup(condition) : NULL;
    b.pub.line = line;
    return bp_new(s, &b);
}

int dbg_bp_add_function(dbg_session *s, const char *function)
{
    bpoint b;
    memset(&b, 0, sizeof b);
    b.function = sdup(function);
    return bp_new(s, &b);
}

int dbg_bp_add_address(dbg_session *s, uint32_t addr)
{
    bpoint b;
    memset(&b, 0, sizeof b);
    b.req_addr = addr;
    return bp_new(s, &b);
}

static void bp_changed(dbg_session *s)
{
    refresh_user_traps(s);
    if (s->state == DBG_RUNNING) {
        IExec->SuspendTask((struct Task *)s->proc, 0);
        sync_traps(s);
        if (s->exc_count == s->seen) IExec->RestartTask((struct Task *)s->proc, 0);
    } else sync_traps(s);
}

int dbg_bp_remove(dbg_session *s, int id)
{
    for (size_t i = 0; i < s->bps.n; i++) {
        bpoint *b = &VEC_AT(&s->bps, bpoint, i);
        if (b->pub.id != id) continue;
        free(b->file); free(b->function); free(b->condition);
        memmove(b, b + 1, (s->bps.n - i - 1) * sizeof(bpoint));
        s->bps.n--;
        bp_changed(s);
        return 1;
    }
    return 0;
}

int dbg_bp_enable(dbg_session *s, int id, int enabled)
{
    bpoint *b = find_bp(s, id);
    if (!b) return 0;
    b->pub.enabled = enabled != 0;
    bp_changed(s);
    return 1;
}

int dbg_bp_list(dbg_session *s, dbg_breakpoint *out, int max)
{
    int n = 0;
    for (size_t i = 0; i < s->bps.n && n < max; i++) out[n++] = VEC_AT(&s->bps, bpoint, i).pub;
    return n;
}

/* ======================================================================== session */

dbg_session *dbg_create(void)
{
    dbg_session *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    vec_init(&s->bps, sizeof(bpoint));
    vec_init(&s->traps, sizeof(trap));
    vec_init(&s->events, sizeof(dbg_event));
    vec_init(&s->hp_mem, sizeof(APTR)); vec_init(&s->hp_funcs, sizeof(hp_entry));   /* --- hotpatch */
    s->idebug = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, NULL);
    s->immu = (struct MMUIFace *)IExec->GetInterface((struct Library *)SysBase, "mmu", 1, NULL);
    s->dport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    s->tport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    s->sport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (s->tport) s->treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, s->tport, TAG_DONE);
    if (s->sport) s->sreq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, s->sport, TAG_DONE);
    int timer_ok = s->treq && s->sreq && !IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)s->treq, 0);
    if (timer_ok) {
        *s->sreq = *s->treq;                        /* share the unit, own reply port */
        s->sreq->Request.io_Message.mn_ReplyPort = s->sport;
    }
    s->vs = varstore_new();
    if (!s->idebug || !s->immu || !s->dport || !timer_ok || !s->vs) {
        if (timer_ok) IExec->CloseDevice((struct IORequest *)s->treq);
        else if (s->treq) s->treq->Request.io_Device = NULL;
        s->treq = NULL;
        dbg_destroy(s);
        return NULL;
    }
    s->hook.h_Entry = (HOOKFUNC)debug_hook;
    s->hook.h_Data = s;
    s->state = DBG_NONE;
    return s;
}


void dbg_destroy(dbg_session *s)
{
    if (!s) return;
    if (s->di || s->proc) dbg_unload(s);
    dt_stop(s);                                      /* --- debugtools --- */
    dt_free(s);
    for (size_t i = 0; i < s->bps.n; i++) {
        bpoint *b = &VEC_AT(&s->bps, bpoint, i);
        free(b->file); free(b->function); free(b->condition);
    }
    vec_free(&s->bps); vec_free(&s->traps); vec_free(&s->events);
    hp_release(s);                                          /* --- hotpatch */
    hp_elf_free(s->hp_exe); s->hp_exe = NULL;
    vec_free(&s->hp_mem); vec_free(&s->hp_funcs);
    for (int i = 0; i < s->nresolved; i++) free(s->resolved[i]);
    free(s->path); free(s->args); free(s->cwd); free(s->console); free(s->srcpath); free(s->run_args);
    reap_timer(s, 1);
    if (s->treq) { IExec->CloseDevice((struct IORequest *)s->treq); IExec->FreeSysObject(ASOT_IOREQUEST, s->treq); }
    if (s->sreq) IExec->FreeSysObject(ASOT_IOREQUEST, s->sreq);
    if (s->tport) IExec->FreeSysObject(ASOT_PORT, s->tport);
    if (s->sport) IExec->FreeSysObject(ASOT_PORT, s->sport);
    if (s->dport) IExec->FreeSysObject(ASOT_PORT, s->dport);
    if (s->idebug) IExec->DropInterface((struct Interface *)s->idebug);
    if (s->immu) IExec->DropInterface((struct Interface *)s->immu);
    varstore_free(s->vs);
    arena_free(&s->stop_mem);
    free(s);
}

/* Only the poll timer. The death port is deliberately left out: being woken the moment the
 * DeathMessage arrives lets the IDE start the next run while the old process is still in
 * DOS's cleanup (which runs at a low priority), and the next process's exit then livelocks
 * in DOS. The exit is noticed on the next poll instead (see docs/debugger-notes.md). */
uint32_t dbg_signal_mask(dbg_session *s)
{
    return 1UL << s->tport->mp_SigBit;
}

dbg_state dbg_get_state(dbg_session *s) { return s->state; }
const char *dbg_last_error(dbg_session *s) { return s->err; }

/* Loads the seglist and maps every allocated section to its load address. */
static int load_seg(dbg_session *s)
{
    s->seg = IDOS->LoadSeg(s->path);
    if (!s->seg) return set_err(s, "LoadSeg(%s) failed (error %ld)", s->path, (long)IDOS->IoErr());
    Elf32_Handle eh = NULL;
    IDOS->GetSegListInfoTags(s->seg, GSLI_ElfHandle, &eh, TAG_DONE);
    if (!eh) { IDOS->UnLoadSeg(s->seg); s->seg = 0; return set_err(s, "%s is not an ELF executable", s->path); }
    eh = IElf->OpenElfTags(OET_ElfHandle, eh, TAG_DONE);
    dreloc rel[64];
    int n = 0;
    for (int i = 0; i < s->di->elf.nsect && n < 64; i++) {
        const elf_section *sec = &s->di->elf.sect[i];
        if (!(sec->flags & ELF_SHF_ALLOC) || !sec->size) continue;
        APTR p = eh ? IElf->GetSectionTags(eh, GST_SectionIndex, i, GST_Load, FALSE, TAG_DONE) : NULL;
        if (!p) continue;
        rel[n].link = sec->addr; rel[n].size = sec->size; rel[n].run = (uint32_t)p;
        n++;
    }
    if (eh) IElf->CloseElfTags(eh, CET_ReClose, TRUE, TAG_DONE);
    if (!n) { IDOS->UnLoadSeg(s->seg); s->seg = 0; return set_err(s, "can't find the loaded sections of %s", s->path); }
    info_set_reloc(s->di, rel, n);
    s->ran = 0;
    s->entry_addr = info_l2r(s->di, s->di->elf.entry);
    const dfunc *m = info_func_named(s->di, "main");
    s->main_addr = m ? info_l2r(s->di, m->body) : 0;
    return 1;
}

static void unload_seg(dbg_session *s)
{
    remove_all_traps(s);
    if (s->seg) { IDOS->UnLoadSeg(s->seg); s->seg = 0; }
    hp_release(s);                                          /* --- hotpatch */
    if (s->di) info_set_reloc(s->di, NULL, 0);
}

int dbg_load(dbg_session *s, const char *path, const char *args, const char *cwd, const char *console)
{
    if (s->di) dbg_unload(s);
    s->err[0] = 0;
    free(s->path); free(s->args); free(s->cwd); free(s->console);
    s->path = sdup(path); s->args = sdup(args ? args : ""); s->cwd = sdup(cwd); s->console = sdup(console);
    char err[128];
    s->di = info_load(path, err, sizeof err);
    if (!s->di) { set_err(s, "%s", err); return 0; }
    if (s->di->err[0]) message(s, "%s: %s", path, s->di->err);
    if (!load_seg(s)) { info_free(s->di); s->di = NULL; return 0; }
    s->state = DBG_LOADED;
    resolve_all_bps(s);
    sync_traps(s);
    return 1;
}

static void wait_death(dbg_session *s, uint32_t ms);
static void kill_now(dbg_session *s);

void dbg_unload(dbg_session *s)
{
    if (s->proc) kill_now(s);
    dt_stop(s);                                      /* --- debugtools --- */
    unload_seg(s);
    if (s->di) { info_free(s->di); s->di = NULL; }
    for (size_t i = 0; i < s->bps.n; i++) {
        bpoint *b = &VEC_AT(&s->bps, bpoint, i);
        b->pub.addr = 0; b->pub.actual_line = 0;
        b->pub.file = b->file;
    }
    arena_free(&s->stop_mem);
    s->nframes = 0;
    s->state = DBG_NONE;
}

void dbg_set_source_path(dbg_session *s, const char *paths)
{
    free(s->srcpath);
    s->srcpath = sdup(paths);
}

static int file_exists(const char *name)
{
    struct Process *me = (struct Process *)IExec->FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;                    /* no "insert volume" requesters */
    BPTR l = IDOS->Lock(name, SHARED_LOCK);
    me->pr_WindowPtr = old;
    if (!l) return 0;
    IDOS->UnLock(l);
    return 1;
}

static const char *remember(dbg_session *s, const char *name)
{
    for (int i = 0; i < s->nresolved; i++) if (!strcmp(s->resolved[i], name)) return s->resolved[i];
    if (s->nresolved == 32) { free(s->resolved[0]); memmove(s->resolved, s->resolved + 1, 31 * sizeof(char *)); s->nresolved--; }
    s->resolved[s->nresolved] = sdup(name);
    return s->resolved[s->nresolved++];
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t l = strlen(dir);
    while (*name == '.' && name[1] == '/') name += 2;
    snprintf(out, n, "%s%s%s", dir, l && dir[l - 1] != '/' && dir[l - 1] != ':' ? "/" : "", name);
}

const char *dbg_resolve_source(dbg_session *s, const char *stabs_file)
{
    if (!stabs_file || !*stabs_file) return NULL;
    char buf[512];
    if (strchr(stabs_file, ':') && file_exists(stabs_file)) return remember(s, stabs_file);
    if (s->srcpath) {
        const char *p = s->srcpath;
        while (*p) {
            const char *e = strchr(p, ';');
            size_t l = e ? (size_t)(e - p) : strlen(p);
            char dir[256];
            if (l && l < sizeof dir) {
                memcpy(dir, p, l); dir[l] = 0;
                if (stabs_file[0] != '/') { join(buf, sizeof buf, dir, stabs_file); if (file_exists(buf)) return remember(s, buf); }
                join(buf, sizeof buf, dir, path_base(stabs_file));
                if (file_exists(buf)) return remember(s, buf);
            }
            if (!e) break;
            p = e + 1;
        }
    }
    /* compilation directory (natively built programs have Amiga paths here) */
    if (s->di) {
        for (size_t i = 0; i < s->di->files.n; i++) {
            const dfile *f = &VEC_AT(&s->di->files, dfile, i);
            if (strcmp(f->name, stabs_file)) continue;
            const dcu *cu = f->cu >= 0 ? &VEC_AT(&s->di->cus, dcu, f->cu) : NULL;
            if (cu && cu->dir && strchr(cu->dir, ':')) {
                join(buf, sizeof buf, cu->dir, stabs_file);
                if (file_exists(buf)) return remember(s, buf);
            }
            break;
        }
    }
    if (s->path && stabs_file[0] != '/') {           /* next to the program */
        char dir[256];
        snprintf(dir, sizeof dir, "%s", s->path);
        char *b = (char *)path_base(dir);
        *b = 0;
        join(buf, sizeof buf, dir, path_base(stabs_file));
        if (dir[0] && file_exists(buf)) return remember(s, buf);
    }
    return NULL;
}

/* ======================================================================== stops */

static void describe(dbg_session *s, uint32_t pc, const char **file, int *line, const char **func)
{
    *file = NULL; *line = 0; *func = NULL;
    if (!s->di) return;
    const dline *l = info_line_at(s->di, pc);
    if (l) { *file = info_file_name(s->di, l->file); *line = l->line; }
    const dfunc *f = info_func_at(s->di, pc);
    if (f) *func = f->display;
    else {
        char sym[256];
        if (info_symbolize(s->di, pc, sym, sizeof sym)) *func = arena_strdup(&s->stop_mem, sym);
    }
}

static const char *trap_name(uint32_t t)
{
    switch (t) {
    case 0x200: return "machine check";
    case 0x300: return "DSI (bad data access)";
    case 0x400: return "ISI (bad instruction fetch)";
    case 0x600: return "alignment";
    case 0x700: return "program (illegal instruction or trap)";
    case 0x800: return "FPU unavailable";
    case 0xd00: return "trace";
    default: return "exception";
    }
}

static void compute_frames(dbg_session *s)
{
    dbg_mem m = session_mem(s);
    s->nframes = frame_walk(s->di, &m, s->ctx.ip, s->ctx.gpr[1], s->ctx.lr, s->frames, MAX_FRAMES);
    s->sel = 0;
}

/* Context of a task we suspended ourselves (pause). Stops reported by the hook use the
 * hook's own copy, which is the complete exception context. */
static void read_context(dbg_session *s)
{
    memset(&s->ctx, 0, sizeof s->ctx);
    s->idebug->ReadTaskContext((struct Task *)s->proc, &s->ctx, RTCF_INFO | RTCF_SPECIAL | RTCF_STATE | RTCF_GENERAL | RTCF_FPU);
    regs_from_ctx(s);
}

/* The program is suspended for the user: clean up temporary traps and report. */
static void report_stop(dbg_session *s, dbg_stop_reason reason, int bp_id)
{
    trace(s, "stop reason %d bp %d pc %08lx", reason, bp_id, (unsigned long)s->ctx.ip);
    arena_free(&s->stop_mem);
    clear_kinds(s, TK_TEMP);
    for (size_t i = 0; i < s->traps.n; i++) VEC_AT(&s->traps, trap, i).suppressed = 0;
    s->stepping_off = 0;
    s->op = OP_NONE;
    sync_traps(s);
    reap_timer(s, 1);
    s->state = DBG_STOPPED;
    s->generation++;
    compute_frames(s);

    dbg_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = DBG_EV_STOPPED;
    ev.reason = reason;
    ev.bp_id = bp_id;
    ev.pc = s->ctx.ip;
    ev.trap_type = s->ctx.Traptype;
    ev.fault_addr = s->ctx.dar;
    describe(s, ev.pc, &ev.file, &ev.line, &ev.function);
    char where[160];
    if (ev.file) snprintf(where, sizeof where, "%s() at %s:%d", ev.function ? ev.function : "??", path_base(ev.file), ev.line);
    else if (ev.function) snprintf(where, sizeof where, "%s () at 0x%08lx", ev.function, (unsigned long)ev.pc);
    else {
        struct DebugSymbol *ds = s->idebug->ObtainDebugSymbol((CONST_APTR)ev.pc, NULL);
        snprintf(where, sizeof where, "0x%08lx in %s", (unsigned long)ev.pc, ds && ds->Name ? ds->Name : "??");
        if (ds) s->idebug->ReleaseDebugSymbol(ds);
    }
    switch (reason) {
    case DBG_STOP_BREAKPOINT: snprintf(ev.text, sizeof ev.text, "Breakpoint %d, %s", bp_id, where); break;
    case DBG_STOP_ENTRY: snprintf(ev.text, sizeof ev.text, "Stopped at %s", where); break;
    case DBG_STOP_PAUSE: snprintf(ev.text, sizeof ev.text, "Paused in %s", where); break;
    case DBG_STOP_EXCEPTION:
        if (ev.trap_type == 0x300)
            snprintf(ev.text, sizeof ev.text, "Program crashed: %s at address 0x%08lx, in %s", trap_name(ev.trap_type), (unsigned long)ev.fault_addr, where);
        else
            snprintf(ev.text, sizeof ev.text, "Program crashed: %s (0x%lx), in %s", trap_name(ev.trap_type), (unsigned long)ev.trap_type, where);
        break;
    default: snprintf(ev.text, sizeof ev.text, "%s", where); break;
    }
    push_event(s, &ev);
}

static void report_exit(dbg_session *s)
{
    trace(s, "exit code %ld", (long)s->dm.dm_ReturnCode);
    dbg_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = DBG_EV_EXITED;
    ev.exit_code = (int)s->dm.dm_ReturnCode;
    snprintf(ev.text, sizeof ev.text, "Program exited with code %d", ev.exit_code);
    s->proc = NULL;
    s->op = OP_NONE;
    reap_timer(s, 1);
    for (size_t i = 0; i < s->traps.n; i++) VEC_AT(&s->traps, trap, i).suppressed = 0;
    clear_kinds(s, TK_TEMP | TK_PENTRY | TK_MAIN);
    sync_traps(s);                                   /* the seglist is still loaded */
    s->state = DBG_EXITED;
    s->nframes = 0;
    s->generation++;
    s->just_exited = 1;
    dt_after_exit(s);                                /* --- debugtools --- */
    push_event(s, &ev);
}

/* Frame 0 analysis at the current context. */
static void frame0(dbg_session *s, frame_info *f)
{
    dbg_mem m = session_mem(s);
    frame_analyse0(s->di, &m, s->ctx.ip, s->ctx.gpr[1], s->ctx.lr, f);
}

static void env_for_frame(dbg_session *s, int level, eval_env *env)
{
    memset(env, 0, sizeof *env);
    env->di = s->di;
    env->mem = session_mem(s);
    env->level = level;
    env->generation = s->generation;
    if (level < s->nframes) {
        env->pc = s->frames[level].pc;
        env->sp = s->frames[level].sp;
    } else {
        frame_info f;
        frame0(s, &f);
        env->pc = f.pc; env->sp = f.sp;
    }
    /* outer frames: the return address belongs to the line of the call */
    uint32_t look = level > 0 ? env->pc - 4 : env->pc;
    env->func = info_func_at(s->di, look);
    if (level > 0 && env->func) env->pc = look;
    env->regs = level == 0 ? &s->regs : NULL;
}

/* ======================================================================== resuming */

static void plant_line_traps(dbg_session *s, uint32_t lo, uint32_t hi, int file, int line)
{
    const dbg_info *di = s->di;
    uint32_t llo = info_r2l(di, lo), lhi = info_r2l(di, hi);
    for (size_t i = 0; i < di->lines.n; i++) {
        const dline *l = &VEC_AT(&di->lines, dline, i);
        if (l->addr < llo || l->addr >= lhi) continue;
        if (l->line == line && l->file == file) continue;
        add_kind(s, info_l2r(di, l->addr), TK_LINE);
    }
}

static void plant_function_entries(dbg_session *s)
{
    const dbg_info *di = s->di;
    for (size_t i = 0; i < di->funcs.n; i++) {
        const dfunc *f = &VEC_AT(&di->funcs, dfunc, i);
        if (f->has_lines) add_kind(s, info_l2r(di, f->body), TK_FNENTRY);
    }
    /* assembler functions: line entries outside stabs functions, at a .symtab start */
    for (size_t i = 0; i < di->lines.n; i++) {
        const dline *l = &VEC_AT(&di->lines, dline, i);
        if (l->func >= 0) continue;
        uint32_t lo, hi, body, run = info_l2r(di, l->addr);
        if (info_func_range(di, run, &lo, &hi, &body) && lo == run) add_kind(s, run, TK_FNENTRY);
    }
}

/* Plants the traps an operation needs at the current stop (not the step-off ones). */
static void plan_op(dbg_session *s)
{
    uint32_t pc = s->ctx.ip;
    frame_info f;
    frame0(s, &f);
    s->start_cfa = f.cfa;
    switch (s->op) {
    case OP_CONT:
        break;
    case OP_RUNTO:
        break;                                        /* the TK_RUNTO trap is already planted */
    case OP_STEPI: {
        uint32_t insn, out[2];
        dbg_mem m = session_mem(s);
        if (!mem_read32(&m, pc, &insn)) break;
        ppc_branch b;
        ppc_decode(insn, pc, &b);
        if (b.link && s->over_calls) { add_kind(s, pc + 4, TK_STEP); s->ret_sp = s->ctx.gpr[1]; break; }
        int n = ppc_successors(insn, pc, s->ctx.lr, s->ctx.ctr, out);
        for (int i = 0; i < n; i++) {
            if (info_in_program(s->di, out[i])) add_kind(s, out[i], TK_STEP);
            else if (b.link) add_kind(s, pc + 4, TK_STEP);   /* a call out of the program */
        }
        s->ret_sp = 0;
        break;
    }
    case OP_OVER: case OP_INTO: {
        const dline *l = info_in_program(s->di, pc) ? info_line_at(s->di, pc) : NULL;
        uint32_t lo, hi;
        if (!l || !info_func_range(s->di, pc, &lo, &hi, NULL)) { s->op = OP_OUT; goto out_op; }
        s->start_file = l->file; s->start_line = l->line;
        s->fn_lo = lo; s->fn_hi = hi;
        plant_line_traps(s, lo, hi, l->file, l->line);
        if (s->op == OP_INTO) plant_function_entries(s);
        if (f.ret && info_in_program(s->di, f.ret)) { add_kind(s, f.ret, TK_RET); s->ret_sp = f.cfa; }
        break;
    }
    case OP_OUT:
    out_op: {
        /* step out of the selected frame; a step without line info finishes up to the
         * first frame that has some */
        int need_lines = s->op != OP_OUT;
        int start = s->op == OP_OUT ? s->sel + 1 : 1;
        s->ret_sp = 0;
        for (int i = start; i < s->nframes; i++) {
            uint32_t look = s->frames[i].pc - 4;
            if (!info_in_program(s->di, s->frames[i].pc) || (need_lines && !info_line_at(s->di, look))) continue;
            add_kind(s, s->frames[i].pc, TK_RET);
            s->ret_sp = s->frames[i].sp;
            break;
        }
        s->op = OP_OUT;
        break;
    }
    }
}

/* Resumes the program. If a trap is wanted at the current pc, steps off it first. */
/* OS4 sometimes loses the wake-up of a task that has a debug hook: after a Signal() or a
 * RestartTask() it is marked TS_READY but never put on the ready list, so it never runs.
 * A SuspendTask() + RestartTask() puts it back in circulation. */
static int task_in_limbo(struct Task *t)
{
    if (t->tc_State != TS_READY) return 0;
    struct ExecBase *eb = (struct ExecBase *)SysBase;
    int limbo = 0;
    IExec->Forbid();
    if (t->tc_State == TS_READY) {
        limbo = 1;
        for (struct Node *n = eb->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
            if (n == &t->tc_Node) { limbo = 0; break; }
    }
    IExec->Permit();
    return limbo;
}

static void kick(dbg_session *s)
{
    struct Task *t = (struct Task *)s->proc;
    trace(s, "  kick: the task was ready but not queued");
    IExec->SuspendTask(t, 0);
    if (s->exc_count != s->seen) return;               /* it ran into a trap on the way: dbg_poll handles it */
    if (t->tc_State == TS_SUSPENDED) IExec->RestartTask(t, 0);
}

/* A RestartTask right after a hook suspension is sometimes lost: the task either drops back
 * to suspended or sits "ready" at the same address without ever running again. So check
 * that it got going — it hit the next trap, is waiting, or has moved off the restart
 * address — and kick it again if not. */
static void restart_checked(dbg_session *s, uint32_t pc)
{
    struct Task *t = (struct Task *)s->proc;
    uint32_t count = s->exc_count;
    for (int attempt = 0; attempt < 6; attempt++) {
        IExec->RestartTask(t, 0);
        delay_ms(s, 15);
        if (s->exc_count != count) return;
        /* it may have run to the end already: then the task is gone (and its memory reused) */
        if (!IsListEmpty(&s->dport->mp_MsgList) || s->remtask) return;
        uint8 st = t->tc_State;
        if (st == TS_WAIT || st == TS_REMOVED) return;
        if (st == TS_RUN || (st == TS_READY && !task_in_limbo(t))) return;   /* it's going */
        trace(s, "  restart didn't take (state %d), again", st);
        if (st != TS_SUSPENDED && st != TS_READY && st != TS_RUN) return;   /* not a state we know: leave it */
        if (t->tc_State != TS_SUSPENDED) {
            IExec->SuspendTask(t, 0);
            if (s->exc_count != count) return;        /* it hit something meanwhile */
        }
        delay_ms(s, SETTLE_MS);
    }
    trace(s, "  restart: giving up");
}

static void resume(dbg_session *s)
{
    uint32_t pc = s->ctx.ip;
    trap *t = find_trap(s, pc);
    s->stepping_off = 0;
    if (t && t->kinds) {
        uint32_t insn, out[2];
        dbg_mem m = session_mem(s);
        if (mem_read32(&m, pc, &insn)) {
            ppc_branch b;
            ppc_decode(insn, pc, &b);
            int n = ppc_successors(insn, pc, s->ctx.lr, s->ctx.ctr, out);
            for (int i = 0; i < n; i++) {
                if (out[i] == pc) continue;
                if (info_in_program(s->di, out[i])) add_kind(s, out[i], TK_STEPOFF);
                else if (b.link) add_kind(s, pc + 4, TK_STEPOFF);
            }
            t = find_trap(s, pc);                     /* the vector may have moved */
            t->suppressed = 1;
            s->stepping_off = 1;
            s->stepoff_pc = pc;
        }
    }
    trace(s, "resume pc %08lx op %d stepoff %d", (unsigned long)pc, s->op, s->stepping_off);
    sync_traps(s);
    s->state = DBG_RUNNING;
    s->nframes = 0;
    s->generation++;
    s->quick = s->stepping_off || s->op != OP_CONT;
    int before = ((struct Task *)s->proc)->tc_State;
    restart_checked(s, pc);
    trace(s, "  restarted: state %d -> %d", before, ((struct Task *)s->proc)->tc_State);
    arm_timer(s);
}

static int user_bp_at(dbg_session *s, uint32_t pc, int *cond_err)
{
    *cond_err = 0;
    for (size_t i = 0; i < s->bps.n; i++) {
        bpoint *b = &VEC_AT(&s->bps, bpoint, i);
        if (!b->pub.enabled || b->pub.addr != pc) continue;
        if (b->condition) {
            eval_env env;
            char err[160];
            frame_info f;
            frame0(s, &f);
            env_for_frame(s, 0, &env);
            env.pc = pc; env.sp = f.sp;
            int r = eval_condition(&env, b->condition, err, sizeof err);
            trace(s, "  condition \"%s\" = %d", b->condition, r);
            if (r == 0) continue;
            if (r < 0) { message(s, "Breakpoint %d: error in condition \"%s\": %s", b->pub.id, b->condition, err); *cond_err = 1; }
        }
        b->pub.hits++;
        return b->pub.id;
    }
    return 0;
}

/* A trap of ours was hit at pc. Returns 1 if the program stays stopped (event queued). */
static int handle_trap(dbg_session *s, trap *t)
{
    uint32_t pc = s->ctx.ip, kinds = t->kinds;
    if (s->stepping_off) {                            /* un-suppress the trap we stepped off */
        trap *o = find_trap(s, s->stepoff_pc);
        if (o) o->suppressed = 0;
        clear_kinds(s, TK_STEPOFF);
        s->stepping_off = 0;
        if (s->op == OP_STEPI) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
    }
    if (kinds & TK_PENTRY) {                          /* program entry: remember the kill context */
        s->entry_ctx = s->ctx;
        s->have_entry = 1;
        t->kinds &= ~TK_PENTRY;
    }
    if (kinds & TK_MAIN) { t->kinds &= ~TK_MAIN; dt_at_main(s); }   /* main is reached once; debugtools */
    int cond_err, id = (kinds & TK_USER) ? user_bp_at(s, pc, &cond_err) : 0;
    if (id) { report_stop(s, DBG_STOP_BREAKPOINT, id); return 1; }
    if ((kinds & TK_MAIN) && s->stop_at_main) { report_stop(s, DBG_STOP_ENTRY, 0); return 1; }
    uint32_t r1 = s->ctx.gpr[1];
    switch (s->op) {
    case OP_STEPI:
        if ((kinds & TK_STEP) && r1 >= s->ret_sp) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        break;
    case OP_RUNTO:
        if (kinds & TK_RUNTO) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        break;
    case OP_INTO:
        if (kinds & TK_FNENTRY) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        /* fall through */
    case OP_OVER:
        if (kinds & TK_LINE) {
            frame_info f;
            frame0(s, &f);
            if (f.cfa == s->start_cfa) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        }
        if ((kinds & TK_RET) && r1 >= s->ret_sp) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        break;
    case OP_OUT:
        if ((kinds & TK_RET) && r1 >= s->ret_sp) { report_stop(s, DBG_STOP_STEP, 0); return 1; }
        break;
    default:
        break;
    }
    return 0;                                         /* not for us: keep going */
}

/* The hook bumps exc_count before the kernel has finished suspending the task, and the
 * task can be preempted in between (other tasks at its priority may run first). Restarting
 * it then is lost: it finishes suspending afterwards and stays suspended. So before touching
 * it, raise its priority above ours and yield: if it is still on its way, it now runs first
 * and parks. Then its priority is put back. */

static void wait_suspended(dbg_session *s)
{
    struct Task *t = (struct Task *)s->proc;
    struct Task *me = IExec->FindTask(NULL);
    /* let it finish parking before anything else of ours runs */
    int8 old = IExec->SetTaskPri(t, me->tc_Node.ln_Pri + 1);
    int i;
    for (i = 0; i < 500 && t->tc_State != TS_SUSPENDED; i++) delay_ms(s, 1);
    /* TS_SUSPENDED is set before the kernel is done with the task: a RestartTask in the
     * next few milliseconds can be lost (the task then stays "ready" without ever running) */
    delay_ms(s, SETTLE_MS);
    IExec->SetTaskPri(t, old);
    if (t->tc_State != TS_SUSPENDED) trace(s, "task didn't park (state %d)", t->tc_State);
}

/* Processes a hit reported by the hook. */
static void on_exception(dbg_session *s)
{
    s->seen = s->exc_count;
    wait_suspended(s);
    s->ctx = s->exc;                                 /* the full context at the exception */
    regs_from_ctx(s);
    uint32_t pc = s->ctx.ip;
    trap *t = find_trap(s, pc);
    trace(s, "hit %lu: pc %08lx type %lx r1 %08lx trap %s kinds %lx op %d stepoff %d", (unsigned long)s->seen, (unsigned long)pc,
          (unsigned long)s->ctx.Traptype, (unsigned long)s->ctx.gpr[1], t ? (t->inserted ? "in" : "out") : "none",
          t ? (unsigned long)t->kinds : 0UL, s->op, s->stepping_off);
    if (s->ctx.Traptype == 0x700 && t && t->inserted) {
        if (handle_trap(s, t)) return;
        resume(s);
        return;
    }
    /* a real exception in the program */
    s->op = OP_NONE;
    report_stop(s, DBG_STOP_EXCEPTION, 0);
}

static void check_death(dbg_session *s)
{
    if (s->proc && IExec->GetMsg(s->dport)) report_exit(s);
}

/* Waits briefly for the next hit after an internal resume, so steps don't cost a poll. */
static void quick_wait(dbg_session *s)
{
    if (s->proc) trace(s, "  quick wait: state %d count %lu seen %lu", ((struct Task *)s->proc)->tc_State, (unsigned long)s->exc_count, (unsigned long)s->seen);
    for (int i = 0; i < QUICK_WAIT_MS && s->state == DBG_RUNNING; i++) {
        if (s->exc_count != s->seen) { on_exception(s); i = 0; if (!s->quick) break; s->quick = 0; continue; }
        check_death(s);
        if (s->state != DBG_RUNNING) break;
        delay_ms(s, 1);
    }
}

int dbg_poll(dbg_session *s, dbg_event *ev)
{
    if (s->ev_head < s->events.n) goto pop;
    if (s->timer_busy) reap_timer(s, 0);
    if (s->state == DBG_RUNNING && s->proc) {
        for (int guard = 0; guard < 1000 && s->state == DBG_RUNNING; guard++) {
            if (s->exc_count != s->seen) { on_exception(s); continue; }
            break;
        }
        check_death(s);
        /* (debugtools) DOS cleans an ended process up at priority -10, spinning in the kernel
         * until it gets the CPU; with a busy machine (a profiler, another program) that can take
         * forever. Once the program has returned, every ~200 ms that the process is still here
         * with a lowered priority, give it a normal one so it can finish. */
        if (s->proc && s->returned && ++s->returned_polls % 20 == 0 && ((struct Task *)s->proc)->tc_Node.ln_Pri < 0) {
            trace(s, "process still in DOS's cleanup: raising its priority");
            IExec->SetTaskPri((struct Task *)s->proc, 0);
        }
        /* OS4 sometimes stops dispatching a ready debuggee (with a debug hook) until something
         * reschedules it; it then sits ready at the same address. Kick it when we see that.
         * (debugtools) Not once the program has returned: the hook is off, and suspending a
         * process in DOS's cleanup left it hanging there. */
        if (s->state == DBG_RUNNING && s->proc && !s->returned && !dt_sampling(s)) {
            struct Task *t = (struct Task *)s->proc;
            if (t->tc_State == TS_READY) {
                struct ExceptionContext c;
                s->idebug->ReadTaskContext(t, &c, RTCF_STATE);
                if (c.ip == s->stall_ip) {
                    if (++s->stall >= 2) {
                        s->stall = 0;
                        kick(s);
                        if (s->exc_count != s->seen) on_exception(s);
                    }
                } else {
                    s->stall_ip = c.ip;
                    s->stall = 0;
                }
            } else {
                s->stall = 0;
            }
        }
        if (s->state == DBG_RUNNING && s->quick && s->events.n == s->ev_head) { s->quick = 0; quick_wait(s); }
        if (s->state == DBG_RUNNING) arm_timer(s);
    }
    if (s->ev_head >= s->events.n) { s->events.n = s->ev_head = 0; return 0; }
pop:
    *ev = VEC_AT(&s->events, dbg_event, s->ev_head++);
    if (s->ev_head >= s->events.n) s->events.n = s->ev_head = 0;
    return 1;
}

/* ======================================================================== execution */

static int open_console(dbg_session *s, BPTR *in, BPTR *out, int *close_in)
{
    char spec[256];
    const char *name = path_base(s->path);
    if (s->console && *s->console) snprintf(spec, sizeof spec, "%s", s->console);
    else snprintf(spec, sizeof spec, "CON:40/40/640/240/%s/AUTO/CLOSE", name);
    s->con_is_file = strncasecmp(spec, "CON:", 4) && strncasecmp(spec, "RAW:", 4);
    *out = IDOS->Open(spec, MODE_NEWFILE);
    if (!*out) return set_err(s, "can't open console %s", spec);
    if (s->con_is_file) { *in = IDOS->Open("NIL:", MODE_OLDFILE); *close_in = 1; }
    else { *in = *out; *close_in = 0; }     /* one console handle, closed once (as output) */
    return 1;
}

/* The debuggee process runs this (code in the IDE, data in the session) and the program
 * inside it with RunCommand, the way a shell does. Starting the program itself as an
 * NP_Seglist process looks simpler, but on OS4 a second such process created by the same
 * parent hangs in DOS's process cleanup when it ends (see docs/debugger-notes.md). */
static int32 launcher(STRPTR args, int32 len, struct ExecBase *sysbase)
{
    dbg_session *s = IExec->FindTask(NULL)->tc_UserData;
    /* set up the directories and stderr here rather than with NP_ tags: the minimal tag set
     * is the one verified to start and end cleanly run after run */
    BPTR cwd = s->run_cwd, home = s->run_home;
    BPTR oldcwd = cwd ? IDOS->SetCurrentDir(cwd) : 0, oldhome = home ? IDOS->SetProgramDir(home) : 0;
    BPTR olderr = IDOS->SelectErrorOutput(IDOS->Output());
    /* Hook ourselves rather than being suspended and hooked from outside: a process that is
     * suspended before it has ever run hangs in DOS's cleanup when it ends. Then wait for the
     * debugger to plant its traps. */
    struct Task *me = IExec->FindTask(NULL);
    s->idebug->AddDebugHook(me, &s->hook);
    IExec->SetSignal(0, SIGBREAKF_CTRL_F);
    s->launcher_ready = 1;
    IExec->Wait(SIGBREAKF_CTRL_F);
    dt_program_starting(s);                          /* --- debugtools --- */
    int32 rc = IDOS->RunCommand(s->seg, DEFAULT_STACK, s->run_args, s->run_arglen);
    dt_program_returned(s);                          /* --- debugtools --- */
    s->returned = 1;                                 /* debugtools: no more kicks (see dbg_poll) */
    /* and leave without the hook: DOS's process cleanup then runs as for any program */
    s->idebug->AddDebugHook(me, NULL);
    IDOS->SelectErrorOutput(olderr);
    if (home) { IDOS->SetProgramDir(oldhome); IDOS->UnLock(home); }
    if (cwd) { IDOS->SetCurrentDir(oldcwd); IDOS->UnLock(cwd); }
    return rc;
}

static int32 pid_hook(struct Hook *h, APTR pid, struct Process *p)
{
    return p->pr_ProcessID == (uint32)pid;
}

/* A process that has sent its death message is still in DOS's cleanup for a while, and
 * a new process started meanwhile can be starved until it's gone. Wait for it. */
static void wait_gone(dbg_session *s)
{
    if (!s->last_pid) return;
    struct Hook h;
    memset(&h, 0, sizeof h);
    h.h_Entry = (HOOKFUNC)pid_hook;
    int i;
    for (i = 0; i < 300 && IDOS->ProcessScan(&h, (CONST_APTR)s->last_pid, 0); i++) delay_ms(s, 10);
    trace(s, "previous process %s after %d ms", i < 300 ? "gone" : "still there", i * 10);
    s->last_pid = 0;
}

int dbg_run(dbg_session *s, int stop_at_main)
{
    trace(s, "run %s %s", s->path ? s->path : "", s->args ? s->args : "");
    if (s->state != DBG_LOADED && s->state != DBG_EXITED) return set_err(s, "nothing to run");
    if (s->ran) {                                     /* fresh data sections for every run */
        int patched = s->hp_count;                    /* --- hotpatch: back to the file's debug info */
        unload_seg(s);
        if (patched) {
            char err[128];
            dbg_info *di = info_load(s->path, err, sizeof err);
            if (!di) { info_free(s->di); s->di = NULL; s->state = DBG_NONE; return set_err(s, "%s", err); }
            info_free(s->di);
            s->di = di;
            hp_elf_free(s->hp_exe); s->hp_exe = NULL;
        }
        if (!load_seg(s)) { s->state = DBG_NONE; return 0; }
        resolve_all_bps(s);
    }
    BPTR in = 0, out = 0;
    int close_in = 0;
    if (s->just_exited) {           /* the last process may still be in DOS's cleanup */
        wait_gone(s);
        s->just_exited = 0;
    }
    if (!open_console(s, &in, &out, &close_in)) return 0;
    BPTR cwd = s->cwd ? IDOS->Lock(s->cwd, SHARED_LOCK) : 0;
    BPTR progdir = 0;
    BPTR fl = IDOS->Lock(s->path, SHARED_LOCK);
    if (fl) { progdir = IDOS->ParentDir(fl); IDOS->UnLock(fl); }
    if (!cwd && progdir) cwd = IDOS->DupLock(progdir);
    size_t al = strlen(s->args);
    free(s->run_args);
    s->run_args = malloc(al + 2);
    if (!s->run_args) return set_err(s, "out of memory");
    memcpy(s->run_args, s->args, al);
    s->run_args[al] = '\n'; s->run_args[al + 1] = 0;
    s->run_arglen = (int32_t)al + 1;
    memset(&s->dm, 0, sizeof s->dm);
    s->dm.dm_Msg.mn_ReplyPort = s->dport;
    s->dm.dm_Msg.mn_Length = sizeof s->dm;
    while (IExec->GetMsg(s->dport)) {}
    const char *name = path_base(s->path);

    s->exc_count = s->seen = 0;
    s->remtask = 0;
    s->returned = 0;                                 /* debugtools */
    s->returned_polls = 0;
    s->have_entry = 0;
    s->stop_at_main = stop_at_main;

    s->run_cwd = cwd; s->run_home = progdir;
    s->launcher_ready = 0;
    /* a clean hook every run (the kernel may link it through h_MinNode) */
    memset(&s->hook, 0, sizeof s->hook);
    s->hook.h_Entry = (HOOKFUNC)debug_hook;
    s->hook.h_Data = s;
    /* the process owns its handles (it closes them when it ends) */
    s->proc = IDOS->CreateNewProcTags(
        NP_Entry, launcher, NP_UserData, s,
        NP_Name, name, NP_CommandName, name, NP_Cli, TRUE,
        NP_Input, in, NP_CloseInput, close_in, NP_Output, out, NP_CloseOutput, TRUE,
        NP_NotifyOnDeathMessage, &s->dm,
        TAG_DONE);
    if (!s->proc) {
        if (close_in) IDOS->Close(in);
        IDOS->Close(out);
        if (cwd) IDOS->UnLock(cwd);
        if (progdir) IDOS->UnLock(progdir);
        return set_err(s, "can't create the process (error %ld)", (long)IDOS->IoErr());
    }
    s->last_pid = s->proc->pr_ProcessID;
    for (int i = 0; i < 400 && !s->launcher_ready; i++) delay_ms(s, 5);
    if (!s->launcher_ready) trace(s, "launcher didn't report ready");
    s->ran = 1;

    add_kind(s, s->entry_addr, TK_PENTRY);
    if ((stop_at_main || dt_wants_main(s)) && s->main_addr) add_kind(s, s->main_addr, TK_MAIN);   /* debugtools */
    refresh_user_traps(s);
    sync_traps(s);
    s->op = OP_CONT;
    s->state = DBG_RUNNING;
    s->generation++;
    dt_before_go(s);                                 /* --- debugtools --- */
    IExec->Signal((struct Task *)s->proc, SIGBREAKF_CTRL_F);   /* go */
    arm_timer(s);
    return 1;
}

static int start_op(dbg_session *s, int op)
{
    if (s->state != DBG_STOPPED) return set_err(s, "the program isn't stopped");
    s->op = op;
    plan_op(s);
    resume(s);
    return 1;
}

int dbg_continue(dbg_session *s) { return start_op(s, OP_CONT); }
int dbg_step_into(dbg_session *s) { return start_op(s, OP_INTO); }
int dbg_step_over(dbg_session *s) { return start_op(s, OP_OVER); }
int dbg_step_out(dbg_session *s) { return start_op(s, OP_OUT); }

int dbg_step_instruction(dbg_session *s, int over_calls)
{
    s->over_calls = over_calls;
    return start_op(s, OP_STEPI);
}

int dbg_run_to(dbg_session *s, const char *file, int line)
{
    if (s->state != DBG_STOPPED) return set_err(s, "the program isn't stopped");
    int actual;
    uint32_t a = info_line_addr(s->di, file, line, &actual);
    if (!a) return set_err(s, "no code at %s:%d", file, line);
    add_kind(s, a, TK_RUNTO);
    return start_op(s, OP_RUNTO);
}

int dbg_pause(dbg_session *s)
{
    if (s->state != DBG_RUNNING || !s->proc) return set_err(s, "the program isn't running");
    trace(s, "pause: state %d count %lu seen %lu", ((struct Task *)s->proc)->tc_State, (unsigned long)s->exc_count, (unsigned long)s->seen);
    IExec->SuspendTask((struct Task *)s->proc, 0);
    if (s->exc_count != s->seen) { on_exception(s); if (s->state != DBG_RUNNING) return 1; IExec->SuspendTask((struct Task *)s->proc, 0); }
    check_death(s);
    if (s->state != DBG_RUNNING) return 1;
    read_context(s);
    /* stepping off a trap: put it back, the program is not between our two traps anymore */
    if (s->stepping_off) { trap *o = find_trap(s, s->stepoff_pc); if (o) o->suppressed = 0; }
    report_stop(s, DBG_STOP_PAUSE, 0);
    return 1;
}

static void wait_death(dbg_session *s, uint32_t ms)
{
    for (uint32_t t = 0; t < ms && s->proc; t += 5) {
        if (IExec->GetMsg(s->dport)) { report_exit(s); return; }
        if (s->exc_count != s->seen) {               /* crashed on the way out: give up on it */
            s->seen = s->exc_count;
            break;
        }
        delay_ms(s, 5);
    }
}

/* Makes the program return from its entry point with EXIT_KILLED. */
static void kill_now(dbg_session *s)
{
    if (!s->proc) return;
    trace(s, "kill: state %d have_entry %d", s->state, s->have_entry);
    if (s->returned) {
        /* (debugtools) the program has already returned and DOS is cleaning the process up:
         * the entry context means nothing any more (restoring it crashed the process). Let it
         * finish, at a normal priority. */
        IExec->SetTaskPri((struct Task *)s->proc, 0);
        wait_death(s, 5000);
        if (!s->proc) return;
    }
    if (s->state == DBG_RUNNING) {
        IExec->SuspendTask((struct Task *)s->proc, 0);
        if (s->exc_count != s->seen) { s->seen = s->exc_count; wait_suspended(s); }
        check_death(s);
        if (!s->proc) return;
    }
    if (!s->have_entry) {
        /* not at the entry point yet: let it get there (the entry trap is always planted) */
        clear_kinds(s, ~(uint32_t)TK_PENTRY);
        sync_traps(s);
        IExec->RestartTask((struct Task *)s->proc, 0);
        for (int i = 0; i < 200 && s->exc_count == s->seen; i++) delay_ms(s, 5);
        if (s->exc_count == s->seen) { IExec->SuspendTask((struct Task *)s->proc, 0); }
        else { s->seen = s->exc_count; wait_suspended(s); s->entry_ctx = s->exc; s->have_entry = 1; }
    }
    remove_all_traps(s);
    if (s->have_entry) {
        struct ExceptionContext c = s->entry_ctx;
        c.ip = c.lr;
        c.gpr[3] = EXIT_KILLED;
        s->idebug->WriteTaskContext((struct Task *)s->proc, &c, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
        IExec->RestartTask((struct Task *)s->proc, 0);
        wait_death(s, KILL_WAIT_MS);
    }
    if (s->proc) {
        /* couldn't unwind it: leave it suspended rather than letting it run without us */
        IExec->SuspendTask((struct Task *)s->proc, 0);
        s->idebug->AddDebugHook((struct Task *)s->proc, NULL);
        message(s, "the program could not be stopped cleanly; it was left suspended");
        s->proc = NULL;
        s->seg = 0;                                   /* still in use by the suspended process */
        s->state = DBG_EXITED;
        s->nframes = 0;
    }
}

int dbg_kill(dbg_session *s)
{
    if (!s->proc) return set_err(s, "the program isn't running");
    kill_now(s);
    return 1;
}

/* ======================================================================== inspection */

int dbg_get_regs(dbg_session *s, dbg_regs *r)
{
    if (s->state != DBG_STOPPED) return set_err(s, "the program isn't stopped");
    *r = s->regs;
    return 1;
}

int dbg_set_reg(dbg_session *s, const char *name, uint32_t value)
{
    if (s->state != DBG_STOPPED) return set_err(s, "the program isn't stopped");
    if ((name[0] == 'r' || name[0] == 'R') && name[1] >= '0' && name[1] <= '9') {
        int n = atoi(name + 1);
        if (n < 0 || n > 31) return set_err(s, "no register %s", name);
        s->ctx.gpr[n] = value;
        write_ctx(s, RTCF_GENERAL);
    } else if (!strcmp(name, "pc") || !strcmp(name, "ip")) { s->ctx.ip = value; write_ctx(s, RTCF_STATE); }
    else if (!strcmp(name, "lr")) { s->ctx.lr = value; write_ctx(s, RTCF_SPECIAL); }
    else if (!strcmp(name, "ctr")) { s->ctx.ctr = value; write_ctx(s, RTCF_SPECIAL); }
    else if (!strcmp(name, "cr")) { s->ctx.cr = value; write_ctx(s, RTCF_SPECIAL); }
    else if (!strcmp(name, "xer")) { s->ctx.xer = value; write_ctx(s, RTCF_SPECIAL); }
    else if (!strcmp(name, "sp")) { s->ctx.gpr[1] = value; write_ctx(s, RTCF_GENERAL); }
    else return set_err(s, "no register %s", name);
    compute_frames(s);
    s->generation++;
    return 1;
}

int dbg_stack(dbg_session *s, dbg_frame *out, int max)
{
    if (s->state != DBG_STOPPED) return 0;
    int n = 0;
    for (int i = 0; i < s->nframes && n < max; i++) {
        const frame_info *fi = &s->frames[i];
        dbg_frame *f = &out[n];
        memset(f, 0, sizeof *f);
        f->level = i; f->pc = fi->pc; f->sp = fi->sp;
        uint32_t look = i > 0 ? fi->pc - 4 : fi->pc;
        if (info_in_program(s->di, look)) {
            const char *file, *func;
            describe(s, look, &file, &f->line, &func);
            f->file = file;
            f->function = func ? func : "??";
            f->module = "program";
        } else {
            struct DebugSymbol *ds = s->idebug->ObtainDebugSymbol((CONST_APTR)look, NULL);
            if (ds) {
                f->module = ds->Name ? arena_strdup(&s->stop_mem, path_base(ds->Name)) : "??";
                f->function = ds->SourceFunctionName ? arena_strdup(&s->stop_mem, ds->SourceFunctionName) : "??";
                if (ds->SourceFileName && ds->SourceLineNumber) {
                    f->file = arena_strdup(&s->stop_mem, ds->SourceFileName);
                    f->line = (int)ds->SourceLineNumber;
                }
                s->idebug->ReleaseDebugSymbol(ds);
            } else { f->function = "??"; f->module = "??"; }
        }
        n++;
    }
    return n;
}

int dbg_select_frame(dbg_session *s, int level)
{
    if (s->state != DBG_STOPPED || level < 0 || level >= s->nframes) return set_err(s, "no frame %d", level);
    s->sel = level;
    return 1;
}

int dbg_locals(dbg_session *s, dbg_var ***out)
{
    *out = NULL;
    if (s->state != DBG_STOPPED) return 0;
    eval_env env;
    env_for_frame(s, s->sel, &env);
    return eval_locals(s->vs, &env, out);
}

int dbg_globals(dbg_session *s, dbg_var ***out)
{
    *out = NULL;
    if (!s->di || !s->seg) return 0;
    eval_env env;
    if (s->state == DBG_STOPPED) env_for_frame(s, s->sel, &env);
    else { memset(&env, 0, sizeof env); env.di = s->di; env.mem = session_mem(s); env.generation = s->generation; }
    int cu = env.func ? env.func->cu : info_cu_at(s->di, env.pc);
    if (cu < 0) {
        const dfunc *m = info_func_named(s->di, "main");
        cu = m ? m->cu : -1;
    }
    return eval_globals(s->vs, &env, cu, out);
}

dbg_var *dbg_eval(dbg_session *s, const char *expr)
{
    eval_env env;
    if (!s->di || !s->seg) return eval_error(s->vs, expr ? expr : "", "no program loaded");
    if (s->state == DBG_STOPPED) env_for_frame(s, s->sel, &env);
    else { memset(&env, 0, sizeof env); env.di = s->di; env.mem = session_mem(s); env.generation = s->generation; }
    return eval_expr(s->vs, &env, expr);
}

int dbg_var_children(dbg_session *s, dbg_var *v, dbg_var ***out)
{
    *out = NULL;
    if (!s->di || !s->seg) return 0;
    eval_env env;
    if (s->state == DBG_STOPPED) env_for_frame(s, s->sel, &env);
    else { memset(&env, 0, sizeof env); env.di = s->di; env.mem = session_mem(s); env.generation = s->generation; }
    return eval_children(s->vs, &env, v, out);
}

int dbg_var_set(dbg_session *s, dbg_var *v, const char *value)
{
    if (s->state != DBG_STOPPED && s->state != DBG_LOADED) return set_err(s, "the program isn't stopped");
    eval_env env;
    if (s->state == DBG_STOPPED) env_for_frame(s, s->sel, &env);
    else { memset(&env, 0, sizeof env); env.di = s->di; env.mem = session_mem(s); env.generation = s->generation; }
    char err[160];
    if (!eval_assign(s->vs, &env, v, value, err, sizeof err)) return set_err(s, "%s", err);
    return 1;
}

void dbg_var_free(dbg_session *s, dbg_var **vars, int n) { varstore_release(s->vs, vars, n); }
void dbg_var_release(dbg_session *s, dbg_var *v) { if (v) varstore_release(s->vs, &v, 1); }

int dbg_read_memory(dbg_session *s, uint32_t addr, void *buf, uint32_t len)
{
    if (s->state == DBG_RUNNING) return 0;
    return mem_read(s, addr, buf, len);
}

int dbg_write_memory(dbg_session *s, uint32_t addr, const void *buf, uint32_t len)
{
    if (s->state != DBG_STOPPED && s->state != DBG_LOADED) return set_err(s, "the program isn't stopped");
    return mem_write(s, addr, buf, len);
}

int dbg_disassemble(dbg_session *s, uint32_t addr, int count, dbg_insn *out)
{
    if (s->state == DBG_RUNNING) return 0;
    addr &= ~3u;
    int n = 0;
    for (int i = 0; i < count; i++, addr += 4) {
        dbg_insn *d = &out[n];
        memset(d, 0, sizeof *d);
        d->addr = addr;
        uint8_t b[4];
        if (mem_read(s, addr, b, 4) != 4) {
            snprintf(d->text, sizeof d->text, "<unreadable>");
            n++;
            continue;
        }
        d->word = be32(b);
        trap *t = find_trap(s, addr);
        int patched = t && t->inserted;
        if (patched) code_write(s, addr, t->orig);    /* show the real instruction */
        char op[LEN_DISASSEMBLE_OPCODE_STRING + 8] = "", ops[LEN_DISASSEMBLE_OPERANDS_STRING + 8] = "";
        s->idebug->DisassembleNative((APTR)addr, (STRPTR)op, (STRPTR)ops);
        if (patched) code_write(s, addr, PPC_TRAP_INSN);
        snprintf(d->text, sizeof d->text, "%s%s%s", op, ops[0] ? " " : "", ops);
        for (size_t k = 0; k < s->bps.n; k++) {
            const bpoint *bp = &VEC_AT(&s->bps, bpoint, k);
            if (bp->pub.enabled && bp->pub.addr == addr) d->is_breakpoint = 1;
        }
        if (s->di && info_in_program(s->di, addr)) {
            char sym[160];
            if (info_symbolize(s->di, addr, sym, sizeof sym)) d->symbol = arena_strdup(&s->stop_mem, sym);
            if (info_is_line_start(s->di, addr)) {
                const dline *l = info_line_at(s->di, addr);
                if (l) { d->file = info_file_name(s->di, l->file); d->line = l->line; }
            }
        } else {
            struct DebugSymbol *ds = s->idebug->ObtainDebugSymbol((CONST_APTR)addr, NULL);
            if (ds) {
                char sym[160];
                if (ds->SourceFunctionName) snprintf(sym, sizeof sym, "%s", ds->SourceFunctionName);
                else snprintf(sym, sizeof sym, "%s+0x%lx", ds->Name ? path_base(ds->Name) : "?", (unsigned long)ds->Offset);
                d->symbol = arena_strdup(&s->stop_mem, sym);
                s->idebug->ReleaseDebugSymbol(ds);
            }
        }
        n++;
    }
    return n;
}

/* ======================================================================== symbols */

int dbg_addr_to_line(dbg_session *s, uint32_t addr, const char **file, int *line, const char **function)
{
    if (!s->di) return 0;
    const char *f, *fn;
    int l;
    describe(s, addr, &f, &l, &fn);
    if (file) *file = f;
    if (line) *line = l;
    if (function) *function = fn;
    return f != NULL || fn != NULL;
}

uint32_t dbg_line_to_addr(dbg_session *s, const char *file, int line, int *actual_line)
{
    if (!s->di) return 0;
    return info_line_addr(s->di, file, line, actual_line);
}

uint32_t dbg_function_addr(dbg_session *s, const char *function)
{
    if (!s->di) return 0;
    const dfunc *f = info_func_named(s->di, function);
    if (f) return info_l2r(s->di, f->body);
    return info_symbol_addr(s->di, function);
}


/* ======================================================================== --- hotpatch: Edit and Continue */

/* Frees the patches of the current run (the program's code is gone or about to be). */
static void hp_release(dbg_session *s)
{
    for (size_t i = 0; i < s->hp_mem.n; i++) IExec->FreeVec(VEC_AT(&s->hp_mem, APTR, i));
    s->hp_mem.n = 0;
    for (size_t i = 0; i < s->hp_funcs.n; i++) {
        hp_entry *e = &VEC_AT(&s->hp_funcs, hp_entry, i);
        free(e->source); free(e->name);
    }
    s->hp_funcs.n = 0;
    s->hp_count = 0;
}

static uint32_t hp_l2r(void *ud, uint32_t link) { return info_l2r(((dbg_session *)ud)->di, link); }

/* Copies into executable memory: write access switched on around the copy (keeping
 * EXECUTE and the cache bits, as code_write does), then caches flushed. */
static void hp_copy_code(dbg_session *s, uint32_t addr, const void *src, uint32_t len)
{
    APTR st = IExec->SuperState();
    for (uint32_t done = 0; done < len;) {
        uint32_t a = addr + done, n = 4096 - (a & 4095);
        if (n > len - done) n = len - done;
        uint32_t old = s->immu->GetMemoryAttrs((APTR)a, 0);
        uint32_t rw = ((old & ~(uint32_t)MEMATTRF_RW_MASK) | MEMATTRF_READ_WRITE) &
                      ~(uint32_t)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED | MEMATTRF_NOT_MAPPED);
        s->immu->SetMemoryAttrs((APTR)a, n, rw);
        memcpy((void *)a, (const uint8_t *)src + done, n);
        s->immu->SetMemoryAttrs((APTR)a, n, old & ~(uint32_t)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED));
        done += n;
    }
    if (st) IExec->UserState(st);
    IExec->CacheClearE((APTR)addr, len, CACRF_ClearI | CACRF_ClearD);
}

/* Does [lo, hi) overlap the program's link-time addresses (which would confuse the
 * link/run address mapping)? */
static int hp_overlaps_link(const dbg_info *di, uint32_t lo, uint32_t hi)
{
    for (int i = 0; i < di->elf.nsect; i++) {
        const elf_section *e = &di->elf.sect[i];
        if (!(e->flags & ELF_SHF_ALLOC) || !e->size) continue;
        if (lo < e->addr + e->size && e->addr < hi) return 1;
    }
    return 0;
}

static int hp_msg(char *msg, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int hp_msg(char *msg, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (msg && n) vsnprintf(msg, n, fmt, ap);
    va_end(ap);
    return 0;
}

int dbg_hotpatch_count(dbg_session *s) { return s ? s->hp_count : 0; }

int dbg_hotpatch(dbg_session *s, const char *object, const char *source, char *msg, size_t msglen)
{
    if (msg && msglen) msg[0] = 0;
    if (!s->di || !s->seg || !s->proc || s->state != DBG_STOPPED)
        return hp_msg(msg, msglen, "Changes can only be applied while the program is stopped in the debugger.");
    char err[300];
    if (!s->hp_exe) {
        s->hp_exe = hp_elf_load(s->path, 0, err, sizeof err);
        if (!s->hp_exe) return hp_msg(msg, msglen, "Can't read the program: %s", err);
    }
    hp_elf *obj = hp_elf_load(object, 1, err, sizeof err);
    if (!obj) return hp_msg(msg, msglen, "Can't read the compiled file: %s", err);
    hp_patch p;
    if (!hp_prepare(&p, obj, s->hp_exe, source, hp_l2r, s, err, sizeof err)) {
        hp_elf_free(obj);
        return hp_msg(msg, msglen, "Can't apply the changes to %s: %s.", path_base(source), err);
    }

    /* memory for it, away from the program's link-time addresses */
    APTR mem = NULL, spare[4];
    int nspare = 0;
    for (int tries = 0; tries < 4 && !mem; tries++) {
        APTR m = IExec->AllocVecTags(p.mem_size, AVT_Type, MEMF_EXECUTABLE, AVT_ClearWithValue, 0, TAG_DONE);
        if (!m) break;
        if (hp_overlaps_link(s->di, (uint32_t)m, (uint32_t)m + p.mem_size)) spare[nspare++] = m;
        else mem = m;
    }
    while (nspare) IExec->FreeVec(spare[--nspare]);
    if (!mem) { hp_patch_free(&p); hp_elf_free(obj); return hp_msg(msg, msglen, "Not enough memory for the changed code."); }
    uint32_t base = (uint32_t)mem;
    char farvar[8];   /* tests: ENV:AFHOTPATCH_FAR=1 takes the long-branch paths even when in range */
    int force_far = IDOS->GetVar("AFHOTPATCH_FAR", farvar, sizeof farvar, 0) > 0 && farvar[0] == '1';
    p.force_far = force_far;
    if (!hp_link(&p, base, err, sizeof err)) {
        IExec->FreeVec(mem); hp_patch_free(&p); hp_elf_free(obj);
        return hp_msg(msg, msglen, "Can't apply the changes to %s: %s.", path_base(source), err);
    }

    /* which entries get a branch: the program's version and earlier patches' versions */
    typedef struct { uint32_t at, to; int words; uint32_t w[4]; const char *name; } redirect;
    vec reds;
    vec_init(&reds, sizeof(redirect));
    char skipped[200] = "";
    for (int i = 0; i < p.nfuncs; i++) {
        const hp_func *f = &p.funcs[i];
        uint32_t olds[17], sizes[17];
        int n = 0;
        if (f->old_addr) { olds[n] = f->old_addr; sizes[n++] = f->old_size; }
        for (size_t k = 0; k < s->hp_funcs.n && n < 17; k++) {
            hp_entry *e = &VEC_AT(&s->hp_funcs, hp_entry, k);
            if (!strcmp(e->name, f->name) && path_base(e->source) && str_ieq(path_base(e->source), path_base(source))) {
                olds[n] = e->addr; sizes[n++] = e->size;
            }
        }
        for (int k = 0; k < n; k++) {
            redirect *r = vec_push(&reds);
            if (!r) continue;
            r->at = olds[k]; r->to = f->new_addr; r->name = f->name;
            r->words = hp_branch_ex(r->at, r->to, force_far, r->w);
            if (sizes[k] && sizes[k] < 4u * (uint32_t)r->words) {
                reds.n--;
                size_t l = strlen(skipped);
                snprintf(skipped + l, sizeof skipped - l, "%s%s", l ? ", " : "", f->name);
            }
        }
    }
    /* never overwrite instructions a frame is in the middle of */
    for (size_t i = 0; i < reds.n; i++) {
        redirect *r = &VEC_AT(&reds, redirect, i);
        for (int k = 0; k < s->nframes; k++) {
            uint32_t pc = s->frames[k].pc;
            if (pc > r->at && pc < r->at + 4u * (uint32_t)r->words) {
                char dm[200];
                const char *nm = dbg_demangle(r->name, dm, sizeof dm);
                vec_free(&reds); IExec->FreeVec(mem); hp_patch_free(&p); hp_elf_free(obj);
                return hp_msg(msg, msglen, "%s is in its first instructions: step once more, then apply the changes.", nm ? nm : r->name);
            }
        }
    }

    /* in it goes */
    hp_copy_code(s, base, p.image, p.mem_size);
    *(APTR *)vec_push(&s->hp_mem) = mem;
    dbg_info *pi = hp_debug_info(&p, err, sizeof err);
    int have_info = pi && hp_merge_info(s->di, pi, source, base, p.mem_size, p.code_lo, p.code_hi);
    if (pi && !have_info) info_free(pi);
    if (!have_info) {
        /* the code is still part of the program for traps and memory access */
        dreloc *rl = realloc(s->di->reloc, sizeof(dreloc) * (size_t)(s->di->nreloc + 1));
        uint32_t (*pc)[2] = realloc(s->di->patch_code, sizeof(*pc) * (size_t)(s->di->npatch_code + 1));
        if (rl) { s->di->reloc = rl; rl[s->di->nreloc++] = (dreloc){ base, p.mem_size, base }; }
        if (pc) { s->di->patch_code = pc; pc[s->di->npatch_code][0] = p.code_lo; pc[s->di->npatch_code++][1] = p.code_hi; }
    }
    for (size_t i = 0; i < reds.n; i++) {
        redirect *r = &VEC_AT(&reds, redirect, i);
        uint8_t b[16];
        for (int k = 0; k < r->words; k++) put_be32(b + 4 * k, r->w[k]);
        if (mem_write(s, r->at, b, 4u * (uint32_t)r->words) != 4 * r->words)
            trace(s, "hotpatch: couldn't redirect %s at %08lx", r->name, (unsigned long)r->at);
        else
            trace(s, "hotpatch: %s %08lx -> %08lx (%d words)", r->name, (unsigned long)r->at, (unsigned long)r->to, r->words);
    }
    for (int i = 0; i < p.nfuncs; i++) {
        hp_entry *e = vec_push(&s->hp_funcs);
        if (!e) break;
        e->source = sdup(source); e->name = sdup(p.funcs[i].name);
        e->addr = p.funcs[i].new_addr; e->size = p.funcs[i].new_size;
    }
    s->hp_count++;

    /* breakpoints in the file move to the new code; frames and lines are looked up again */
    resolve_all_bps(s);
    sync_traps(s);
    arena_free(&s->stop_mem);
    compute_frames(s);
    s->generation++;

    /* summary */
    char list[300] = "", running[200] = "";
    int nnew = 0, nchanged = 0;
    for (int i = 0; i < p.nfuncs; i++) {
        const hp_func *f = &p.funcs[i];
        char dm[200];
        const char *nm = dbg_demangle(f->name, dm, sizeof dm);
        if (nm) { char *par = strchr(dm, '('); if (par) *par = 0; } else nm = f->name;
        if (f->old_addr) nchanged++; else nnew++;
        size_t l = strlen(list);
        if (l < sizeof list - 40) snprintf(list + l, sizeof list - l, "%s%s()%s", l ? ", " : "", nm, f->old_addr ? "" : " (new)");
        /* a call already running keeps its old code */
        for (int k = 0; k < s->nframes; k++) {
            uint32_t pc = s->frames[k].pc - (k ? 4 : 0);
            const dfunc *df = info_func_at(s->di, pc);
            if (df && df->superseded && !strcmp(df->name, f->name) && pc != f->old_addr) {
                size_t m = strlen(running);
                if (m < sizeof running - 40 && !strstr(running, nm)) snprintf(running + m, sizeof running - m, "%s%s()", m ? ", " : "", nm);
                break;
            }
        }
    }
    int len = snprintf(msg, msglen, "Applied the changes to %s: %s.", path_base(source), list);
    if (running[0] && len > 0 && (size_t)len < msglen)
        len += snprintf(msg + len, msglen - (size_t)len, " %s %s running: the current call finishes in the old code, the next call uses the new code.",
                        running, strchr(running, ',') ? "are" : "is");
    if (skipped[0] && len > 0 && (size_t)len < msglen)
        len += snprintf(msg + len, msglen - (size_t)len, " %s too small to redirect: still the old code.", skipped);
    if (!have_info && len > 0 && (size_t)len < msglen)
        len += snprintf(msg + len, msglen - (size_t)len, " (No debug info for the new code: %s.)", err);
    if (p.warn[0] && len > 0 && (size_t)len < msglen)
        snprintf(msg + len, msglen - (size_t)len, " Note: %s.", p.warn);
    trace(s, "hotpatch: %d changed, %d new, %d stubs, %lu bytes at %08lx", nchanged, nnew, p.nstubs, (unsigned long)p.mem_size, (unsigned long)base);
    vec_free(&reds);
    hp_patch_free(&p);
    hp_elf_free(obj);
    return 1;
}
