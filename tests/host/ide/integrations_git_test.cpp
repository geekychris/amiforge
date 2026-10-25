// GitCore against repositories made with the host's git: loose objects, packs with deltas,
// refs, the index and status.
#include "aftest.h"
#include "ide/GitCore.h"
#include "ide/LineDiff.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <set>
#include <sstream>
#include <algorithm>

using namespace af;

static std::string sh(const std::string &cmd)
{
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    pclose(p);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

static bool listDir(const std::string &dir, std::vector<std::string> &names)
{
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        if (stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) n += "/";
        names.push_back(n);
    }
    closedir(d);
    return true;
}

static void writeFile(const std::string &path, const std::string &s)
{
    FILE *f = fopen(path.c_str(), "wb");
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

static std::string readAll(const std::string &path)
{
    std::string s;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// A repository with history: main.c edited over 6 commits, a subdirectory, a branch.
static std::string makeRepo(bool pack)
{
    char tmpl[] = "/tmp/afgitXXXXXX";
    std::string dir = mkdtemp(tmpl);
    std::string g = "cd " + dir + " && git -c init.defaultBranch=main ";
    sh(g + "init -q");
    sh("cd " + dir + " && git config user.name 'Ann Coder' && git config user.email ann@example.org");
    std::string text;
    for (int i = 1; i <= 40; i++) text += "line " + std::to_string(i) + " of the file\n";
    writeFile(dir + "/main.c", text);
    mkdir((dir + "/src").c_str(), 0755);
    writeFile(dir + "/src/util.c", "int util(void) { return 1; }\n");
    sh(g + "add -A && git commit -q -m 'First commit' --date='2024-01-02T10:00:00+0100'");
    for (int c = 2; c <= 6; c++) {
        // change one line and add one
        size_t p = text.find("line " + std::to_string(c * 5) + " of");
        text.replace(p, 4, "LINE");
        text += "added in commit " + std::to_string(c) + "\n";
        writeFile(dir + "/main.c", text);
        sh(g + "commit -q -a -m 'Commit " + std::to_string(c) + "\n\nDetails here.'");
    }
    writeFile(dir + "/notes.txt", "unrelated\n");
    sh(g + "add notes.txt && git commit -q -m 'Notes'");
    sh(g + "branch feature/x");
    if (pack) sh(g + "gc -q --aggressive");
    return dir;
}

TEST(sha1_vectors)
{
    CHECK_EQ(git::sha1Hex(""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK_EQ(git::sha1Hex("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    std::string m(1000000, 'a');
    CHECK_EQ(git::sha1Hex(m), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
    CHECK_EQ(git::blobId("hello\n"), sh("printf 'hello\\n' | git hash-object --stdin"));
    CHECK_EQ(git::blobId(""), std::string("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));
}

TEST(path_join)
{
    CHECK_EQ(git::pathJoin("RAM:", "x"), std::string("RAM:x"));
    CHECK_EQ(git::pathJoin("DH1:a", ".git"), std::string("DH1:a/.git"));
    CHECK_EQ(git::pathJoin("/tmp/", "x"), std::string("/tmp/x"));
}

static void checkRepo(bool packed)
{
    std::string dir = makeRepo(packed);
    git::Repo r;
    std::string err;
    CHECK(r.open(dir, listDir, &err));
    std::string head = sh("cd " + dir + " && git rev-parse HEAD");
    CHECK_EQ(r.resolve("HEAD"), head);
    CHECK_EQ(r.resolve("main"), head);
    CHECK_EQ(r.headBranch(), std::string("main"));
    std::vector<std::string> br = r.branches();
    CHECK_EQ(br.size(), (size_t)2);
    CHECK(br.size() == 2 && br[0] == "feature/x" && br[1] == "main");

    git::Commit c;
    CHECK(r.readCommit(head, c));
    CHECK_EQ(c.summary(), std::string("Notes"));
    CHECK_EQ(c.author, std::string("Ann Coder"));
    CHECK_EQ(c.email, std::string("ann@example.org"));
    CHECK_EQ(c.parents.size(), (size_t)1);
    CHECK_EQ(c.tree, sh("cd " + dir + " && git rev-parse HEAD^{tree}"));

    std::map<std::string, std::string> files;
    CHECK(r.treeFiles("HEAD" == std::string("HEAD") ? head : head, files));
    CHECK_EQ(files.size(), (size_t)3);
    CHECK_EQ(files["src/util.c"], sh("cd " + dir + " && git rev-parse HEAD:src/util.c"));
    CHECK_EQ(r.blobAt(head, "main.c"), sh("cd " + dir + " && git rev-parse HEAD:main.c"));
    CHECK_EQ(r.blobAt(head, "src/util.c"), files["src/util.c"]);
    CHECK_EQ(r.blobAt(head, "nope.c"), std::string(""));
    CHECK_EQ(r.blobAt(head, "src"), std::string(""));

    std::string blob;
    CHECK(r.readBlob(r.blobAt(head, "main.c"), blob));
    CHECK_EQ(blob, readAll(dir + "/main.c"));
    // an old version (packed: a delta against the newer one)
    std::string first = sh("cd " + dir + " && git rev-list --max-parents=0 HEAD");
    CHECK(r.readBlob(r.blobAt(first, "main.c"), blob));
    CHECK_EQ(blob, sh("cd " + dir + " && git show " + first + ":main.c") + "\n");

    auto hist = r.fileHistory("HEAD", "main.c", 50);
    std::string expect = sh("cd " + dir + " && git log --format=%H -- main.c");
    std::string got;
    for (auto &v : hist) got += (got.empty() ? "" : "\n") + v.commit.id;
    CHECK_EQ(got, expect);
    CHECK_EQ(hist.size(), (size_t)6);
    CHECK_EQ(r.fileHistory("HEAD", "main.c", 2).size(), (size_t)2);
    CHECK_EQ(r.fileHistory("HEAD", "notes.txt", 10).size(), (size_t)1);

    std::vector<git::IndexEntry> idx;
    CHECK(r.readIndex(idx));
    CHECK_EQ(idx.size(), (size_t)3);
    sh("rm -rf " + dir);
}

TEST(repo_loose) { checkRepo(false); }
TEST(repo_packed) { checkRepo(true); }

TEST(status_matches_git)
{
    std::string dir = makeRepo(true);
    std::string g = "cd " + dir + " && git ";
    writeFile(dir + "/main.c", readAll(dir + "/main.c") + "more\n");     // modified, not staged
    writeFile(dir + "/src/util.c", "int util(void) { return 2; }\n");   // modified and staged
    sh(g + "add src/util.c");
    writeFile(dir + "/new.c", "new\n");                                   // untracked
    writeFile(dir + "/staged.c", "staged\n");
    sh(g + "add staged.c");                                               // added
    sh(g + "rm -q --cached notes.txt");                                   // deleted from index, still on disk
    git::Repo r;
    CHECK(r.open(dir, listDir));
    std::map<std::string, std::string> head, work;
    CHECK(r.treeFiles(r.resolve("HEAD"), head));
    std::vector<git::IndexEntry> idx;
    CHECK(r.readIndex(idx));
    for (const char *f : { "main.c", "src/util.c", "new.c", "staged.c", "notes.txt" })
        work[f] = git::blobId(readAll(dir + "/" + f));
    auto st = git::computeStatus(head, idx, work);
    std::string got;
    for (auto &s : st) got += std::string(1, s.staged) + s.work + " " + s.path + "\n";
    // git status --porcelain, sorted by path then status
    std::vector<std::pair<std::string, std::string>> lines;   // (path, status)
    std::istringstream is(sh(g + "status --porcelain"));
    std::string line;
    while (std::getline(is, line)) lines.push_back({ line.substr(3), line.substr(0, 2) });
    std::sort(lines.begin(), lines.end(), [](const std::pair<std::string, std::string> &a, const std::pair<std::string, std::string> &b) {
        return a.first != b.first ? a.first < b.first : a.second > b.second;
    });
    std::string want;
    for (auto &l : lines) want += l.second + " " + l.first + "\n";
    CHECK_EQ(got, want);
    sh("rm -rf " + dir);
}

TEST(index_rejects_garbage)
{
    std::vector<git::IndexEntry> out;
    std::string err;
    CHECK(!git::parseIndex("nonsense", out, &err));
    CHECK(!err.empty());
    CHECK(!git::parseIndex(std::string("DIRC\0\0\0\x09\0\0\0\0", 12), out, &err));
}

TEST(blame_matches_git)
{
    std::string dir = makeRepo(true);
    git::Repo r;
    CHECK(r.open(dir, listDir));
    auto hist = r.fileHistory("HEAD", "main.c", 50);
    std::vector<std::vector<std::string>> versions;
    for (auto &v : hist) {
        std::string b;
        r.readBlob(v.blob, b);
        versions.push_back(splitLines(b));
    }
    std::string text = readAll(dir + "/main.c");
    std::vector<std::string> cur = splitLines(text);
    auto who = blameLines(cur, versions);
    // git blame --porcelain: the commit of each line
    std::string porc = sh("cd " + dir + " && git blame -l -s --root main.c | cut -c1-40");
    std::istringstream is(porc);
    std::string line;
    size_t i = 0;
    int mismatches = 0;
    while (std::getline(is, line) && i < who.size()) {
        std::string mine = who[i] >= 0 ? hist[who[i]].commit.id : "";
        if (mine != line) mismatches++;
        i++;
    }
    CHECK_EQ(i, who.size());
    CHECK_EQ(mismatches, 0);
    // an uncommitted line
    cur.insert(cur.begin() + 3, "brand new");
    who = blameLines(cur, versions);
    CHECK_EQ(who[3], -1);
    CHECK(who[0] >= 0);
    sh("rm -rf " + dir);
}
