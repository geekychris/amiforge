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
