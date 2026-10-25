// The profiler's aggregation (see ProfileModel.h).
#include "ProfileModel.h"

#include <stdio.h>
#include <algorithm>
#include <set>

namespace af {

void ProfileModel::reset()
{
    raw.clear();
    funcs.clear();
    lineMap.clear();
    nSamples = nWaiting = 0;
}

void ProfileModel::add(const dbg_sample &s)
{
    Raw r;
    r.ip = s.ip;
    r.state = s.state;
    r.n = s.ncallers > DBG_SAMPLE_CALLERS ? DBG_SAMPLE_CALLERS : s.ncallers;
    for (int i = 0; i < DBG_SAMPLE_CALLERS; i++) r.callers[i] = i < r.n ? s.callers[i] : 0;
    raw.push_back(r);
    nSamples++;
    if (s.state == DBG_SAMPLE_WAITING) nWaiting++;
}

void ProfileModel::finish(const CodeResolver &resolve)
{
    funcs.clear();
    lineMap.clear();
    std::map<uint32_t, CodeLoc> cache;
    auto loc = [&](uint32_t a) -> const CodeLoc & {
        auto it = cache.find(a);
        if (it != cache.end()) return it->second;
        return cache[a] = resolve(a);
    };
    std::map<std::string, ProfFunction> byName;
    std::map<std::string, std::map<std::pair<std::string, int>, uint64_t>> hottest;   // func -> line -> count
    auto func = [&](const CodeLoc &c, bool program) -> ProfFunction & {
        std::string name = c.function.empty() ? "??" : c.function;
        if (!program) name = "[OS] " + name;
        ProfFunction &f = byName[name];
        if (f.name.empty()) { f.name = name; f.program = program; }
        return f;
    };
    for (auto &r : raw) {
        const CodeLoc ip = loc(r.ip);
        std::set<std::string> seen;   // for totals: each function once per sample
        if (ip.inProgram) {
            ProfFunction &f = func(ip, true);
            f.self++;
            if (!ip.file.empty() && ip.line > 0) {
                lineMap[ip.file][ip.line].self++;
                hottest[f.name][{ ip.file, ip.line }]++;
            }
            f.total++;
            seen.insert(f.name);
        } else {
            // the OS row (by module/function), and the nearest program frame that led there
            ProfFunction &os = func(ip, false);
            os.self++;
            if (r.state == DBG_SAMPLE_WAITING) os.waiting++;
            for (int i = 0; i < r.n; i++) {
                if (!r.callers[i]) break;
                const CodeLoc c = loc(r.callers[i] - 4);   // the call instruction
                if (!c.inProgram) continue;
                ProfFunction &f = func(c, true);
                f.inOS++;
                if (r.state == DBG_SAMPLE_WAITING) f.waiting++;
                if (!c.file.empty() && c.line > 0) {
                    lineMap[c.file][c.line].inOS++;
                    hottest[f.name][{ c.file, c.line }]++;
                }
                break;
            }
        }
        // totals: every program function on the chain (the LR may repeat the first saved return)
        for (int i = 0; i < r.n; i++) {
            if (!r.callers[i]) break;
            const CodeLoc c = loc(r.callers[i] - 4);
            if (!c.inProgram) continue;
            std::string name = c.function.empty() ? "??" : c.function;
            if (seen.insert(name).second) func(c, true).total++;
        }
    }
    for (auto &p : byName) {
        ProfFunction f = p.second;
        auto h = hottest.find(f.name);
        if (h != hottest.end()) {
            uint64_t best = 0;
            for (auto &l : h->second)
                if (l.second > best) { best = l.second; f.file = l.first.first; f.line = l.first.second; }
        }
        if (!f.program) f.total = f.self;
        funcs.push_back(f);
    }
    std::sort(funcs.begin(), funcs.end(), [](const ProfFunction &a, const ProfFunction &b) {
        if (a.program != b.program) return a.program;
        uint64_t ka = a.self + a.inOS, kb = b.self + b.inOS;
        if (ka != kb) return ka > kb;
        if (a.total != b.total) return a.total > b.total;
        return a.name < b.name;
    });
}

std::string ProfileModel::annotation(const ProfLine &l) const
{
    char b[96];
    if (l.self && l.inOS)
        snprintf(b, sizeof b, "\xAB %.1f%% (%lu) + %.1f%% in OS", percent(l.self), (unsigned long)l.self, percent(l.inOS));
    else if (l.self)
        snprintf(b, sizeof b, "\xAB %.1f%% (%lu)", percent(l.self), (unsigned long)l.self);
    else
        snprintf(b, sizeof b, "\xAB %.1f%% in OS (%lu)", percent(l.inOS), (unsigned long)l.inOS);
    return b;
}

} // namespace af
