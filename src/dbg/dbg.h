/*
 * dbg — source-level debugger core for AmigaOS 4.1 PPC programs.
 *
 * Runs inside the IDE's own process. It loads the program with LoadSeg and starts it in a
 * small launcher process (created suspended, with an exec debug hook) that runs the seglist
 * with RunCommand, the way a shell does. Breakpoints are `trap` instructions written into
 * the program's code (code pages are made writable with IMMU around the store); a trap
 * enters the debug hook, which suspends the task and bumps a counter the IDE polls.
 *
 * Debug info comes from the program's own ELF file: stabs (-gstabs / -gstabs+) for lines,
 * functions, scopes, locals, parameters, globals and types; .symtab for everything else.
 * Link-time addresses are mapped to load addresses section by section using
 * GetSegListInfo(GSLI_ElfHandle) + GetSection().
 *
 * Single-stepping is done in software: the instruction at the PC is decoded and temporary
 * traps go on its possible successors. Traps are only ever written into the debuggee's own
 * code — never into libraries or the kernel, which other tasks run. A step that would leave
 * the program's code (a call into a library through its interface) is turned into a
 * breakpoint on the return address.
 *
 * The IDE drives it from its event loop:
 *     Wait(... | dbg_signal_mask(s));
 *     while (dbg_poll(s, &ev)) handle(ev);
 * dbg_signal_mask is a timer that ticks while the program runs; dbg_poll can also be
 * called at any other time. All dbg_* calls are made from the IDE task. Nothing here
 * blocks for long (a few tens of milliseconds at most). See docs/debugger-notes.md for the
 * OS behaviour this depends on.
 */
#ifndef AF_DBG_H
#define AF_DBG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dbg_session dbg_session;

typedef enum {
    DBG_NONE,       /* nothing loaded */
    DBG_LOADED,     /* program loaded, not started (breakpoints can be set) */
    DBG_RUNNING,
    DBG_STOPPED,    /* suspended: inspect, then continue/step */
    DBG_EXITED
} dbg_state;

typedef enum {
    DBG_STOP_BREAKPOINT,  /* a user breakpoint (bp_id says which) */
    DBG_STOP_STEP,        /* a step / step over / step out / run-to-cursor finished */
    DBG_STOP_ENTRY,       /* stopped at main() (stop_at_main) */
    DBG_STOP_PAUSE,       /* dbg_pause */
    DBG_STOP_EXCEPTION    /* the program crashed: DSI, ISI, alignment, illegal instruction... */
} dbg_stop_reason;

typedef enum {
    DBG_EV_STOPPED,
    DBG_EV_EXITED,        /* exit_code valid; the session is back to DBG_LOADED-able state */
    DBG_EV_MESSAGE        /* a diagnostic for the debug log (text) */
} dbg_event_type;

typedef struct {
    dbg_event_type type;
    dbg_stop_reason reason;
    int bp_id;               /* for DBG_STOP_BREAKPOINT */
    uint32_t pc;
    uint32_t trap_type;      /* exception vector (0x300 DSI, 0x400 ISI, 0x600 alignment, 0x700 program...) */
    uint32_t fault_addr;     /* DAR for DSI */
    const char *file;        /* source file (as recorded in stabs, may be relative), or NULL */
    int line;                /* 0 if unknown */
    const char *function;    /* demangled, or NULL */
    int exit_code;
    char text[256];          /* human-readable summary: "Breakpoint 2, main() at hello.c:12" */
} dbg_event;

typedef struct {
    uint32_t gpr[32];
    uint32_t pc, msr, cr, xer, ctr, lr;
    uint32_t dsisr, dar;
    double fpr[32];
    uint64_t fpscr;
} dbg_regs;

typedef struct {
    int level;               /* 0 = innermost */
    uint32_t pc;
    uint32_t sp;             /* r1 of this frame */
    const char *function;    /* demangled; "??" if unknown */
    const char *file;        /* NULL if no line info */
    int line;
    const char *module;      /* "program" or the library / kernel module name (ObtainDebugSymbol) */
} dbg_frame;

typedef struct {
    int id;
    const char *file;        /* as given */
    int line;                /* requested line */
    int actual_line;         /* line it resolved to (first line with code at or after `line`), 0 if unresolved */
    uint32_t addr;           /* 0 if unresolved / not loaded */
    int enabled;
    int hits;
    const char *condition;   /* optional expression; stop only when non-zero (NULL = always) */
} dbg_breakpoint;

/* A variable / expression value. Children (struct members, array elements, pointer
 * targets) are produced lazily with dbg_var_children. Strings are owned by the session
 * and stay valid until the next resume or dbg_var_free. */
typedef struct dbg_var {
    const char *name;        /* "count", "p->next", "[3]" */
    const char *type;        /* "int", "struct node *", "char [16]" */
    const char *value;       /* "42", "0x01234560", "{...}", "\"hello\"", "<unreadable>" */
    uint32_t addr;           /* address of the object, 0 if it lives in a register */
    uint32_t size;
    int has_children;
    int editable;            /* scalar in memory or a register: dbg_var_set works */
    void *priv;
} dbg_var;

typedef struct {
    uint32_t addr;
    uint32_t word;
    char text[96];           /* "lwz r9,8(r31)" */
    const char *symbol;      /* "main+0x1c" or NULL */
    const char *file;        /* the source line that starts at this address, if any */
    int line;
    int is_breakpoint;
} dbg_insn;

/* --- session --- */
dbg_session *dbg_create(void);
void dbg_destroy(dbg_session *s);
uint32_t dbg_signal_mask(dbg_session *s);   /* Wait() on this in the IDE loop */
dbg_state dbg_get_state(dbg_session *s);
const char *dbg_last_error(dbg_session *s);

/* Load the program and its debug info. args is the argument string (no program name).
 * console: NULL for a new CON: window titled after the program, or a DOS file name
 * ("CON:0/0/640/200/Output/AUTO/CLOSE/WAIT", "NIL:", "T:out.txt"). cwd may be NULL. */
int dbg_load(dbg_session *s, const char *path, const char *args, const char *cwd, const char *console);
void dbg_unload(dbg_session *s);   /* kills the program if running */

/* Source search path for resolving stabs file names to real files (";"-separated). */
void dbg_set_source_path(dbg_session *s, const char *paths);
/* Resolve a stabs file name to an existing file; returns NULL if not found. */
const char *dbg_resolve_source(dbg_session *s, const char *stabs_file);

/* --- breakpoints (can be set before load; resolved on load) --- */
int dbg_bp_add(dbg_session *s, const char *file, int line, const char *condition);   /* returns id or -1 */
int dbg_bp_add_function(dbg_session *s, const char *function);
int dbg_bp_add_address(dbg_session *s, uint32_t addr);
int dbg_bp_remove(dbg_session *s, int id);
int dbg_bp_enable(dbg_session *s, int id, int enabled);
int dbg_bp_list(dbg_session *s, dbg_breakpoint *out, int max);

/* --- execution --- */
int dbg_run(dbg_session *s, int stop_at_main);
int dbg_continue(dbg_session *s);
int dbg_step_into(dbg_session *s);        /* source line, into functions that have line info */
int dbg_step_over(dbg_session *s);        /* source line, over calls */
int dbg_step_out(dbg_session *s);         /* until the current function returns */
int dbg_step_instruction(dbg_session *s, int over_calls);
int dbg_run_to(dbg_session *s, const char *file, int line);
int dbg_pause(dbg_session *s);
int dbg_kill(dbg_session *s);

/* Next event, if any (returns 1 and fills ev, or 0). */
int dbg_poll(dbg_session *s, dbg_event *ev);

/* --- inspection (DBG_STOPPED only) --- */
int dbg_get_regs(dbg_session *s, dbg_regs *r);
int dbg_set_reg(dbg_session *s, const char *name, uint32_t value);   /* "r3", "pc", "lr", "ctr" */
int dbg_stack(dbg_session *s, dbg_frame *out, int max);              /* returns count */
int dbg_select_frame(dbg_session *s, int level);                     /* locals/eval use this frame */
int dbg_locals(dbg_session *s, dbg_var ***out);                      /* params then locals; returns count */
int dbg_globals(dbg_session *s, dbg_var ***out);                     /* file-scope variables */
dbg_var *dbg_eval(dbg_session *s, const char *expr);                 /* never NULL: errors come back as value "<error: ...>" */
int dbg_var_children(dbg_session *s, dbg_var *v, dbg_var ***out);
int dbg_var_set(dbg_session *s, dbg_var *v, const char *value);
void dbg_var_free(dbg_session *s, dbg_var **vars, int n);   /* frees the vars and the array */
void dbg_var_release(dbg_session *s, dbg_var *v);            /* frees one var (from dbg_eval) */

int dbg_read_memory(dbg_session *s, uint32_t addr, void *buf, uint32_t len);   /* bytes read; unreadable bytes stop it */
int dbg_write_memory(dbg_session *s, uint32_t addr, const void *buf, uint32_t len);
int dbg_disassemble(dbg_session *s, uint32_t addr, int count, dbg_insn *out);  /* breakpoints shown as the original instruction */

/* Diagnostic trace of the runtime (hits, resumes, trap writes), one line per call. The
 * callback runs on the IDE task. NULL turns it off. */
void dbg_set_trace(dbg_session *s, void (*fn)(void *ud, const char *line), void *ud);

/* --- symbols (work while DBG_LOADED or later) --- */
int dbg_addr_to_line(dbg_session *s, uint32_t addr, const char **file, int *line, const char **function);
uint32_t dbg_line_to_addr(dbg_session *s, const char *file, int line, int *actual_line);
uint32_t dbg_function_addr(dbg_session *s, const char *function);

#ifdef __cplusplus
}
#endif
#endif
