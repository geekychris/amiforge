/* frame.c — stack frames from the PowerPC SysV back chain.
 *
 * Every frame starts with the back chain word (the caller's r1); a function saves its
 * return address at 4(caller's r1). At -O0 GCC's prologue is
 *     stwu r1,-N(r1) ; [mflr r0 ; stw r0,N+4(r1)] ; stw r31,... ; mr r31,r1
 * so before the stwu the caller's r1 (CFA) is r1 itself, and until LR is saved (or for
 * leaf functions, which never save it) the return address is still in LR. */
#include "dbg_int.h"

#define MFLR_R0 0x7c0802a6u

void frame_analyse0(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr, frame_info *f)
{
    uint32_t lo, hi, body, insn = 0, cfa = 0;
    f->pc = pc; f->sp = r1; f->cfa = 0; f->ret = 0;
    if (info_func_range(di, pc, &lo, &hi, &body) && mem_read32(m, lo, &insn)) {
        int32_t disp;
        if (!ppc_is_stwu_r1(insn, &disp)) {          /* frameless (assembler leaf) */
            f->cfa = r1; f->ret = lr;
            return;
        }
        if (pc == lo) {                              /* nothing of the prologue has run */
            f->cfa = r1; f->sp = r1 + (uint32_t)disp; f->ret = lr;
            return;
        }
        if (!mem_read32(m, r1, &cfa)) return;
        f->cfa = cfa;
        /* has the function saved LR yet? (non-leaf: mflr r0 in the prologue) */
        int saves_lr = 0;
        for (uint32_t a = lo + 4; a < body && a < lo + 64; a += 4) {
            uint32_t w;
            if (mem_read32(m, a, &w) && w == MFLR_R0) { saves_lr = 1; break; }
        }
        if (!saves_lr || pc < body) f->ret = lr;
        else if (!mem_read32(m, cfa + 4, &f->ret)) f->ret = 0;
        return;
    }
    /* outside the program: assume a normal frame */
    if (mem_read32(m, r1, &cfa) && cfa > r1) {
        f->cfa = cfa;
        if (!mem_read32(m, cfa + 4, &f->ret)) f->ret = 0;
    }
}

int frame_walk(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr,
               frame_info *out, int max)
{
    if (max <= 0) return 0;
    frame_analyse0(di, m, pc, r1, lr, &out[0]);
    int n = 1;
    while (n < max) {
        const frame_info *p = &out[n - 1];
        if (!p->ret || !p->cfa || p->cfa < p->sp || (p->ret & 3)) break;
        frame_info *f = &out[n];
        f->pc = p->ret;
        f->sp = p->cfa;
        uint32_t cfa = 0;
        if (!mem_read32(m, f->sp, &cfa) || !cfa || cfa <= f->sp) {   /* end of the chain */
            f->cfa = 0; f->ret = 0;
            n++;
            break;
        }
        f->cfa = cfa;
        if (!mem_read32(m, cfa + 4, &f->ret)) f->ret = 0;
        n++;
    }
    return n;
}

/* Where a pause that caught the program outside its own code (in the kernel or a library)
 * can stop instead: the return addresses into the program's code, innermost first. LR comes
 * first: a leaf (or a function that hasn't saved it yet) returns through it, and the back
 * chain then starts one frame further out. Every address returned is in the program's code,
 * so any of them is a safe place to stop; the innermost is the one normally hit. Returns
 * 0 when pc is in the program itself (nothing to defer) or nothing usable was found. */
int frame_pause_targets(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr,
                        uint32_t *out, int max)
{
    if (max <= 0 || info_in_program(di, pc)) return 0;
    int n = 0;
    if (!(lr & 3) && info_in_program(di, lr)) out[n++] = lr;
    frame_info f[32];
    int nf = frame_walk(di, m, pc, r1, lr, f, 32);
    for (int i = 1; i < nf && n < max; i++) {
        uint32_t a = f[i].pc;
        if ((a & 3) || !info_in_program(di, a)) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) if (out[k] == a) dup = 1;
        if (!dup) out[n++] = a;
    }
    return n;
}

/* A pause trap was hit with stack pointer r1; the pause caught the program in the OS with
 * pause_r1. A return to one of the program's frames leaves r1 at or above pause_r1, while
 * a callback the OS makes into the program (a hook, a comparison function) runs deeper, below
 * it, possibly with the OS's locks held: that is not where to stop. The check only applies
 * when pause_r1 lies on the task's stack [sp_lo, sp_hi); if the OS call was running on
 * another stack, r1 says nothing and any hit counts. */
int frame_pause_hit_ok(uint32_t pause_r1, uint32_t sp_lo, uint32_t sp_hi, uint32_t r1)
{
    if (!pause_r1 || pause_r1 < sp_lo || pause_r1 >= sp_hi) return 1;
    return r1 >= pause_r1;
}
