/* ppc.c — decodes PowerPC branches to find where an instruction can go next. */
#include "dbg_int.h"

static int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

void ppc_decode(uint32_t insn, uint32_t pc, ppc_branch *b)
{
    uint32_t op = insn >> 26, aa = (insn >> 1) & 1, bo = (insn >> 21) & 31;
    b->kind = PPC_OTHER; b->link = (int)(insn & 1); b->always = 0; b->uses_ctr = 0; b->target = 0;
    switch (op) {
    case 18:                                            /* b, ba, bl, bla */
        b->kind = PPC_B; b->always = 1;
        b->target = (uint32_t)sext(insn & 0x03fffffc, 26) + (aa ? 0 : pc);
        return;
    case 16:                                            /* bc, bca, bcl, bcla */
        b->kind = PPC_BC;
        b->target = (uint32_t)sext(insn & 0xfffc, 16) + (aa ? 0 : pc);
        break;
    case 19: {
        uint32_t xo = (insn >> 1) & 0x3ff;
        if (xo == 16) b->kind = PPC_BCLR;
        else if (xo == 528) b->kind = PPC_BCCTR;
        else { b->link = 0; return; }
        break;
    }
    case 17:                                            /* sc */
        b->kind = PPC_SC; b->link = 0;
        return;
    case 3:                                             /* twi */
        b->kind = PPC_TRAP; b->link = 0;
        return;
    case 31:
        if (((insn >> 1) & 0x3ff) == 4) { b->kind = PPC_TRAP; b->link = 0; }   /* tw */
        else b->link = 0;
        return;
    default:
        b->link = 0;
        return;
    }
    /* BO: 1z1zz = branch always; bit 2 (0x04) clear = decrement CTR */
    b->always = (bo & 0x14) == 0x14;
    b->uses_ctr = !(bo & 0x04);
}

int ppc_successors(uint32_t insn, uint32_t pc, uint32_t lr, uint32_t ctr, uint32_t out[2])
{
    ppc_branch b;
    ppc_decode(insn, pc, &b);
    uint32_t target;
    switch (b.kind) {
    case PPC_B: case PPC_BC: target = b.target; break;
    case PPC_BCLR: target = lr & ~3u; break;
    case PPC_BCCTR: target = ctr & ~3u; break;
    default: out[0] = pc + 4; return 1;
    }
    if (b.always) { out[0] = target; return 1; }
    out[0] = pc + 4;
    if (target == pc + 4) return 1;
    out[1] = target;
    return 2;
}

int ppc_is_stwu_r1(uint32_t insn, int32_t *disp)
{
    /* stwu r1,d(r1): opcode 37, rS = 1, rA = 1 */
    if ((insn & 0xffff0000u) != 0x94210000u) return 0;
    if (disp) *disp = (int16_t)(insn & 0xffff);
    return 1;
}
