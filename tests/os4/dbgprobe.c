/* dbgprobe — settles the exec debug-interface behaviour the debugger relies on.
 *   dbgprobe <program> <main-link-addr-hex> <text-link-addr-hex>
 * Loads the program suspended with a debug hook, plants traps at main and main+4, and checks:
 * trap delivery (Traptype, ip), task state after the hook returns 1, ip writes via
 * WriteTaskContext being honoured on RestartTask, and the exit code via a DeathMessage. */
#undef __USE_INLINE__   /* explicit IExec->/IDOS-> calls */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/elf.h>
#include <exec/exectags.h>
#include <exec/interrupts.h>
#include <exec/debug.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *dbg_demangle(const char *name, char *buf, size_t n);   /* libafdbg: pulls in libstdc++ */
#include "dbg/dbg.h"
#include "dbg/dbg_int.h"
struct KDM { uint32 type; union { struct ExceptionContext *ctx; struct Library *lib; } u; };

static void logf_(const char *fmt, ...) __attribute__((format(printf,1,2)));
#include <stdarg.h>
static void logf_(const char *fmt, ...)
{
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    printf("%s", buf); fflush(stdout);
    BPTR fh = IDOS->Open("DH1:AmiForge/probe.log", MODE_READWRITE);
    if (fh) { IDOS->ChangeFilePosition(fh, 0, OFFSET_END); IDOS->Write(fh, buf, strlen(buf)); IDOS->Close(fh); }
}
#define printf logf_
static int stage = 3, variant = 0;
static uint32 seen;
static struct Interrupt *softint;
static struct DebugIFace *IDbg;
static struct MMUIFace *IMmu;
static struct Task *me, *target;
static volatile uint32 hits, remtask, last_type;
static struct ExceptionContext copy;
static uint32 ring_type[8], ring_ip[8], ring_dar[8], ring_msr[8];
static struct ExceptionContext *ctxptr;
static int8 sigbit;

static uint32 soft_fn(struct ExceptionContext *c, struct ExecBase *sb, APTR data)
{
    IExec->Signal(me, 1UL << sigbit);
    return 0;
}

/* stage 13: a helper process creates the debuggee and attaches the hook */
static struct TagItem *g_tags;
static struct Hook *g_hook;
static int32 helper_main(STRPTR args, int32 len, struct ExecBase *sb)
{
    IExec->Forbid();
    struct Task *t = (struct Task *)IDOS->CreateNewProc(g_tags);
    if (t) IExec->SuspendTask(t, 0);
    IExec->Permit();
    if (t) IDbg->AddDebugHook(t, g_hook);
    target = t;
    IExec->Signal(me, SIGBREAKF_CTRL_F);
    return 0;
}

static uint32 wait_ms_(uint32 mask, uint32 ms);
static struct MsgPort *tport; static struct TimeRequest *treq;
static void logf_(const char *fmt, ...);
static void probe_trace(void *ud, const char *line) { logf_("    | %s\n", line); }
static int32 trivial_main(STRPTR args, int32 len, struct ExecBase *sb) { return 7; }

/* stage 14: are sequential children of one parent able to exit at all? */
static int child_twice(int cli, const char *what)
{
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    for (int r = 0; r < 2; r++) {
        struct DeathMessage dm; memset(&dm, 0, sizeof dm);
        dm.dm_Msg.mn_ReplyPort = port; dm.dm_Msg.mn_Length = sizeof dm;
        BPTR in = IDOS->Open("NIL:", MODE_OLDFILE), out = IDOS->Open("NIL:", MODE_NEWFILE);
        struct Process *p = IDOS->CreateNewProcTags(NP_Entry, trivial_main, NP_Name, "probe-child", NP_Cli, cli,
            NP_Input, in, NP_Output, out, NP_NotifyOnDeathMessage, &dm, TAG_DONE);
        if (!p) { printf("%s run %d: CreateNewProc failed\n", what, r); return 0; }
        int ok = 0;
        for (int i = 0; i < 60 && !ok; i++) { if (IExec->GetMsg(port)) ok = 1; else wait_ms_(0, 50); }
        printf("%s run %d: %s (rc %ld)\n", what, r, ok ? "exited" : "STUCK", (long)dm.dm_ReturnCode);
        if (!ok) return 0;
    }
    IExec->FreeSysObject(ASOT_PORT, port);
    return 1;
}

/* stage 15: a launcher process runs the program with RunCommand, like a shell would */
static BPTR g_seg;
struct lctx { BPTR seg; char args[4]; };
static int32 launcher_main(STRPTR args, int32 len, struct ExecBase *sb)
{
    if (variant & 4) { struct lctx *c = IExec->FindTask(NULL)->tc_UserData; return IDOS->RunCommand(c->seg, 256 * 1024, c->args, 1); }
    return IDOS->RunCommand(g_seg, 256 * 1024, "\n", 1);
}
static uint32 hook_fn(struct Hook *h, struct Task *t, struct KDM *m);
static void poke(uint32 addr, uint32 v);

static int launch_runs(const char *path)
{
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (variant & 64) {   /* a library session that loads the same program, then stays idle */
        dbg_session *S = dbg_create();
        printf("  dbg_create %p, load %d\n", S, S ? dbg_load(S, path, "", NULL, "NIL:") : -1);
    }
    for (int r = 0; r < 4; r++) {
        int hooked = r >= 2 || (variant & 8192);
        if (variant & 16) { char e[128]; dbg_info *di = info_load(path, e, sizeof e); printf("  info_load: %s\n", di ? "ok" : e); info_free(di); }
        g_seg = IDOS->LoadSeg(path);
        Elf32_Handle eh = 0;
        IDOS->GetSegListInfoTags(g_seg, GSLI_ElfHandle, &eh, TAG_DONE);
        eh = IElf->OpenElfTags(OET_ElfHandle, eh, TAG_DONE);
        uint32 E = (uint32)IElf->GetSectionTags(eh, GST_SectionIndex, 1, GST_Load, FALSE, TAG_DONE);
        if (variant & 2048) {   /* like dbg_load: every section */
            uint32 ns = 0; IElf->GetElfAttrsTags(eh, EAT_NumSections, &ns, TAG_DONE);
            for (uint32 i = 0; i < ns; i++) IElf->GetSectionTags(eh, GST_SectionIndex, i, GST_Load, FALSE, TAG_DONE);
        }
        IElf->CloseElfTags(eh, CET_ReClose, TRUE, TAG_DONE);
        uint32 oe = *(uint32 *)E;
        struct DeathMessage dms, *dmp = (variant & 1) ? calloc(1, sizeof *dmp) : &dms;
        memset(dmp, 0, sizeof *dmp);
        dmp->dm_Msg.mn_ReplyPort = port; dmp->dm_Msg.mn_Length = sizeof *dmp;
        struct Hook hooks = { {0}, (HOOKFUNC)hook_fn, 0, 0 }, *hookp = (variant & 2) ? calloc(1, sizeof *hookp) : &hooks;
        *hookp = hooks;
        static struct lctx lc; lc.seg = g_seg; strcpy(lc.args, "\n");
        struct lctx *lcp = (variant & 4) ? calloc(1, sizeof *lcp) : &lc; *lcp = lc;
        BPTR in = IDOS->Open("NIL:", MODE_OLDFILE), out = IDOS->Open("NIL:", MODE_NEWFILE);
        IExec->Forbid();
        target = (struct Task *)IDOS->CreateNewProcTags(NP_Entry, launcher_main, NP_Name, (variant & 512) ? "asmtest" : "probe-launch", NP_Cli, TRUE,
            (variant & 4) ? NP_UserData : TAG_IGNORE, lcp,
            NP_CommandName, "asmtest", NP_Input, in, NP_Output, out, NP_NotifyOnDeathMessage, dmp, TAG_DONE);
        if (target) IExec->SuspendTask(target, 0);
        IExec->Permit();
        if (!target) { printf("launch run %d: CreateNewProc failed\n", r); return 0; }
        uint32 h0 = hits;
        if (hooked) { IDbg->AddDebugHook(target, hookp); poke(E, 0x7FE00008); }
        IExec->RestartTask(target, 0);
        if (hooked) {
            for (int i = 0; i < 100 && hits == h0; i++) wait_ms_(0, 20);
            if (variant & 32) { poke(E, oe); IExec->RestartTask(target, 0); }
            printf("launch run %d: %s at %08lx\n", r, hits != h0 ? "hit" : "NO HIT", (long)copy.ip);
            if (!(variant & 32)) { poke(E, oe); IExec->RestartTask(target, 0); }
        }
        int ok = 0;
        if (variant & 128) { for (int i = 0; i < 1000 && !ok; i++) { if (IExec->GetMsg(port)) ok = 1; else wait_ms_(0, 5); } }
        if (variant & 4096) {   /* the library's delay: DoIO on a struct copy of an opened request */
            struct MsgPort *cp = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
            struct TimeRequest *cr = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
            *cr = *treq;
            cr->Request.io_Message.mn_ReplyPort = cp;
            for (int i = 0; i < 200 && !ok; i++) {
                if (IExec->GetMsg(port)) { ok = 1; break; }
                cr->Request.io_Command = TR_ADDREQUEST; cr->Time.Seconds = 0; cr->Time.Microseconds = 20000;
                IExec->DoIO((struct IORequest *)cr);
            }
        }
        if (variant & 256) {   /* the library's async poll timer: one 10 ms request always outstanding */
            struct MsgPort *ap = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
            struct TimeRequest *ar = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, ap, TAG_DONE);
            IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)ar, 0);
            int busy = 0;
            for (int i = 0; i < 500 && !ok; i++) {
                if (busy && IExec->CheckIO((struct IORequest *)ar)) { IExec->WaitIO((struct IORequest *)ar); busy = 0; }
                if (IExec->GetMsg(port)) { ok = 1; break; }
                if (!busy) { ar->Request.io_Command = TR_ADDREQUEST; ar->Time.Seconds = 0; ar->Time.Microseconds = 10000; IExec->SendIO((struct IORequest *)ar); busy = 1; }
                IExec->Wait((1UL << ap->mp_SigBit) | (1UL << port->mp_SigBit));
            }
            if (busy) { if (!IExec->CheckIO((struct IORequest *)ar)) IExec->AbortIO((struct IORequest *)ar); IExec->WaitIO((struct IORequest *)ar); }
            IExec->CloseDevice((struct IORequest *)ar);
            IExec->FreeSysObject(ASOT_IOREQUEST, ar); IExec->FreeSysObject(ASOT_PORT, ap);
        }
        for (int i = 0; i < 100 && !ok; i++) { if (IExec->GetMsg(port)) ok = 1; else wait_ms_(0, 50); }
        printf("launch run %d (%s): %s rc %ld\n", r, hooked ? "debugged" : "plain", ok ? "exited" : "STUCK", (long)dmp->dm_ReturnCode);
        if (!ok) return 0;
        IDOS->UnLoadSeg(g_seg);
    }
    return 1;
}

static uint32 hook_fn(struct Hook *h, struct Task *t, struct KDM *m)
{
    if (t != target) return 0;
    if (m->type == DBHMT_EXCEPTION) {
        copy = *m->u.ctx; ctxptr = m->u.ctx; last_type = copy.Traptype;
        if (hits < 8) { ring_type[hits] = copy.Traptype; ring_ip[hits] = copy.ip; ring_dar[hits] = copy.dar; ring_msr[hits] = copy.msr; }
        hits++;
        if (variant & 1) m->u.ctx->Traptype = 0;
        if (variant & 16) IExec->Cause(softint);
        else if (!(variant & 8)) IExec->Signal(me, 1UL << sigbit);
        return 1;
    }
    if (m->type == DBHMT_REMTASK && (variant & 32768)) IDbg->AddDebugHook(t, NULL);
    if (m->type == DBHMT_REMTASK) { remtask++; if (variant & 16) IExec->Cause(softint); else if (!(variant & 8)) IExec->Signal(me, 1UL << sigbit); return (variant & 2) ? 1 : 0; }
    return 0;
}

static struct MsgPort *tport; static struct TimeRequest *treq;
static uint32 wait_ms_(uint32 mask, uint32 ms);
static uint32 wait_ms(uint32 mask, uint32 ms)
{
    if (variant & 8) {   /* poll the hit counter instead of being signalled */
        uint32 step = (variant & 32) ? 1 : 20;
        for (uint32 t = 0; t < ms; t += step) {
            if (hits != seen) { seen = hits; return mask; }
            uint32 got = wait_ms_(mask & ~(1UL << sigbit), step);
            if (got) return got;
        }
        return 0;
    }
    return wait_ms_(mask, ms);
}
static uint32 wait_ms_(uint32 mask, uint32 ms)
{
    treq->Request.io_Command = TR_ADDREQUEST;
    treq->Time.Seconds = ms / 1000; treq->Time.Microseconds = (ms % 1000) * 1000;
    IExec->SendIO((struct IORequest *)treq);
    uint32 got = IExec->Wait(mask | (1UL << tport->mp_SigBit));
    if (!IExec->CheckIO((struct IORequest *)treq)) IExec->AbortIO((struct IORequest *)treq);
    IExec->WaitIO((struct IORequest *)treq);
    return got & mask;
}

static void sym(const char *what, uint32 a)
{
    struct DebugSymbol *ds = IDbg->ObtainDebugSymbol((CONST_APTR)a, NULL);
    if (ds) {
        printf("  %s %08lx: %s +%lx fn=%s type=%ld\n", what, (long)a, ds->Name ? ds->Name : "?", (long)ds->Offset,
            ds->SourceFunctionName ? ds->SourceFunctionName : "?", (long)ds->Type);
        IDbg->ReleaseDebugSymbol(ds);
    } else printf("  %s %08lx: no symbol\n", what, (long)a);
}

static void poke(uint32 addr, uint32 v)
{
    APTR stack = IExec->SuperState();
    uint32 old = IMmu->GetMemoryAttrs((APTR)addr, 0);
    uint32 rw = (old & ~(uint32)MEMATTRF_RW_MASK) | MEMATTRF_READ_WRITE;
    rw &= ~(uint32)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED | MEMATTRF_NOT_MAPPED);
    IMmu->SetMemoryAttrs((APTR)addr, 4, rw);
    *(volatile uint32 *)addr = v;
    IMmu->SetMemoryAttrs((APTR)addr, 4, (variant & 8 && stage == 15) ? (old & ~(uint32)(MEMATTRF_REFERENCED | MEMATTRF_CHANGED)) : old);
    if (stack) IExec->UserState(stack);
    IExec->CacheClearE((APTR)addr, 4, CACRF_ClearI | CACRF_ClearD);
    if (!(variant & 1024 && stage == 15))
    printf("  poke %08lx=%08lx (attrs were %08lx) readback %08lx\n", (long)addr, (long)v, (long)old, (long)*(volatile uint32 *)addr);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "dis")) {   /* dbgprobe dis ADDR [N] */
        IDbg = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, 0);
        uint32 a = strtoul(argv[2], 0, 16), n = argc > 3 ? strtoul(argv[3], 0, 10) : 16;
        for (uint32 i = 0; i < n; i++, a += 4) {
            char op[LEN_DISASSEMBLE_OPCODE_STRING + 8], ops[LEN_DISASSEMBLE_OPERANDS_STRING + 8];
            IDbg->DisassembleNative((APTR)a, op, ops);
            IExec->DebugPrintF("");
            fprintf(stdout, "%08lx %08lx %s %s\n", (long)a, (long)*(uint32 *)a, op, ops);
        }
        return 0;
    }
    if (argc < 4) { printf("usage\n"); return 20; }
    if (argc > 4) stage = atoi(argv[4]);
    if (argc > 5) variant = atoi(argv[5]);
    printf("variant %d\n", variant);
    if (variant & 16384) { char b[64]; printf("demangle: %s\n", dbg_demangle("_ZN6shapes4Rect4areaEv", b, sizeof b) ? b : "-"); }
    uint32 main_link = strtoul(argv[2], 0, 16), text_link = strtoul(argv[3], 0, 16);
    me = IExec->FindTask(NULL);
    sigbit = IExec->AllocSignal(-1);
    IDbg = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, 0);
    IMmu = (struct MMUIFace *)IExec->GetInterface((struct Library *)SysBase, "mmu", 1, 0);
    tport = IExec->AllocSysObject(ASOT_PORT, 0);
    treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
    IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0);
    struct MsgPort *dport = IExec->AllocSysObject(ASOT_PORT, 0);
    softint = IExec->AllocSysObjectTags(ASOT_INTERRUPT, ASOINTR_Code, soft_fn, ASOINTR_Data, NULL, TAG_DONE);
    printf("IDebug=%p IMMU=%p\n", IDbg, IMmu);

    if (stage == 16) {   /* the library's own run engine, from this process */
        tport = IExec->AllocSysObject(ASOT_PORT, 0);
        treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
        IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0);
        dbg_session *S = dbg_create();
        dbg_set_trace(S, probe_trace, NULL);
        for (int r = 0; r < 3; r++) {
            if ((r == 0 || !(variant & 1)) && !dbg_load(S, argv[1], "", NULL, "NIL:")) { printf("lib run %d: load failed %s\n", r, dbg_last_error(S)); break; }
            dbg_run(S, 0);
            int done = 0;
            dbg_event ev;

            for (int i = 0; i < 250 && !done; i++) {
                while (dbg_poll(S, &ev)) {
                    if (ev.type == DBG_EV_EXITED) { printf("lib run %d: exited %d\n", r, ev.exit_code); done = 1; }
                    else if (ev.type == DBG_EV_STOPPED) { printf("lib run %d: stopped %s\n", r, ev.text); done = 2; }
                }
                if (!done) wait_ms_(dbg_signal_mask(S), 20);
            }
            if (done != 1) { printf("lib run %d: STUCK\n", r); return 0; }
            if (!(variant & 1)) dbg_unload(S);
            if (variant & 2) wait_ms_(0, 1000);
        }
        dbg_destroy(S);
        printf("probe done\n");
        return 0;
    }
    if (stage == 15) {
        me = IExec->FindTask(NULL);
        IDbg = (struct DebugIFace *)IExec->GetInterface((struct Library *)SysBase, "debug", 1, 0);
        IMmu = (struct MMUIFace *)IExec->GetInterface((struct Library *)SysBase, "mmu", 1, 0);
        tport = IExec->AllocSysObject(ASOT_PORT, 0);
        treq = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, tport, TAG_DONE);
        IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0);
        launch_runs(argv[1]);
        printf("probe done\n");
        return 0;
    }
    if (stage == 14) {
        if (!child_twice(FALSE, "entry, no CLI")) return 0;
        if (!child_twice(TRUE, "entry, CLI")) return 0;
        stage = 12; variant = 655368;     /* then the program twice, no debugger */
    }
    int runs = 0;
    BPTR seg = 0;
again:;
    if (!seg) seg = IDOS->LoadSeg(argv[1]);
    if (!seg) { printf("LoadSeg failed\n"); return 20; }
    Elf32_Handle eh = 0;
    IDOS->GetSegListInfoTags(seg, GSLI_ElfHandle, &eh, TAG_DONE);
    eh = IElf->OpenElfTags(OET_ElfHandle, eh, TAG_DONE);
    uint32 text_load = (uint32)IElf->GetSectionTags(eh, GST_SectionIndex, 1, GST_Load, FALSE, TAG_DONE);
    uint32 X = text_load + (main_link - text_link);
    uint32 nsec = 0; IElf->GetElfAttrsTags(eh, EAT_NumSections, &nsec, TAG_DONE);
    for (uint32 i = 0; i < nsec; i++) {
        APTR p = IElf->GetSectionTags(eh, GST_SectionIndex, i, GST_Load, FALSE, TAG_DONE);
        if (p) printf("  section %ld at %p\n", (long)i, p);
    }
    IElf->CloseElfTags(eh, CET_ReClose, TRUE, TAG_DONE);
    printf(".text at %08lx, main at %08lx, insns %08lx %08lx\n", (long)text_load, (long)X, (long)((uint32 *)X)[0], (long)((uint32 *)X)[1]);

    struct Hook hook = { {0}, (HOOKFUNC)hook_fn, 0, 0 };
    struct DeathMessage dm; memset(&dm, 0, sizeof dm);
    dm.dm_Msg.mn_ReplyPort = dport; dm.dm_Msg.mn_Length = sizeof dm;
    char outname[64]; snprintf(outname, sizeof outname, "DH1:AmiForge/probe-out%d-%d.txt", stage, variant);
    BPTR in = IDOS->Open("NIL:", MODE_OLDFILE);
    IExec->Forbid();
    BPTR out = IDOS->Open((variant & 4096) ? "NIL:" : outname, MODE_NEWFILE);
    BPTR plock = 0, clock = 0;
    if (variant & 512) { BPTR fl = IDOS->Lock(argv[1], SHARED_LOCK); plock = IDOS->ParentDir(fl); IDOS->UnLock(fl); clock = IDOS->DupLock(plock); }
    char uname[32]; snprintf(uname, sizeof uname, "probe-target-%d", runs);
    const char *pname = (variant & 2048) ? IDOS->FilePart(argv[1]) : uname;
    struct TagItem tags[] = {
        { NP_Seglist, (uint32)seg }, { NP_FreeSeglist, FALSE }, { NP_Name, (uint32)"probe-target" },
        { NP_Cli, TRUE }, { NP_Arguments, (uint32)"\n" },
        { NP_Input, (uint32)in }, { NP_CloseInput, TRUE }, { NP_Output, (uint32)out }, { NP_CloseOutput, TRUE },
        { NP_Error, 0 }, { NP_CloseError, FALSE }, { NP_NotifyOnDeathMessage, (uint32)&dm }, { TAG_DONE, 0 } };
    if (stage == 13) {
        IExec->Permit();
        g_tags = tags; g_hook = &hook; target = NULL;
        IExec->SetSignal(0, SIGBREAKF_CTRL_F);
        struct Process *hp = IDOS->CreateNewProcTags(NP_Entry, helper_main, NP_Name, "probe-launcher", NP_Child, FALSE, TAG_DONE);
        if (hp) IExec->Wait(SIGBREAKF_CTRL_F);
        IExec->Forbid();
    } else {
    target = (struct Task *)IDOS->CreateNewProcTags(NP_Seglist, seg, NP_FreeSeglist, FALSE, NP_Name, pname,
        (variant & 2048) ? NP_CommandName : TAG_IGNORE, pname,
        NP_Cli, TRUE, NP_Arguments, "\n",
        NP_Input, (variant & 1024) ? out : in, NP_CloseInput, (variant & 1024) ? FALSE : TRUE,
        NP_Output, out, NP_CloseOutput, (variant & 1024) ? FALSE : TRUE,
        NP_Error, (variant & 1024) ? out : 0, NP_CloseError, FALSE,
        (variant & 128) ? NP_StackSize : TAG_IGNORE, 256 * 1024,
        (variant & 256) ? NP_Path : TAG_IGNORE, 0,
        (variant & 256) ? NP_ConsolePort : TAG_IGNORE, (variant & 8192) ? ((struct FileHandle *)BADDR(out))->fh_MsgPort : NULL,
        (variant & 512) ? NP_CurrentDir : TAG_IGNORE, clock,
        (variant & 512) ? NP_ProgramDir : TAG_IGNORE, plock,
        (variant & 1048576) ? NP_NotifyOnDeathSigTask : NP_NotifyOnDeathMessage, (variant & 1048576) ? (uint32)me : (uint32)&dm,
        (variant & 1048576) ? NP_NotifyOnDeathSignalBit : TAG_IGNORE, (uint32)dport->mp_SigBit,
        TAG_DONE);
    if (target) IExec->SuspendTask(target, 0);
    }
    IExec->Permit();
    if (!target) { printf("CreateNewProc failed\n"); return 20; }
    if (stage == 13) printf("created by the launcher: state=%ld\n", (long)target->tc_State);
    else if (!(variant & 524288))
    printf("AddDebugHook=%ld state=%ld\n", (long)IDbg->AddDebugHook(target, &hook), (long)target->tc_State);

    uint32 o0 = ((uint32 *)X)[0], o1 = ((uint32 *)X)[1], o2 = ((uint32 *)X)[2];
    {   APTR st = IExec->SuperState();
        uint32 a = IMmu->GetMemoryAttrs((APTR)X, 0);
        if (st) IExec->UserState(st);
        printf("stage %d: attrs at main %08lx\n", stage, (long)a); }
    struct ExceptionContext rc;
    uint32 mask = 1UL << sigbit, dmask = 1UL << dport->mp_SigBit;
    if (stage == 1) { IExec->RestartTask(target, 0); goto wait_exit; }
    if (stage == 2) { poke(X, o0); IExec->RestartTask(target, 0); goto wait_exit; }
    if (stage == 9) {   /* kill by returning from the program's entry point with the entry context */
        uint32 E = text_load, oe = *(uint32 *)E;   /* _start is the first thing in .text here */
        poke(E, 0x7FE00008); poke(X, 0x7FE00008);
        IExec->RestartTask(target, 0);
        if (!wait_ms(mask, 3000)) { printf("FAIL: no entry hit\n"); goto out; }
        struct ExceptionContext ent; memset(&ent, 0, sizeof ent);
        IDbg->ReadTaskContext(target, &ent, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
        printf("entry hit ip=%08lx lr=%08lx r1=%08lx\n", (long)copy.ip, (long)ent.lr, (long)ent.gpr[1]);
        sym("entry lr", ent.lr);
        poke(E, oe);
        IExec->RestartTask(target, 0);
        if (!wait_ms(mask, 3000)) { printf("FAIL: no main hit\n"); goto out; }
        printf("main hit ip=%08lx; killing\n", (long)copy.ip);
        poke(X, o0);
        ent.ip = ent.lr; ent.gpr[3] = 20;
        IDbg->WriteTaskContext(target, &ent, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
        IExec->RestartTask(target, 0);
        goto wait_exit;
    }
    if (stage == 10 || stage == 11 || stage == 12 || stage == 13) {   /* hit at the ELF entry (10) or at main (11), then run to the end */
        uint32 E = stage != 11 ? text_load : X, oe = *(uint32 *)E;
        if (variant & (131072 | 262144)) {   /* no stops: plain run, optionally with a write+restore */
            if (variant & 262144) { poke(E, 0x7FE00008); poke(E, oe); }
            IExec->RestartTask(target, 0);
            goto wait_exit;
        }
        poke(E, 0x7FE00008);
        IExec->RestartTask(target, 0);
        if (!wait_ms(mask, 3000)) { printf("FAIL: no hit\n"); goto out; }
        printf("hit at %s ip=%08lx\n", stage == 10 ? "_start" : "main", (long)copy.ip);
        if (variant & 64) {
            struct ExceptionContext fc; memset(&fc, 0, sizeof fc);
            IDbg->ReadTaskContext(target, &fc, RTCF_INFO | RTCF_SPECIAL | RTCF_STATE | RTCF_GENERAL | RTCF_FPU);
            printf("  read context: ip %08lx fpscr %08lx\n", (long)fc.ip, (long)(uint32)fc.fpscr);
        }
        poke(E, oe);
        IExec->RestartTask(target, 0);
        goto wait_exit;
    }
    if (stage == 8) {   /* one hit only, then run to exit */
        poke(X, 0x7FE00008);
        IExec->RestartTask(target, 0);
        if (!wait_ms(mask, 3000)) { printf("FAIL: no hit\n"); goto out; }
        printf("hit1 ip=%08lx variant=%d\n", (long)copy.ip, variant);
        poke(X, o0);
        if (variant & 4) printf("  removed hook: %ld\n", (long)IDbg->AddDebugHook(target, NULL));
        IExec->RestartTask(target, 0);
        goto wait_exit;
    }
    poke(X, 0x7FE00008); poke(X + 4, 0x7FE00008);
    IExec->RestartTask(target, 0);
    if (!wait_ms(mask, 3000)) { printf("FAIL: no hit at main\n"); goto out; }
    memset(&rc, 0, sizeof rc);
    IDbg->ReadTaskContext(target, &rc, RTCF_INFO | RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
    printf("hit1: hits=%ld type=%lx ip=%08lx (want %08lx) msr=%08lx r1=%08lx lr=%08lx flags=%lx | rtc ip=%08lx type=%lx r1=%08lx | state=%ld ctxptr=%p\n",
        (long)hits, (long)copy.Traptype, (long)copy.ip, (long)X, (long)copy.msr, (long)copy.gpr[1], (long)copy.lr, (long)copy.Flags,
        (long)rc.ip, (long)rc.Traptype, (long)rc.gpr[1], (long)target->tc_State, ctxptr);

    if (stage >= 4) {   /* plain restart after restoring main's first insn */
        uint32 r1a = copy.gpr[1];
        poke(X, o0);
        IExec->RestartTask(target, 0);
        printf("  after RestartTask state=%ld\n", (long)target->tc_State);
        if (!wait_ms(mask, 3000)) { printf("FAIL: no hit2 (plain restart)\n"); goto out; }
        uint32 r1b = copy.gpr[1];
        if (variant & 64) {   /* read the FPU context like the library does */
            struct ExceptionContext fc; memset(&fc, 0, sizeof fc);
            IDbg->ReadTaskContext(target, &fc, RTCF_INFO | RTCF_SPECIAL | RTCF_STATE | RTCF_GENERAL | RTCF_FPU);
            printf("  read FPU context: fpscr %08lx\n", (long)(uint32)fc.fpscr);
        }
        printf("hit2: hits=%ld ip=%08lx (want %08lx) r1 %08lx -> %08lx backchain=%08lx => stwu %s\n", (long)hits, (long)copy.ip,
            (long)(X + 4), (long)r1a, (long)r1b, (long)*(uint32 *)r1b, r1b == r1a - 0x80 ? "EXECUTED" : "SKIPPED");
        if (stage == 4) { poke(X + 4, o1); IExec->RestartTask(target, 0); goto wait_exit; }
        if (stage == 6 || stage == 7) {   /* write back UNCHANGED context, then plain run to exit */
            uint32 fl = stage == 6 ? RTCF_STATE : RTCF_GENERAL;
            memset(&rc, 0, sizeof rc);
            IDbg->ReadTaskContext(target, &rc, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL | RTCF_INFO);
            printf("  rtc: ip=%08lx msr=%08lx r1=%08lx type=%lx (hook: ip=%08lx msr=%08lx)\n", (long)rc.ip, (long)rc.msr, (long)rc.gpr[1],
                (long)rc.Traptype, (long)copy.ip, (long)copy.msr);
            poke(X + 4, o1);
            printf("  WriteTaskContext(unchanged, %s) -> %ld\n", stage == 6 ? "STATE" : "GENERAL", (long)IDbg->WriteTaskContext(target, &rc, fl));
            IExec->RestartTask(target, 0);
            goto wait_exit;
        }
        /* stage 5: learn where a resume lands. Make the state correct for either answer:
         * emulate stwu if it was skipped, set r0 = lr (what mflr at X+4 does), ip = X+4, trap at X+8. */
        IDbg->ReadTaskContext(target, &rc, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL);
        if (r1b == r1a) { rc.gpr[1] = r1a - 0x80; *(uint32 *)rc.gpr[1] = r1a; printf("  emulated stwu\n"); }
        rc.gpr[0] = rc.lr;
        rc.ip = X + 4;
        poke(X + 4, o1); poke(X + 8, 0x7FE00008);
        printf("  WriteTaskContext(ip=X+4) -> %ld\n", (long)IDbg->WriteTaskContext(target, &rc, RTCF_STATE | RTCF_GENERAL));
        IExec->RestartTask(target, 0);
        if (wait_ms(mask, 3000)) {
            printf("hit3: ip=%08lx (X+8 = %08lx) r1=%08lx r0=%08lx lr=%08lx\n", (long)copy.ip, (long)(X + 8), (long)copy.gpr[1], (long)copy.gpr[0], (long)copy.lr);
            poke(X + 8, o2);
            IExec->RestartTask(target, 0);
            goto wait_exit;
        }
        printf("no hit3: resume skipped X+8 or went elsewhere\n");
        goto out;
    }
    rc.ip = X + 4;
    printf("  WriteTaskContext -> %ld\n", (long)IDbg->WriteTaskContext(target, &rc, RTCF_STATE));
    IExec->RestartTask(target, 0);
    printf("  after RestartTask state=%ld\n", (long)target->tc_State);
    if (!wait_ms(mask, 3000)) { printf("FAIL: no hit2\n"); goto out; }
    printf("hit2: hits=%ld ip=%08lx (want %08lx: ip write %s)\n", (long)hits, (long)copy.ip, (long)(X + 4),
        copy.ip == X + 4 ? "honoured" : "IGNORED");
    poke(X, o0); poke(X + 4, o1);
    rc.ip = X;
    IDbg->WriteTaskContext(target, &rc, RTCF_STATE);
    IExec->RestartTask(target, 0);
wait_exit:;
    int deathsig = 0;
    for (int i = 0; i < 10; i++) {
        struct Message *m = IExec->GetMsg(dport);
        if ((variant & 1048576) && deathsig) m = (struct Message *)1;
        if (m) {
            printf("exit: DeathMessage rc=%ld res2=%ld remtask=%ld hits=%ld (run %d)\n", (long)dm.dm_ReturnCode, (long)dm.dm_Result2, (long)remtask, (long)hits, runs);
            struct Task *old = target;
            target = 0;
            (void)old;
            {   /* does the dead process really go away? */
                char nm[32]; snprintf(nm, sizeof nm, "probe-target-%d", runs);
                int gone = -1;
                for (int k = 0; k < 40; k++) {
                    IExec->Forbid();
                    struct Task *ft = IExec->FindTask(nm);
                    int st = ft ? ft->tc_State : -1;
                    IExec->Permit();
                    if (!ft) { gone = k * 50; break; }
                    if (k == 0 || k == 39) printf("  %s still there, state %d\n", nm, st);
                    wait_ms_(0, 50);
                }
                printf("  %s gone after %d ms\n", nm, gone);
            }
            if ((stage == 12 || stage == 13) && ++runs < 3) {
                if (!(variant & 2097152)) { IDOS->UnLoadSeg(seg); seg = 0; }
                hits = seen = remtask = 0;
                if (variant & 65536) wait_ms_(0, 2000);
                goto again;
            }
            break;
        }
        if (wait_ms(dmask | mask, 500) & dmask) deathsig = 1;
    }
out:
    for (uint32 i = 0; i < hits && i < 8; i++)
        printf("  exc %ld: type=%lx ip=%08lx dar=%08lx msr=%08lx\n", (long)i, (long)ring_type[i], (long)ring_ip[i], (long)ring_dar[i], (long)ring_msr[i]);
    if (target) {   /* recover: put the code back and let it run to the end */
        printf("stuck: state=%ld hits=%ld\n", (long)target->tc_State, (long)hits);
        IExec->SuspendTask(target, 0);
        memset(&rc, 0, sizeof rc);
        IDbg->ReadTaskContext(target, &rc, RTCF_STATE | RTCF_GENERAL | RTCF_SPECIAL | RTCF_INFO);
        printf("  stuck at ip=%08lx msr=%08lx r1=%08lx lr=%08lx type=%lx (main=%08lx)\n", (long)rc.ip, (long)rc.msr, (long)rc.gpr[1], (long)rc.lr, (long)rc.Traptype, (long)X);
        IExec->RestartTask(target, 0);
        poke(X, o0); poke(X + 4, o1); if (stage == 5) poke(X + 8, o2);
        sym("ip", rc.ip); sym("lr", rc.lr);
        for (uint32 a = rc.ip - 24; a <= rc.ip + 8; a += 4) {
            char op[LEN_DISASSEMBLE_OPCODE_STRING + 8], ops[LEN_DISASSEMBLE_OPERANDS_STRING + 8];
            IDbg->DisassembleNative((APTR)a, op, ops);
            printf("    %08lx %08lx %s %s%s\n", (long)a, (long)*(uint32 *)a, op, ops, a == rc.ip ? "   <==" : "");
        }
        uint32 *fp = (uint32 *)rc.gpr[1];
        for (int d = 0; d < 12 && fp && ((uint32)fp & 3) == 0 && IExec->TypeOfMem(fp); d++) {
            uint32 *next = (uint32 *)fp[0];
            if (!next || !IExec->TypeOfMem(next)) break;
            sym("frame", next[1]); fp = next;
        }
        IExec->RestartTask(target, 0);
        for (int i = 0; i < 10 && target; i++) {
            if (IExec->GetMsg(dport)) { printf("recovered: exit rc=%ld\n", (long)dm.dm_ReturnCode); target = 0; }
            else wait_ms(dmask, 500);
        }
    }
    if (target) { IExec->SuspendTask(target, 0); printf("target STILL alive: suspended it (seglist left loaded)\n"); }
    else IDOS->UnLoadSeg(seg);
    IExec->FreeSignal(sigbit);
    printf("probe done\n");
    return 0;
}
