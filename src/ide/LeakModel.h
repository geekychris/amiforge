// The leak tracker's bookkeeping: matches the resource events the debugger records (see
// dbg_track_resources) and, when the program has ended, reports what it never released,
// grouped by where it was acquired, plus misuse it saw on the way (double frees, frees with
// the wrong function, FreeMem with the wrong size). No OS calls: tested on the host.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdint.h>

#include "dbg/dbg.h"

namespace af {

// What an address is, as far as the debugger knows.
struct CodeLoc {
    bool inProgram = false;
    std::string function;       // "main", "Ball::step(int, int)", or "newlib.library: malloc"
    std::string file;           // as stabs recorded it; empty if unknown
    int line = 0;
};
using CodeResolver = std::function<CodeLoc(uint32_t addr)>;

struct Leak {
    int kind = 0;               // dbg_res_kind
    int fn = 0;                 // the call that acquired it
    uint32_t addr = 0, size = 0, seq = 0;
    uint32_t callers[DBG_RES_CALLERS] = {};
};

struct LeakGroup {
    int kind = 0;
    uint32_t site = 0;          // the first return address in the program (0: none)
    CodeLoc loc;                // ... and where that is
    std::vector<Leak> items;    // in acquisition order
    uint64_t bytes = 0;         // memory kinds only
};

struct LeakProblem {
    std::string text;           // "FreeVec() of memory from malloc()"
    uint32_t site = 0;
    CodeLoc loc;                // where the bad release happened
};

class LeakModel {
public:
    void reset();
    void add(const dbg_res_event &e);
    void addAll(const dbg_res_event *e, int n) { for (int i = 0; i < n; i++) add(e[i]); }

    // What is still held, grouped by acquisition site (most bytes / items first).
    std::vector<LeakGroup> report(const CodeResolver &resolve) const;
    std::vector<LeakProblem> problems(const CodeResolver &resolve) const;

    size_t outstanding() const;
    uint64_t outstandingBytes() const;
    uint32_t events() const { return nEvents; }
    uint32_t unknownReleases() const { return nUnknown; }   // releases of things acquired before tracking began
    static bool isMemory(int kind);
    // "3 x AllocVec, 1.2 KB" style summary of a group.
    static std::string describe(const LeakGroup &g);
    static std::string formatBytes(uint64_t n);

private:
    struct Key {
        int kind; uint32_t addr;
        bool operator<(const Key &o) const { return kind != o.kind ? kind < o.kind : addr < o.addr; }
    };
    struct Bad { std::string text; uint32_t callers[DBG_RES_CALLERS]; };
    std::map<Key, std::vector<Leak>> held;      // outstanding acquisitions (a stack per key)
    std::map<Key, uint32_t> released;            // released memory, until reused: double frees
    std::vector<Bad> bad;
    uint32_t nEvents = 0, nUnknown = 0;
};

// The first caller in the program (callers are innermost first); 0 if none is.
uint32_t firstProgramCaller(const uint32_t *callers, int n, const CodeResolver &resolve, CodeLoc *loc = nullptr);

} // namespace af
