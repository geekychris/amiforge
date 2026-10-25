// A fake program for the debugtools tests: code at 0x1000-0x1fff, three functions.
#pragma once
#include "LeakModel.h"
#include <cstring>

namespace dtfake {
inline af::CodeLoc resolve(uint32_t a)
{
    af::CodeLoc c;
    if (a >= 0x1000 && a < 0x2000) {
        c.inProgram = true;
        c.file = "main.c";
        if (a < 0x1400) c.function = "main";
        else if (a < 0x1800) c.function = "load_level";
        else c.function = "draw";
        c.line = 10 + (int)((a - 0x1000) / 16);
    } else if (a >= 0x7000000 && a < 0x8000000) {
        c.function = "newlib.library: malloc";
    } else {
        c.function = "kernel";
    }
    return c;
}

inline dbg_res_event ev(int kind, int is_free, int fn, uint32_t addr, uint32_t size, uint32_t c0, uint32_t c1 = 0, uint32_t c2 = 0)
{
    static uint32_t seq = 0;
    dbg_res_event e;
    std::memset(&e, 0, sizeof e);
    e.kind = (uint8_t)kind; e.is_free = (uint8_t)is_free; e.fn = (uint16_t)fn;
    e.addr = addr; e.size = size; e.seq = ++seq;
    e.callers[0] = c0; e.callers[1] = c1; e.callers[2] = c2;
    return e;
}
}
