// The leak tracker's bookkeeping (see LeakModel.h).
#include "LeakModel.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace af {

bool LeakModel::isMemory(int kind)
{
    return kind == DBG_RES_VEC || kind == DBG_RES_MEM || kind == DBG_RES_MALLOC;
}

void LeakModel::reset()
{
    held.clear();
    released.clear();
    bad.clear();
    nEvents = nUnknown = 0;
}

static const char *acquirer(int kind)
{
    switch (kind) {
    case DBG_RES_VEC: return "AllocVec()";
    case DBG_RES_MEM: return "AllocMem()";
    case DBG_RES_MALLOC: return "malloc()";
    default: return dbg_res_kind_name(kind);
    }
}

void LeakModel::add(const dbg_res_event &e)
{
    nEvents++;
    Key k{ e.kind, e.addr };
    if (!e.is_free) {
        Leak l;
        l.kind = e.kind; l.fn = e.fn; l.addr = e.addr; l.size = e.size; l.seq = e.seq;
        memcpy(l.callers, e.callers, sizeof l.callers);
        held[k].push_back(l);
        // memory handed out again is no longer "freed"
        if (isMemory(e.kind))
            for (int kind : { (int)DBG_RES_VEC, (int)DBG_RES_MEM, (int)DBG_RES_MALLOC }) released.erase(Key{ kind, e.addr });
        return;
    }
    auto report = [&](const std::string &text) {
        Bad b;
        b.text = text;
        memcpy(b.callers, e.callers, sizeof b.callers);
        bad.push_back(b);
    };
    auto it = held.find(k);
    if (it != held.end() && !it->second.empty()) {
        Leak l = it->second.back();
        it->second.pop_back();
        if (it->second.empty()) held.erase(it);
        if (isMemory(e.kind)) released[k] = l.size;
        if (e.kind == DBG_RES_MEM && e.size != l.size) {
            char t[120];
            snprintf(t, sizeof t, "FreeMem() of %lu bytes, but AllocMem() gave %lu", (unsigned long)e.size,
                     (unsigned long)l.size);
            report(t);
        }
        return;
    }
    if (isMemory(e.kind)) {
        // allocated with another family's function?
        for (int other : { (int)DBG_RES_VEC, (int)DBG_RES_MEM, (int)DBG_RES_MALLOC }) {
            if (other == e.kind) continue;
            auto o = held.find(Key{ other, e.addr });
            if (o == held.end() || o->second.empty()) continue;
            o->second.pop_back();
            if (o->second.empty()) held.erase(o);
            report(std::string(dbg_res_function_name(e.fn)) + "() of memory from " + acquirer(other));
            return;
        }
        auto r = released.find(k);
        if (r != released.end()) {
            report(std::string(dbg_res_function_name(e.fn)) + "() of memory that was already freed");
            return;
        }
    }
    nUnknown++;
}

size_t LeakModel::outstanding() const
{
    size_t n = 0;
    for (auto &p : held) n += p.second.size();
    return n;
}

uint64_t LeakModel::outstandingBytes() const
{
    uint64_t n = 0;
    for (auto &p : held)
        if (isMemory(p.first.kind))
            for (auto &l : p.second) n += l.size;
    return n;
}

uint32_t firstProgramCaller(const uint32_t *callers, int n, const CodeResolver &resolve, CodeLoc *loc)
{
    for (int i = 0; i < n && callers[i]; i++) {
        // a return address points after the call: look at the call itself
        CodeLoc c = resolve(callers[i] - 4);
        if (c.inProgram) {
            if (loc) *loc = c;
            return callers[i];
        }
    }
    if (loc && n > 0 && callers[0]) *loc = resolve(callers[0] - 4);
    return 0;
}

std::string LeakModel::formatBytes(uint64_t n)
{
    char b[32];
    if (n < 1024) snprintf(b, sizeof b, "%lu bytes", (unsigned long)n);
    else if (n < 1024 * 1024) snprintf(b, sizeof b, "%.1f KB", n / 1024.0);
    else snprintf(b, sizeof b, "%.1f MB", n / (1024.0 * 1024.0));
    return b;
}

std::string LeakModel::describe(const LeakGroup &g)
{
    char b[96];
    const char *what = g.items.empty() ? dbg_res_kind_name(g.kind) : dbg_res_function_name(g.items.front().fn);
    bool sameFn = true;
    for (auto &l : g.items) if (l.fn != g.items.front().fn) sameFn = false;
    if (!sameFn) what = dbg_res_kind_name(g.kind);
    if (isMemory(g.kind))
        snprintf(b, sizeof b, "%lu x %s, %s", (unsigned long)g.items.size(), what, formatBytes(g.bytes).c_str());
    else
        snprintf(b, sizeof b, "%lu x %s", (unsigned long)g.items.size(), what);
    return b;
}

std::vector<LeakGroup> LeakModel::report(const CodeResolver &resolve) const
{
    std::map<std::pair<int, uint32_t>, LeakGroup> groups;
    std::map<uint32_t, CodeLoc> cache;
    auto cached = [&](uint32_t a) {
        auto it = cache.find(a);
        if (it != cache.end()) return it->second;
        CodeLoc c = resolve(a);
        cache[a] = c;
        return c;
    };
    CodeResolver res = cached;
    for (auto &p : held)
        for (auto &l : p.second) {
            CodeLoc loc;
            uint32_t site = firstProgramCaller(l.callers, DBG_RES_CALLERS, res, &loc);
            LeakGroup &g = groups[{ l.kind, site }];
            if (g.items.empty()) { g.kind = l.kind; g.site = site; g.loc = loc; }
            g.items.push_back(l);
            if (isMemory(l.kind)) g.bytes += l.size;
        }
    std::vector<LeakGroup> out;
    for (auto &p : groups) {
        std::sort(p.second.items.begin(), p.second.items.end(), [](const Leak &a, const Leak &b) { return a.seq < b.seq; });
        out.push_back(p.second);
    }
    std::sort(out.begin(), out.end(), [](const LeakGroup &a, const LeakGroup &b) {
        if ((a.site != 0) != (b.site != 0)) return a.site != 0;    // the program's own first
        if (a.bytes != b.bytes) return a.bytes > b.bytes;
        if (a.items.size() != b.items.size()) return a.items.size() > b.items.size();
        return a.items.front().seq < b.items.front().seq;
    });
    return out;
}

std::vector<LeakProblem> LeakModel::problems(const CodeResolver &resolve) const
{
    std::vector<LeakProblem> out;
    for (auto &b : bad) {
        LeakProblem p;
        p.text = b.text;
        p.site = firstProgramCaller(b.callers, DBG_RES_CALLERS, resolve, &p.loc);
        out.push_back(p);
    }
    return out;
}

} // namespace af
