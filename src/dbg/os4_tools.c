/*
 * os4_tools.c — debugtools runtime: resource tracking and the sampling profiler.
 *
 * Resource tracking
 *   exec's and dos's main interfaces are shared by every task, so their methods are patched
 *   with SetMethod (as tools/os4/QEMUCopyFix.c does) while a tracked program runs. A patch
 *   checks first, cheaply, whether the caller is the program's task; everybody else goes
 *   straight to the original. The program's calls are recorded with their return addresses
 *   (the LR and the stack's back chain) into a ring the IDE drains on its poll timer. Calls
 *   made from inside a patched function (dos's Open allocating memory, a library's init code
 *   run by OpenLibrary) aren't recorded: they belong to the OS, not to the program.
 *
 *   malloc/calloc/realloc/free reach newlib.library through the program's own INewlib
 *   interface: the program's stubs are `li r12,<offset>; b __NewlibCall`, and __NewlibCall
 *   loads INewlib and jumps through <offset>. At main() (INewlib is set up by then) those
 *   four slots are patched in the program's interface. If that interface is the IDE's own
 *   one too (shared), the slots are put back when the program has ended; a per-process
 *   interface goes away with the program.
 *
 *   The patch code and the ring are in the IDE's own image (static), so a patch can never
 *   reach freed memory while AmiForge runs; everything is unpatched when the program ends,
 *   and on unload/destroy.
 *
 * Profiler
 *   A process at priority 10 wakes every interval_us on timer.device and reads the program
 *   task's context with IDebug->ReadTaskContext (the task can't be running then: we are, and
 *   there is one CPU). It records the IP, the LR and up to 6 return addresses from the back
 *   chain, and whether the task was waiting. The launcher switches sampling off (under a
 *   mutex) as soon as RunCommand returns, so the sampler never touches a task that is
 *   going away.
 *
 * Both rings are single-producer (the program's task / the sampler) single-consumer (the
 * IDE task) on one CPU: the producer fills a slot, then publishes it by bumping `head`.
 */
#undef __USE_INLINE__
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/interfaces.h>
#include <exec/tasks.h>
#include <exec/debug.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "os4_tools.h"

#define RES_RING   8192          /* events */
#define SAMPLE_RING 8192

/* ======================================================================== shared state */

typedef struct {
    /* resource tracking (written by the program's task) */
    volatile int active;              /* between RunCommand's start and end */
    struct Task *volatile task;
    int depth;                        /* inside a patched call: don't record nested ones */
    uint32_t seq;
    volatile uint32_t head, tail, lost;
    dbg_res_event ring[RES_RING];
} tracker;

typedef struct {
    struct Task *volatile task;       /* sampling target; NULL = don't sample */
    volatile int quit;
    volatile int running;             /* the sampler process is alive */
    uint32_t interval;
    struct DebugIFace *idebug;
    volatile uint32_t *exc_count;     /* the session's hook counter: skip trapped states */
    APTR mutex;                       /* held while a sample is taken */
    volatile uint32_t head, tail, lost;
    dbg_sample ring[SAMPLE_RING];
} sampler;

static tracker g_tr;
static sampler g_sp;
static dbg_session *g_owner;          /* the session the patches / sampler belong to */

typedef struct {
    int track, profile;
    uint32_t interval;
    int patched;                      /* exec/dos patches installed */
    int newlib_patched, newlib_shared;
    struct Interface *newlib;         /* the program's INewlib, while patched */
    uint32_t newlib_off[4];           /* malloc, calloc, realloc, free (0 = not patched) */
    APTR newlib_old[4];
    int sampler_started;
    char note[160];
} dt_state;

static dt_state *dt_get(dbg_session *s, int create)
{
    void **p = dbg__dt(s);
    if (!*p && create) *p = calloc(1, sizeof(dt_state));
    return (dt_state *)*p;
}

/* ======================================================================== recording */

static inline int mine(void)
{
    return g_tr.active && g_tr.depth == 0 && IExec->FindTask(NULL) == g_tr.task;
}

/* The caller's return address and the back chain above it (frame = the patch's r1). */
static void callers_from(uint32_t lr, uint32_t frame, uint32_t *out, int max)
{
    struct Task *t = g_tr.task;
    uint32_t lo = (uint32_t)t->tc_SPLower, hi = (uint32_t)t->tc_SPUpper;
    int n = 0;
    out[n++] = lr;
    uint32_t f = frame >= lo && frame < hi ? *(uint32_t *)frame : 0;   /* the caller's frame */
    while (n < max && f >= lo && f + 8 <= hi && !(f & 3)) {
        uint32_t next = *(uint32_t *)f;
        if (next <= f || next + 8 > hi || (next & 3)) break;
        uint32_t ret = *(uint32_t *)(next + 4);
        if (!ret) break;
        out[n++] = ret;
        f = next;
    }
    while (n < max) out[n++] = 0;
}

static void rec(int kind, int is_free, int fn, uint32_t addr, uint32_t size, uint32_t lr, uint32_t frame)
{
    uint32_t h = g_tr.head;
    if (h - g_tr.tail >= RES_RING) { g_tr.lost++; return; }
    dbg_res_event *e = &g_tr.ring[h % RES_RING];
    e->kind = (uint8_t)kind;
    e->is_free = (uint8_t)is_free;
    e->fn = (uint16_t)fn;
    e->addr = addr;
    e->size = size;
    e->seq = ++g_tr.seq;
    callers_from(lr, frame, e->callers, DBG_RES_CALLERS);
    __asm__ volatile("sync" ::: "memory");
    g_tr.head = h + 1;
}

#define LR() ((uint32_t)__builtin_return_address(0))
#define FR() ((uint32_t)__builtin_frame_address(0))

/* ======================================================================== exec patches */

typedef APTR (APICALL *t_AllocVec)(struct ExecIFace *, ULONG, ULONG);
typedef APTR (APICALL *t_AllocVecTagList)(struct ExecIFace *, uint32, const struct TagItem *);
typedef void (APICALL *t_FreeVec)(struct ExecIFace *, APTR);
typedef APTR (APICALL *t_AllocMem)(struct ExecIFace *, ULONG, ULONG);
typedef void (APICALL *t_FreeMem)(struct ExecIFace *, APTR, ULONG);
typedef APTR (APICALL *t_AllocSysObject)(struct ExecIFace *, ULONG, const struct TagItem *);
typedef void (APICALL *t_FreeSysObject)(struct ExecIFace *, ULONG, APTR);
typedef APTR (APICALL *t_CreatePool)(struct ExecIFace *, ULONG, ULONG, ULONG);
typedef void (APICALL *t_DeletePool)(struct ExecIFace *, APTR);
typedef struct Library *(APICALL *t_OpenLibrary)(struct ExecIFace *, CONST_STRPTR, ULONG);
typedef APTR (APICALL *t_CloseLibrary)(struct ExecIFace *, struct Library *);
typedef struct Interface *(APICALL *t_GetInterface)(struct ExecIFace *, struct Library *, CONST_STRPTR, ULONG, const struct TagItem *);
typedef void (APICALL *t_DropInterface)(struct ExecIFace *, struct Interface *);

static t_AllocVec o_AllocVec;
static t_AllocVecTagList o_AllocVecTagList;
static APTR o_AllocVecTags;
static t_FreeVec o_FreeVec;
static t_AllocMem o_AllocMem;
static t_FreeMem o_FreeMem;
static t_AllocSysObject o_AllocSysObject;
static APTR o_AllocSysObjectTags;
static t_FreeSysObject o_FreeSysObject;
static t_CreatePool o_CreatePool;
static t_DeletePool o_DeletePool;
static t_OpenLibrary o_OpenLibrary;
static t_CloseLibrary o_CloseLibrary;
static t_GetInterface o_GetInterface;
static t_DropInterface o_DropInterface;

static APTR APICALL p_AllocVec(struct ExecIFace *self, ULONG size, ULONG attr)
{
    if (!mine()) return o_AllocVec(self, size, attr);
    g_tr.depth++;
    APTR r = o_AllocVec(self, size, attr);
    g_tr.depth--;
    if (r) rec(DBG_RES_VEC, 0, DBG_FN_ALLOCVEC, (uint32_t)r, size, LR(), FR());
    return r;
}

static APTR APICALL p_AllocVecTagList(struct ExecIFace *self, uint32 size, const struct TagItem *tags)
{
    if (!mine()) return o_AllocVecTagList(self, size, tags);
    g_tr.depth++;
    APTR r = o_AllocVecTagList(self, size, tags);
    g_tr.depth--;
    if (r) rec(DBG_RES_VEC, 0, DBG_FN_ALLOCVECTAGLIST, (uint32_t)r, size, LR(), FR());
    return r;
}

/* The varargs versions hand their (linear) tag list to the original TagList function. */
static APTR APICALL VARARGS68K p_AllocVecTags(struct ExecIFace *self, uint32 size, ...)
{
    va_list ap;
    va_startlinear(ap, size);
    const struct TagItem *tags = va_getlinearva(ap, const struct TagItem *);
    APTR r;
    if (!mine()) r = o_AllocVecTagList(self, size, tags);
    else {
        g_tr.depth++;
        r = o_AllocVecTagList(self, size, tags);
        g_tr.depth--;
        if (r) rec(DBG_RES_VEC, 0, DBG_FN_ALLOCVECTAGS, (uint32_t)r, size, LR(), FR());
    }
    va_end(ap);
    return r;
}

static void APICALL p_FreeVec(struct ExecIFace *self, APTR mem)
{
    if (!mine()) { o_FreeVec(self, mem); return; }
    if (mem) rec(DBG_RES_VEC, 1, DBG_FN_FREEVEC, (uint32_t)mem, 0, LR(), FR());
    g_tr.depth++;
    o_FreeVec(self, mem);
    g_tr.depth--;
}

static APTR APICALL p_AllocMem(struct ExecIFace *self, ULONG size, ULONG attr)
{
    if (!mine()) return o_AllocMem(self, size, attr);
    g_tr.depth++;
    APTR r = o_AllocMem(self, size, attr);
    g_tr.depth--;
    if (r) rec(DBG_RES_MEM, 0, DBG_FN_ALLOCMEM, (uint32_t)r, size, LR(), FR());
    return r;
}

static void APICALL p_FreeMem(struct ExecIFace *self, APTR mem, ULONG size)
{
    if (!mine()) { o_FreeMem(self, mem, size); return; }
    if (mem) rec(DBG_RES_MEM, 1, DBG_FN_FREEMEM, (uint32_t)mem, size, LR(), FR());
    g_tr.depth++;
    o_FreeMem(self, mem, size);
    g_tr.depth--;
}

static APTR APICALL p_AllocSysObject(struct ExecIFace *self, ULONG type, const struct TagItem *tags)
{
    if (!mine()) return o_AllocSysObject(self, type, tags);
    g_tr.depth++;
    APTR r = o_AllocSysObject(self, type, tags);
    g_tr.depth--;
    if (r) rec(DBG_RES_SYSOBJ, 0, DBG_FN_ALLOCSYSOBJECT, (uint32_t)r, type, LR(), FR());
    return r;
}

static APTR APICALL VARARGS68K p_AllocSysObjectTags(struct ExecIFace *self, ULONG type, ...)
{
    va_list ap;
    va_startlinear(ap, type);
    const struct TagItem *tags = va_getlinearva(ap, const struct TagItem *);
    APTR r;
    if (!mine()) r = o_AllocSysObject(self, type, tags);
    else {
        g_tr.depth++;
        r = o_AllocSysObject(self, type, tags);
        g_tr.depth--;
        if (r) rec(DBG_RES_SYSOBJ, 0, DBG_FN_ALLOCSYSOBJECTTAGS, (uint32_t)r, type, LR(), FR());
    }
    va_end(ap);
    return r;
}

static void APICALL p_FreeSysObject(struct ExecIFace *self, ULONG type, APTR obj)
{
    if (!mine()) { o_FreeSysObject(self, type, obj); return; }
    if (obj) rec(DBG_RES_SYSOBJ, 1, DBG_FN_FREESYSOBJECT, (uint32_t)obj, type, LR(), FR());
    g_tr.depth++;
    o_FreeSysObject(self, type, obj);
    g_tr.depth--;
}

static APTR APICALL p_CreatePool(struct ExecIFace *self, ULONG flags, ULONG puddle, ULONG thresh)
{
    if (!mine()) return o_CreatePool(self, flags, puddle, thresh);
    g_tr.depth++;
    APTR r = o_CreatePool(self, flags, puddle, thresh);
    g_tr.depth--;
    if (r) rec(DBG_RES_POOL, 0, DBG_FN_CREATEPOOL, (uint32_t)r, puddle, LR(), FR());
    return r;
}

static void APICALL p_DeletePool(struct ExecIFace *self, APTR pool)
{
    if (!mine()) { o_DeletePool(self, pool); return; }
    if (pool) rec(DBG_RES_POOL, 1, DBG_FN_DELETEPOOL, (uint32_t)pool, 0, LR(), FR());
    g_tr.depth++;
    o_DeletePool(self, pool);
    g_tr.depth--;
}

static struct Library *APICALL p_OpenLibrary(struct ExecIFace *self, CONST_STRPTR name, ULONG version)
{
    if (!mine()) return o_OpenLibrary(self, name, version);
    g_tr.depth++;
    struct Library *r = o_OpenLibrary(self, name, version);
    g_tr.depth--;
    if (r) rec(DBG_RES_LIBRARY, 0, DBG_FN_OPENLIBRARY, (uint32_t)r, version, LR(), FR());
    return r;
}

static APTR APICALL p_CloseLibrary(struct ExecIFace *self, struct Library *lib)
{
    if (!mine()) return o_CloseLibrary(self, lib);
    if (lib) rec(DBG_RES_LIBRARY, 1, DBG_FN_CLOSELIBRARY, (uint32_t)lib, 0, LR(), FR());
    g_tr.depth++;
    APTR r = o_CloseLibrary(self, lib);
    g_tr.depth--;
    return r;
}

static struct Interface *APICALL p_GetInterface(struct ExecIFace *self, struct Library *lib, CONST_STRPTR name,
                                                ULONG version, const struct TagItem *tags)
{
    if (!mine()) return o_GetInterface(self, lib, name, version, tags);
    g_tr.depth++;
    struct Interface *r = o_GetInterface(self, lib, name, version, tags);
    g_tr.depth--;
    if (r) rec(DBG_RES_INTERFACE, 0, DBG_FN_GETINTERFACE, (uint32_t)r, version, LR(), FR());
    return r;
}

static void APICALL p_DropInterface(struct ExecIFace *self, struct Interface *iface)
{
    if (!mine()) { o_DropInterface(self, iface); return; }
    if (iface) rec(DBG_RES_INTERFACE, 1, DBG_FN_DROPINTERFACE, (uint32_t)iface, 0, LR(), FR());
    g_tr.depth++;
    o_DropInterface(self, iface);
    g_tr.depth--;
}

/* ======================================================================== dos patches */

typedef BPTR (APICALL *t_Open)(struct DOSIFace *, CONST_STRPTR, LONG);
typedef BPTR (APICALL *t_FOpen)(struct DOSIFace *, CONST_STRPTR, LONG, LONG);
typedef BPTR (APICALL *t_OpenFromLock)(struct DOSIFace *, BPTR);
typedef LONG (APICALL *t_Close)(struct DOSIFace *, BPTR);
typedef BPTR (APICALL *t_Lock)(struct DOSIFace *, CONST_STRPTR, LONG);
typedef BPTR (APICALL *t_Lock1)(struct DOSIFace *, BPTR);
typedef BPTR (APICALL *t_CreateDir)(struct DOSIFace *, CONST_STRPTR);
typedef VOID (APICALL *t_UnLock)(struct DOSIFace *, BPTR);

static t_Open o_Open;
static t_FOpen o_FOpen;
static t_OpenFromLock o_OpenFromLock;
static t_Close o_Close;
static t_Lock o_Lock;
static t_Lock1 o_DupLock, o_ParentDir, o_DupLockFromFH, o_ParentOfFH;
static t_CreateDir o_CreateDir;
static t_UnLock o_UnLock;

static BPTR APICALL p_Open(struct DOSIFace *self, CONST_STRPTR name, LONG mode)
{
    if (!mine()) return o_Open(self, name, mode);
    g_tr.depth++;
    BPTR r = o_Open(self, name, mode);
    g_tr.depth--;
    if (r) rec(DBG_RES_FILE, 0, DBG_FN_OPEN, (uint32_t)r, 0, LR(), FR());
    return r;
}

static BPTR APICALL p_FOpen(struct DOSIFace *self, CONST_STRPTR name, LONG mode, LONG bufsize)
{
    if (!mine()) return o_FOpen(self, name, mode, bufsize);
    g_tr.depth++;
    BPTR r = o_FOpen(self, name, mode, bufsize);
    g_tr.depth--;
    if (r) rec(DBG_RES_FILE, 0, DBG_FN_FOPEN, (uint32_t)r, 0, LR(), FR());
    return r;
}

/* On success the lock now belongs to the file: the lock is released, a file acquired. */
static BPTR APICALL p_OpenFromLock(struct DOSIFace *self, BPTR lock)
{
    if (!mine()) return o_OpenFromLock(self, lock);
    g_tr.depth++;
    BPTR r = o_OpenFromLock(self, lock);
    g_tr.depth--;
    if (r) {
        rec(DBG_RES_LOCK, 1, DBG_FN_OPENFROMLOCK, (uint32_t)lock, 0, LR(), FR());
        rec(DBG_RES_FILE, 0, DBG_FN_OPENFROMLOCK, (uint32_t)r, 0, LR(), FR());
    }
    return r;
}

static LONG APICALL p_Close(struct DOSIFace *self, BPTR fh)
{
    if (!mine()) return o_Close(self, fh);
    if (fh) rec(DBG_RES_FILE, 1, DBG_FN_CLOSE, (uint32_t)fh, 0, LR(), FR());
    g_tr.depth++;
    LONG r = o_Close(self, fh);
    g_tr.depth--;
    return r;
}

static BPTR APICALL p_Lock(struct DOSIFace *self, CONST_STRPTR name, LONG type)
{
    if (!mine()) return o_Lock(self, name, type);
    g_tr.depth++;
    BPTR r = o_Lock(self, name, type);
    g_tr.depth--;
    if (r) rec(DBG_RES_LOCK, 0, DBG_FN_LOCK, (uint32_t)r, 0, LR(), FR());
    return r;
}

#define LOCK1(NAME, FN) \
static BPTR APICALL p_##NAME(struct DOSIFace *self, BPTR arg) \
{ \
    if (!mine()) return o_##NAME(self, arg); \
    g_tr.depth++; \
    BPTR r = o_##NAME(self, arg); \
    g_tr.depth--; \
    if (r) rec(DBG_RES_LOCK, 0, FN, (uint32_t)r, 0, LR(), FR()); \
    return r; \
}
LOCK1(DupLock, DBG_FN_DUPLOCK)
LOCK1(ParentDir, DBG_FN_PARENTDIR)
LOCK1(DupLockFromFH, DBG_FN_DUPLOCKFROMFH)
LOCK1(ParentOfFH, DBG_FN_PARENTOFFH)

static BPTR APICALL p_CreateDir(struct DOSIFace *self, CONST_STRPTR name)
{
    if (!mine()) return o_CreateDir(self, name);
    g_tr.depth++;
    BPTR r = o_CreateDir(self, name);
    g_tr.depth--;
    if (r) rec(DBG_RES_LOCK, 0, DBG_FN_CREATEDIR, (uint32_t)r, 0, LR(), FR());
    return r;
}

static VOID APICALL p_UnLock(struct DOSIFace *self, BPTR lock)
{
    if (!mine()) { o_UnLock(self, lock); return; }
    if (lock) rec(DBG_RES_LOCK, 1, DBG_FN_UNLOCK, (uint32_t)lock, 0, LR(), FR());
    g_tr.depth++;
    o_UnLock(self, lock);
    g_tr.depth--;
}

/* ======================================================================== newlib patches */

/* newlib's interface methods take the arguments as they are (no Self). */
typedef void *(*t_malloc)(size_t);
typedef void *(*t_calloc)(size_t, size_t);
typedef void *(*t_realloc)(void *, size_t);
typedef void (*t_free)(void *);
static t_malloc o_malloc;
static t_calloc o_calloc;
static t_realloc o_realloc;
static t_free o_free;

static void *p_malloc(size_t n)
{
    if (!mine()) return o_malloc(n);
    g_tr.depth++;
    void *r = o_malloc(n);
    g_tr.depth--;
    if (r) rec(DBG_RES_MALLOC, 0, DBG_FN_MALLOC, (uint32_t)r, n, LR(), FR());
    return r;
}

static void *p_calloc(size_t n, size_t sz)
{
    if (!mine()) return o_calloc(n, sz);
    g_tr.depth++;
    void *r = o_calloc(n, sz);
    g_tr.depth--;
    if (r) rec(DBG_RES_MALLOC, 0, DBG_FN_CALLOC, (uint32_t)r, n * sz, LR(), FR());
    return r;
}

static void *p_realloc(void *p, size_t n)
{
    if (!mine()) return o_realloc(p, n);
    g_tr.depth++;
    void *r = o_realloc(p, n);
    g_tr.depth--;
    if (r || n == 0) {                /* failure leaves p allocated */
        if (p) rec(DBG_RES_MALLOC, 1, DBG_FN_REALLOC, (uint32_t)p, 0, LR(), FR());
        if (r) rec(DBG_RES_MALLOC, 0, DBG_FN_REALLOC, (uint32_t)r, n, LR(), FR());
    }
    return r;
}

static void p_free(void *p)
{
    if (!mine()) { o_free(p); return; }
    if (p) rec(DBG_RES_MALLOC, 1, DBG_FN_FREE, (uint32_t)p, 0, LR(), FR());
    g_tr.depth++;
    o_free(p);
    g_tr.depth--;
}

/* ======================================================================== patching */

typedef struct { struct Interface **iface; uint32_t off; APTR patch; APTR *orig; } patch_def;

static struct Interface *g_iexec, *g_idos;

/* AllocVec, AllocMem, CreatePool... are deprecated, but programs still call them */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define EP(m) { &g_iexec, offsetof(struct ExecIFace, m), (APTR)p_##m, (APTR *)&o_##m }
#define DP(m) { &g_idos, offsetof(struct DOSIFace, m), (APTR)p_##m, (APTR *)&o_##m }
static const patch_def patches[] = {
    EP(AllocVec), EP(AllocVecTagList), EP(AllocVecTags), EP(FreeVec), EP(AllocMem), EP(FreeMem),
    EP(AllocSysObject), EP(AllocSysObjectTags), EP(FreeSysObject), EP(CreatePool), EP(DeletePool),
    EP(OpenLibrary), EP(CloseLibrary), EP(GetInterface), EP(DropInterface),
    DP(Open), DP(FOpen), DP(OpenFromLock), DP(Close), DP(Lock), DP(DupLock), DP(ParentDir), DP(CreateDir),
    DP(DupLockFromFH), DP(ParentOfFH), DP(UnLock)
};
#pragma GCC diagnostic pop
#define NPATCH (sizeof patches / sizeof patches[0])

static void install_patches(void)
{
    g_iexec = (struct Interface *)IExec;
    g_idos = (struct Interface *)IDOS;
    /* the originals first, so a patch never sees a NULL original */
    IExec->Forbid();
    for (size_t i = 0; i < NPATCH; i++) {
        const patch_def *p = &patches[i];
        *p->orig = *(APTR *)((uint8_t *)*p->iface + p->off);
    }
    for (size_t i = 0; i < NPATCH; i++) {
        const patch_def *p = &patches[i];
        APTR old = IExec->SetMethod(*p->iface, p->off, p->patch);
        if (old != *p->orig) *p->orig = old;
    }
    IExec->Permit();
}

/* Puts the originals back. If something patched over us meanwhile, its patch stays (it
 * chains to ours, which keeps working while AmiForge runs). */
static void remove_patches(void)
{
    IExec->Forbid();
    for (size_t i = 0; i < NPATCH; i++) {
        const patch_def *p = &patches[i];
        APTR cur = IExec->SetMethod(*p->iface, p->off, *p->orig);
        if (cur != p->patch) IExec->SetMethod(*p->iface, p->off, cur);
    }
    IExec->Permit();
}

/* malloc & co.: "li r12,off; b __NewlibCall", and __NewlibCall "lis r11,hi; lwz r0,lo(r11); ..." */
static int newlib_slot(dbg_session *s, const char *name, uint32_t *var, uint32_t *off)
{
    uint32_t a = info_symbol_addr(dbg__info(s), name);
    if (!a) return 0;
    const uint32_t *p = (const uint32_t *)a;
    if ((p[0] & 0xFFFF0000u) != 0x39800000u) return -1;          /* li r12,imm */
    if ((p[1] & 0xFC000003u) != 0x48000000u) return -1;          /* b */
    int32_t d = (int32_t)(p[1] & 0x03FFFFFCu);
    if (d & 0x02000000) d -= 0x04000000;
    const uint32_t *c = (const uint32_t *)(a + 4 + d);
    if ((c[0] & 0xFFFF0000u) != 0x3D600000u) return -1;          /* lis r11,hi */
    if ((c[1] & 0xFFFF0000u) != 0x800B0000u) return -1;          /* lwz r0,lo(r11) */
    *var = (c[0] << 16) + (uint32_t)(int32_t)(int16_t)(c[1] & 0xFFFF);
    *off = (uint32_t)(int32_t)(int16_t)(p[0] & 0xFFFF);
    return 1;
}

extern struct Interface *INewlib;     /* the IDE's own */

static void patch_newlib(dbg_session *s, dt_state *d)
{
    static const char *const names[4] = { "malloc", "calloc", "realloc", "free" };
    static APTR *const origs[4] = { (APTR *)&o_malloc, (APTR *)&o_calloc, (APTR *)&o_realloc, (APTR *)&o_free };
    static const APTR mine_[4] = { (APTR)p_malloc, (APTR)p_calloc, (APTR)p_realloc, (APTR)p_free };
    uint32_t var = 0, off[4] = { 0 };
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t v, o;
        int r = newlib_slot(s, names[i], &v, &o);
        if (r < 0) bad = 1;
        if (r <= 0) continue;
        if (var && v != var) { bad = 1; continue; }
        var = v; off[i] = o;
    }
    if (!var || !off[0] || !off[3]) {
        snprintf(d->note, sizeof d->note, bad ? "malloc/free not tracked: unexpected newlib stubs"
                                              : "malloc/free not tracked: the program doesn't call them");
        return;
    }
    struct Interface *iface = *(struct Interface **)var;
    if (!iface) { snprintf(d->note, sizeof d->note, "malloc/free not tracked: INewlib isn't set up"); return; }
    d->newlib = iface;
    d->newlib_shared = iface == INewlib;
    IExec->Forbid();
    for (int i = 0; i < 4; i++) {
        if (!off[i]) continue;
        *origs[i] = *(APTR *)((uint8_t *)iface + off[i]);
        d->newlib_old[i] = *origs[i];
        d->newlib_off[i] = off[i];
    }
    for (int i = 0; i < 4; i++)
        if (off[i]) IExec->SetMethod(iface, off[i], mine_[i]);
    IExec->Permit();
    d->newlib_patched = 1;
    d->note[0] = 0;
}

static void unpatch_newlib(dt_state *d)
{
    if (!d->newlib_patched) return;
    /* a per-program interface is gone (or going) with the program: leave it alone */
    if (d->newlib_shared) {
        IExec->Forbid();
        for (int i = 0; i < 4; i++)
            if (d->newlib_off[i]) IExec->SetMethod(d->newlib, d->newlib_off[i], d->newlib_old[i]);
        IExec->Permit();
    }
    d->newlib_patched = 0;
    d->newlib = NULL;
}

/* ======================================================================== sampler */

static int32 sampler_main(STRPTR args, int32 len, struct ExecBase *sysbase)
{
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    struct TimeRequest *tr = port ? IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                              ASOIOR_ReplyPort, port, TAG_DONE) : NULL;
    int ok = tr && !IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)tr, 0);
    while (ok && !g_sp.quit) {
        tr->Request.io_Command = TR_ADDREQUEST;
        tr->Time.Seconds = 0;
        tr->Time.Microseconds = g_sp.interval;
        IExec->DoIO((struct IORequest *)tr);
        IExec->MutexObtain(g_sp.mutex);
        struct Task *t = g_sp.task;
        uint32_t exc = *g_sp.exc_count;
        if (t) {
            uint8 st = t->tc_State;
            if ((st == TS_READY || st == TS_WAIT) && exc == *g_sp.exc_count) {
                struct ExceptionContext c;
                g_sp.idebug->ReadTaskContext(t, &c, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
                uint32_t h = g_sp.head;
                if (h - g_sp.tail >= SAMPLE_RING) g_sp.lost++;
                else {
                    dbg_sample *smp = &g_sp.ring[h % SAMPLE_RING];
                    smp->ip = c.ip;
                    smp->state = st == TS_WAIT ? DBG_SAMPLE_WAITING : DBG_SAMPLE_RUNNING;
                    uint32_t lo = (uint32_t)t->tc_SPLower, hi = (uint32_t)t->tc_SPUpper;
                    int n = 0;
                    smp->callers[n++] = c.lr;
                    uint32_t f = c.gpr[1];
                    while (n < DBG_SAMPLE_CALLERS && f >= lo && f + 8 <= hi && !(f & 3)) {
                        uint32_t next = *(uint32_t *)f;
                        if (next <= f || next + 8 > hi || (next & 3)) break;
                        uint32_t ret = *(uint32_t *)(next + 4);
                        if (!ret) break;
                        smp->callers[n++] = ret;
                        f = next;
                    }
                    smp->ncallers = (uint8_t)n;
                    __asm__ volatile("sync" ::: "memory");
                    g_sp.head = h + 1;
                }
            }
        }
        IExec->MutexRelease(g_sp.mutex);
    }
    if (ok) IExec->CloseDevice((struct IORequest *)tr);
    if (tr) IExec->FreeSysObject(ASOT_IOREQUEST, tr);
    if (port) IExec->FreeSysObject(ASOT_PORT, port);
    g_sp.running = 0;
    return 0;
}

static void start_sampler(dbg_session *s, dt_state *d)
{
    if (g_sp.running) return;
    if (!g_sp.mutex) g_sp.mutex = IExec->AllocSysObjectTags(ASOT_MUTEX, TAG_DONE);
    if (!g_sp.mutex) return;
    g_sp.quit = 0;
    g_sp.head = g_sp.tail = g_sp.lost = 0;
    g_sp.task = NULL;                 /* set by dt_program_starting */
    g_sp.interval = d->interval ? d->interval : 1000;
    g_sp.idebug = dbg__idebug(s);
    g_sp.exc_count = dbg__exc_count(s);
    g_sp.running = 1;
    struct Process *p = IDOS->CreateNewProcTags(NP_Entry, sampler_main, NP_Name, "AmiForge profiler",
                                                NP_Priority, 10, TAG_DONE);
    if (!p) { g_sp.running = 0; return; }
    d->sampler_started = 1;
}

/* A short sleep on timer.device (Delay(1) is best avoided with newlib, see CLAUDE.md). */
static void sleep_us(uint32_t us)
{
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    struct TimeRequest *tr = port ? IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                              ASOIOR_ReplyPort, port, TAG_DONE) : NULL;
    if (tr && !IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
        tr->Request.io_Command = TR_ADDREQUEST;
        tr->Time.Seconds = us / 1000000;
        tr->Time.Microseconds = us % 1000000;
        IExec->DoIO((struct IORequest *)tr);
        IExec->CloseDevice((struct IORequest *)tr);
    }
    if (tr) IExec->FreeSysObject(ASOT_IOREQUEST, tr);
    if (port) IExec->FreeSysObject(ASOT_PORT, port);
}

static void stop_sampler(dt_state *d)
{
    if (!d->sampler_started) return;
    if (g_sp.mutex) { IExec->MutexObtain(g_sp.mutex); g_sp.task = NULL; IExec->MutexRelease(g_sp.mutex); }
    g_sp.quit = 1;
    for (int i = 0; i < 200 && g_sp.running; i++) sleep_us(10000);
    d->sampler_started = 0;
}

/* ======================================================================== session hooks */

int dt_wants_main(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    return d && d->track;
}

void dt_before_go(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    if (!d) return;
    d->note[0] = 0;
    if (d->track && !d->patched && (!g_owner || g_owner == s)) {
        g_tr.head = g_tr.tail = g_tr.lost = 0;
        g_tr.seq = 0;
        g_tr.depth = 0;
        g_tr.task = dbg__proc(s);
        g_tr.active = 0;              /* the launcher switches it on around RunCommand */
        install_patches();
        d->patched = 1;
        g_owner = s;
    }
    if (d->profile && (!g_owner || g_owner == s)) {
        start_sampler(s, d);
        g_owner = s;
    }
}

void dt_program_starting(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    if (!d) return;
    struct Task *me = IExec->FindTask(NULL);
    if (d->patched) { g_tr.task = me; g_tr.depth = 0; g_tr.active = 1; }
    if (d->sampler_started) g_sp.task = me;
}

void dt_program_returned(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    if (!d) return;
    g_tr.active = 0;
    if (d->sampler_started && g_sp.mutex) {
        IExec->MutexObtain(g_sp.mutex);   /* the sampler isn't looking at us after this */
        g_sp.task = NULL;
        IExec->MutexRelease(g_sp.mutex);
    }
}

void dt_at_main(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    if (d && d->patched && !d->newlib_patched) patch_newlib(s, d);
}

void dt_after_exit(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    if (!d) return;
    g_tr.active = 0;
    if (d->patched) { remove_patches(); d->patched = 0; }
    unpatch_newlib(d);
    stop_sampler(d);
    if (g_owner == s) g_owner = NULL;
}

void dt_stop(dbg_session *s) { dt_after_exit(s); }

void dt_free(dbg_session *s)
{
    void **p = dbg__dt(s);
    free(*p);
    *p = NULL;
}

/* ======================================================================== public API */

int dbg_track_resources(dbg_session *s, int on)
{
    dt_state *d = dt_get(s, 1);
    if (!d) return 0;
    d->track = on;
    return 1;
}

int dbg_resource_events(dbg_session *s, dbg_res_event *out, int max)
{
    if (g_owner && g_owner != s) return 0;
    int n = 0;
    uint32_t h = g_tr.head;
    __asm__ volatile("sync" ::: "memory");
    while (g_tr.tail != h && n < max) {
        out[n++] = g_tr.ring[g_tr.tail % RES_RING];
        g_tr.tail++;
    }
    return n;
}

uint32_t dbg_resources_lost(dbg_session *s) { (void)s; return g_tr.lost; }

const char *dbg_resources_note(dbg_session *s)
{
    dt_state *d = dt_get(s, 0);
    return d ? d->note : "";
}

int dbg_profile(dbg_session *s, int on, uint32_t interval_us)
{
    dt_state *d = dt_get(s, 1);
    if (!d) return 0;
    d->profile = on;
    d->interval = interval_us < 200 ? 200 : interval_us;
    return 1;
}

int dbg_profile_samples(dbg_session *s, dbg_sample *out, int max)
{
    if (g_owner && g_owner != s) return 0;
    int n = 0;
    uint32_t h = g_sp.head;
    __asm__ volatile("sync" ::: "memory");
    while (g_sp.tail != h && n < max) {
        out[n++] = g_sp.ring[g_sp.tail % SAMPLE_RING];
        g_sp.tail++;
    }
    return n;
}

uint32_t dbg_profile_lost(dbg_session *s) { (void)s; return g_sp.lost; }

int dbg_addr_in_program(dbg_session *s, uint32_t addr)
{
    dbg_info *di = dbg__info(s);
    return di && info_in_program(di, addr);
}

int dbg_addr_module(dbg_session *s, uint32_t addr, char *buf, int n)
{
    struct DebugIFace *idebug = dbg__idebug(s);
    if (!idebug || n <= 0) return 0;
    struct DebugSymbol *ds = idebug->ObtainDebugSymbol((CONST_APTR)addr, NULL);
    if (!ds) return 0;
    const char *mod = ds->Name ? path_base(ds->Name) : "?";
    if (ds->SourceFunctionName && *ds->SourceFunctionName) snprintf(buf, n, "%s: %s", mod, ds->SourceFunctionName);
    else snprintf(buf, n, "%s", mod);
    idebug->ReleaseDebugSymbol(ds);
    return 1;
}

void *dbg_task(dbg_session *s) { return dbg__proc(s); }
