// Line diffs, editor change markers and blame. Portable (tested on the host).
#pragma once

#include <string>
#include <vector>

namespace af {

std::vector<std::string> splitLines(const std::string &text);   // "a\nb\n" -> {"a","b"}; CR/LF tolerant

// A difference between a (old) and b (new): a[aStart, aStart+aCount) became b[bStart, bStart+bCount).
struct Hunk { int aStart, aCount, bStart, bCount; };
std::vector<Hunk> diffLines(const std::vector<std::string> &a, const std::vector<std::string> &b);

// For each line of the new text: what happened to it compared with the old.
enum class LineChange { None, Added, Modified, DeletedAbove };   // DeletedAbove: lines were removed just above
std::vector<LineChange> changeMarkers(const std::vector<std::string> &oldLines, const std::vector<std::string> &newLines);

// A unified diff (for showing a file's changes): "--- a/x", "+++ b/x", "@@ ... @@" and lines.
std::string unifiedDiff(const std::vector<std::string> &a, const std::vector<std::string> &b,
                        const std::string &name, int context = 3);

// Blame. versions[0] is the newest version of the file that is committed, versions[i+1] the
// one before versions[i]. Returns, for each line of `current` (which may have uncommitted
// changes), the index of the version that introduced it; -1 = not committed yet;
// versions.size()-1 also covers lines that are older than the oldest version looked at.
std::vector<int> blameLines(const std::vector<std::string> &current,
                            const std::vector<std::vector<std::string>> &versions);

} // namespace af
