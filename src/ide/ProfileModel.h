// The profiler's aggregation: turns the debugger's IP samples (dbg_profile) into per-function
// and per-line counts. A sample in the program counts for the function and line it is in
// ("self"); a sample outside it (in a library, the kernel, or waiting) counts for the
// nearest program function on its call chain ("in OS") and that call's line. Every program
// function on the chain gets the sample in its "total". Samples are weighted by the time they
// stand for (dbg_sample.weight, in microseconds; 1 ms if unknown), so the counts below are
// microseconds. No OS calls: tested on the host.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdint.h>

#include "LeakModel.h"   // CodeLoc, CodeResolver

namespace af {

struct ProfFunction {
    std::string name;           // demangled; "[OS] newlib.library: memcpy" for OS rows
    std::string file;           // of its hottest line
    int line = 0;
    bool program = true;
    uint64_t self = 0;          // IP inside it (microseconds)
    uint64_t inOS = 0;          // IP in the OS, called (directly or not) from it, nearest program frame
    uint64_t waiting = 0;       // part of inOS: the task was waiting
    uint64_t total = 0;         // anywhere on the chain
};

struct ProfLine {
    uint64_t self = 0, inOS = 0;
};

class ProfileModel {
public:
    void reset();
    void add(const dbg_sample &s);
    void addAll(const dbg_sample *s, int n) { for (int i = 0; i < n; i++) add(s[i]); }
    // Resolves the collected addresses; call once the program is done (or any time to update).
    void finish(const CodeResolver &resolve);

    uint64_t samples() const { return nSamples; }      // how many
    uint64_t time() const { return nTime; }            // microseconds they stand for
    uint64_t waiting() const { return nWaiting; }      // microseconds the task was waiting
    const std::vector<ProfFunction> &functions() const { return funcs; }   // program first, by self+inOS
    // file (as stabs recorded it) -> line -> counts
    const std::map<std::string, std::map<int, ProfLine>> &lines() const { return lineMap; }
    double percent(uint64_t us) const { return nTime ? 100.0 * (double)us / (double)nTime : 0.0; }
    // The end-of-line annotation for a line: "« 12.3% (45 ms)" or "« 0.4% (1 ms) + 31.0% in OS".
    std::string annotation(const ProfLine &l) const;

private:
    struct Raw { uint32_t ip; uint32_t w; uint8_t state, n; uint32_t callers[DBG_SAMPLE_CALLERS]; };
    std::vector<Raw> raw;
    uint64_t nSamples = 0, nTime = 0, nWaiting = 0;
    std::vector<ProfFunction> funcs;
    std::map<std::string, std::map<int, ProfLine>> lineMap;
};

} // namespace af
