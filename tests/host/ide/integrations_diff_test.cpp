// Line diffs, change markers, unified diffs and blame.
#include "aftest.h"
#include "ide/LineDiff.h"

using namespace af;

static std::vector<std::string> L(std::initializer_list<const char *> l)
{
    std::vector<std::string> v;
    for (auto s : l) v.push_back(s);
    return v;
}

static std::string marks(const std::vector<LineChange> &m)
{
    std::string s;
    for (auto c : m) s += c == LineChange::None ? '.' : c == LineChange::Added ? 'A' : c == LineChange::Modified ? 'M' : 'D';
    return s;
}

TEST(split_lines)
{
    CHECK_EQ(splitLines("a\nb\n").size(), (size_t)2);
    CHECK_EQ(splitLines("a\r\nb").size(), (size_t)2);
    CHECK_EQ(splitLines("a\r\nb")[0], std::string("a"));
    CHECK_EQ(splitLines("").size(), (size_t)0);
    CHECK_EQ(splitLines("\n\n").size(), (size_t)2);
}

TEST(diff_identical_and_empty)
{
    CHECK(diffLines(L({"a", "b"}), L({"a", "b"})).empty());
    auto h = diffLines(L({}), L({"x"}));
    CHECK_EQ(h.size(), (size_t)1);
    CHECK(h[0].aCount == 0 && h[0].bCount == 1);
    h = diffLines(L({"x", "y"}), L({}));
    CHECK(h.size() == 1 && h[0].aCount == 2 && h[0].bCount == 0);
}

TEST(change_markers)
{
    auto old = L({"one", "two", "three", "four", "five"});
    CHECK_EQ(marks(changeMarkers(old, L({"one", "two", "three", "four", "five"}))), std::string("....."));
    CHECK_EQ(marks(changeMarkers(old, L({"one", "TWO", "three", "four", "five"}))), std::string(".M..."));
    CHECK_EQ(marks(changeMarkers(old, L({"one", "two", "new", "three", "four", "five"}))), std::string("..A..."));
    CHECK_EQ(marks(changeMarkers(old, L({"one", "three", "four", "five"}))), std::string(".D.."));
    CHECK_EQ(marks(changeMarkers(old, L({"one", "two", "three", "four"}))), std::string("...D"));
    CHECK_EQ(marks(changeMarkers(old, L({"one", "TWO", "extra", "three", "four", "five"}))), std::string(".MA..."));
    CHECK_EQ(marks(changeMarkers(L({}), L({"a", "b"}))), std::string("AA"));
    // two separate edits
    CHECK_EQ(marks(changeMarkers(old, L({"ONE", "two", "three", "FOUR", "five", "six"}))), std::string("M..M.A"));
}

TEST(unified_diff_format)
{
    auto a = L({"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    auto b = a;
    b[4] = "five";
    std::string d = unifiedDiff(a, b, "x.c");
    CHECK_EQ(d, std::string("--- a/x.c\n+++ b/x.c\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n"));
    CHECK_EQ(unifiedDiff(a, a, "x.c"), std::string(""));
}

TEST(diff_reorders_minimally)
{
    // LCS keeps the longest run in place
    auto h = diffLines(L({"a", "b", "c", "d"}), L({"b", "c", "d", "a"}));
    int del = 0, add = 0;
    for (auto &x : h) { del += x.aCount; add += x.bCount; }
    CHECK_EQ(del, 1);
    CHECK_EQ(add, 1);
}

TEST(blame_simple)
{
    // v0 newest
    std::vector<std::vector<std::string>> v = {
        L({"a", "B", "c", "d"}),     // changed b->B, added d
        L({"a", "b", "c"}),          // added c
        L({"a", "b"}),               // first
    };
    auto who = blameLines(L({"a", "B", "c", "d", "new"}), v);
    CHECK_EQ(who.size(), (size_t)5);
    CHECK_EQ(who[0], 2);
    CHECK_EQ(who[1], 0);
    CHECK_EQ(who[2], 1);
    CHECK_EQ(who[3], 0);
    CHECK_EQ(who[4], -1);
    // capped history: everything older is attributed to the oldest version looked at
    auto who2 = blameLines(L({"a", "B", "c", "d"}), { v[0], v[1] });
    CHECK_EQ(who2[0], 1);
    CHECK_EQ(who2[2], 1);
}
