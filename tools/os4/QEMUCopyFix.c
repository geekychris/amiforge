/*
 * QEMUCopyFix — makes exec's CopyMem/CopyMemQuick copy with the CPU.
 *
 * On QEMU's sam460ex, exec hands large aligned copies (256 KB and up) to the 460EX DMA
 * engine, which QEMU doesn't emulate: the copy silently doesn't happen, 255 KB at a time.
 * newlib's memcpy uses CopyMem for those copies, so programs that move big buffers (cc1plus
 * parsing C++ headers, for one) see garbage. This patches the two functions in exec's main
 * interface with plain CPU loops and stays resident; Ctrl-C puts the originals back.
 *
 * Not needed on real hardware. Start it early, e.g. in S:User-Startup:
 *     Run >NIL: C:QEMUCopyFix
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/interfaces.h>
#include <stddef.h>

static const char version[] __attribute__((used)) = "$VER: QEMUCopyFix 1.0 (24.10.2026)";

typedef void (*CopyFn)(struct ExecIFace *, CONST_APTR, APTR, ULONG);

/* memmove semantics, no calls (it may run anywhere, even in interrupts) */
static void cpuCopy(const uint8 *s, uint8 *d, ULONG n)
{
    if (d == s || n == 0)
        return;
    if (d < s || d >= s + n) {
        if ((((uint32)s | (uint32)d) & 3) == 0) {
            const uint32 *s4 = (const uint32 *)s;
            uint32 *d4 = (uint32 *)d;
            while (n >= 32) {
                uint32 a = s4[0], b = s4[1], c = s4[2], e = s4[3], f = s4[4], g = s4[5], h = s4[6], i = s4[7];
                d4[0] = a; d4[1] = b; d4[2] = c; d4[3] = e; d4[4] = f; d4[5] = g; d4[6] = h; d4[7] = i;
                s4 += 8; d4 += 8; n -= 32;
            }
            while (n >= 4) { *d4++ = *s4++; n -= 4; }
            s = (const uint8 *)s4;
            d = (uint8 *)d4;
        }
        while (n--) *d++ = *s++;
    } else {
        s += n;
        d += n;
        while (n--) *--d = *--s;
    }
}

static void APICALL fixedCopyMem(struct ExecIFace *self, CONST_APTR src, APTR dst, ULONG size)
{
    (void)self;
    cpuCopy((const uint8 *)src, (uint8 *)dst, size);
}

static void APICALL fixedCopyMemQuick(struct ExecIFace *self, CONST_APTR src, APTR dst, ULONG size)
{
    (void)self;
    cpuCopy((const uint8 *)src, (uint8 *)dst, size);
}

int main(void)
{
    struct Interface *iexec = (struct Interface *)IExec;
    if (IExec->FindPort("QEMUCopyFix")) {
        IDOS->PutStr("QEMUCopyFix is already running.\n");
        return 5;
    }
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, ASOPORT_Name, "QEMUCopyFix",
                                                     ASOPORT_Public, TRUE, TAG_END);
    if (!port)
        return 20;

    IExec->Forbid();
    APTR oldCopyMem = IExec->SetMethod(iexec, offsetof(struct ExecIFace, CopyMem), (APTR)fixedCopyMem);
    APTR oldCopyMemQuick = IExec->SetMethod(iexec, offsetof(struct ExecIFace, CopyMemQuick), (APTR)fixedCopyMemQuick);
    IExec->Permit();
    IDOS->PutStr("QEMUCopyFix: CopyMem and CopyMemQuick now copy with the CPU. Ctrl-C to undo.\n");

    IExec->Wait(SIGBREAKF_CTRL_C);

    IExec->Forbid();
    IExec->SetMethod(iexec, offsetof(struct ExecIFace, CopyMem), oldCopyMem);
    IExec->SetMethod(iexec, offsetof(struct ExecIFace, CopyMemQuick), oldCopyMemQuick);
    IExec->Permit();
    IExec->FreeSysObject(ASOT_PORT, port);
    IDOS->PutStr("QEMUCopyFix: original CopyMem restored.\n");
    return 0;
}
