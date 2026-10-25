// demangle.cpp — C++ name demangling for the debugger (abi::__cxa_demangle from libstdc++).
#include <cxxabi.h>
#include <cstdlib>
#include <cstring>
#include "dbg_int.h"

extern "C" char *dbg_demangle(const char *name, char *buf, size_t n)
{
    if (!name || !n || std::strncmp(name, "_Z", 2)) return nullptr;
    int status = 0;
    char *d = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (!d || status) { std::free(d); return nullptr; }
    std::strncpy(buf, d, n - 1);
    buf[n - 1] = 0;
    std::free(d);
    return buf;
}
