// Line diffs: the common prefix and suffix are trimmed, and what is left in the middle is
// matched with a longest-common-subsequence table (falling back to "all replaced" when the
// middle is too big for one, which only happens with wholesale rewrites).
#include "LineDiff.h"

#include <algorithm>
#include <stdint.h>

namespace af {

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        size_t l = e;
        if (l > p && text[l - 1] == '\r') l--;
        out.push_back(text.substr(p, l - p));
        p = e + 1;
    }
    return out;
}

std::vector<Hunk> diffLines(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    int n = (int)a.size(), m = (int)b.size();
    int pre = 0;
    while (pre < n && pre < m && a[pre] == b[pre]) pre++;
    int suf = 0;
    while (suf < n - pre && suf < m - pre && a[n - 1 - suf] == b[m - 1 - suf]) suf++;
    int an = n - pre - suf, bn = m - pre - suf;
    std::vector<Hunk> out;
    if (an == 0 && bn == 0) return out;
    if (an == 0 || bn == 0 || (int64_t)an * bn > 6000000) {
        out.push_back({ pre, an, pre, bn });
        return out;
    }
    // LCS lengths from the end: L[i][j] = LCS of a[pre+i..] and b[pre+j..]
    std::vector<uint16_t> L((size_t)(an + 1) * (bn + 1), 0);
    auto at = [&](int i, int j) -> uint16_t & { return L[(size_t)i * (bn + 1) + j]; };
    for (int i = an - 1; i >= 0; i--)
        for (int j = bn - 1; j >= 0; j--)
            at(i, j) = a[pre + i] == b[pre + j] ? (uint16_t)(at(i + 1, j + 1) + 1) : std::max(at(i + 1, j), at(i, j + 1));
    int i = 0, j = 0;
    Hunk h{ -1, 0, -1, 0 };
    auto flush = [&]() {
        if (h.aStart >= 0 && (h.aCount || h.bCount)) out.push_back(h);
        h = { -1, 0, -1, 0 };
    };
    while (i < an || j < bn) {
        if (i < an && j < bn && a[pre + i] == b[pre + j]) {
            flush();
            i++; j++;
            continue;
        }
        if (h.aStart < 0) h = { pre + i, 0, pre + j, 0 };
        if (j < bn && (i >= an || at(i, j + 1) >= at(i + 1, j))) { h.bCount++; j++; }
        else { h.aCount++; i++; }
    }
    flush();
    return out;
}

std::vector<LineChange> changeMarkers(const std::vector<std::string> &oldLines, const std::vector<std::string> &newLines)
{
    std::vector<LineChange> out(newLines.size(), LineChange::None);
    for (auto &h : diffLines(oldLines, newLines)) {
        if (h.bCount == 0) {
            // removed lines: mark the line that now follows them (or the last line)
            int l = std::min(h.bStart, (int)newLines.size() - 1);
            if (l >= 0 && out[l] == LineChange::None) out[l] = LineChange::DeletedAbove;
            continue;
        }
        int modified = std::min(h.aCount, h.bCount);
        for (int k = 0; k < h.bCount; k++)
            out[h.bStart + k] = k < modified ? LineChange::Modified : LineChange::Added;
    }
    return out;
}

std::string unifiedDiff(const std::vector<std::string> &a, const std::vector<std::string> &b,
                        const std::string &name, int context)
{
    std::vector<Hunk> hs = diffLines(a, b);
    if (hs.empty()) return "";
    std::string out = "--- a/" + name + "\n+++ b/" + name + "\n";
    size_t k = 0;
    while (k < hs.size()) {
        // group hunks whose context overlaps
        size_t e = k;
        while (e + 1 < hs.size() && hs[e + 1].aStart - (hs[e].aStart + hs[e].aCount) <= 2 * context) e++;
        int a0 = std::max(0, hs[k].aStart - context), b0 = std::max(0, hs[k].bStart - context);
        int a1 = std::min((int)a.size(), hs[e].aStart + hs[e].aCount + context);
        int b1 = std::min((int)b.size(), hs[e].bStart + hs[e].bCount + context);
        out += "@@ -" + std::to_string(a0 + 1) + "," + std::to_string(a1 - a0) + " +" + std::to_string(b0 + 1) + "," +
               std::to_string(b1 - b0) + " @@\n";
        int ai = a0;
        for (size_t q = k; q <= e; q++) {
            for (; ai < hs[q].aStart; ai++) out += " " + a[ai] + "\n";
            for (int t = 0; t < hs[q].aCount; t++) out += "-" + a[hs[q].aStart + t] + "\n";
            for (int t = 0; t < hs[q].bCount; t++) out += "+" + b[hs[q].bStart + t] + "\n";
            ai = hs[q].aStart + hs[q].aCount;
        }
        for (; ai < a1; ai++) out += " " + a[ai] + "\n";
        k = e + 1;
    }
    return out;
}

// For each line of b: the line of a it is the same line as, or -1 if it's new in b.
static std::vector<int> lineMap(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    std::vector<int> map(b.size(), -1);
    std::vector<Hunk> hs = diffLines(a, b);
    int ai = 0, bi = 0;
    for (auto &h : hs) {
        while (bi < h.bStart) map[bi++] = ai++;
        bi += h.bCount;
        ai += h.aCount;
    }
    while (bi < (int)b.size()) map[bi++] = ai++;
    return map;
}

std::vector<int> blameLines(const std::vector<std::string> &current,
                            const std::vector<std::vector<std::string>> &versions)
{
    std::vector<int> out(current.size(), -1);
    if (versions.empty()) return out;
    // where each current line is in the version being looked at (-1: not there)
    std::vector<int> pos = lineMap(versions[0], current);
    for (size_t v = 0; v < versions.size(); v++) {
        bool last = v + 1 == versions.size();
        std::vector<int> toOlder;
        if (!last) toOlder = lineMap(versions[v + 1], versions[v]);
        bool any = false;
        for (size_t l = 0; l < current.size(); l++) {
            if (out[l] != -1 || pos[l] < 0) continue;
            int older = last ? -1 : toOlder[pos[l]];
            if (older < 0) out[l] = (int)v;      // not in the older version: this one added it
            else any = true;
            pos[l] = older;
        }
        if (!any) break;
    }
    // lines present in the newest committed version but never resolved can't happen; lines
    // that are new in `current` keep -1 (not committed yet)
    return out;
}

} // namespace af
