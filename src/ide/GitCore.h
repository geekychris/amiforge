// Reading git repositories directly: loose and packed objects (with deltas), refs, trees,
// commits and the index. SimpleGit is slow to start (a second or more per command on a QEMU
// guest), so everything that only reads - change markers, the Git tab's status, blame - is
// done here, in-process. Writing (add, reset, commit, checkout) still goes through sgit.
//
// Portable: stdio + zlib only, so it builds and is tested on the host (tests/host/ide).
// Directory listing is the one OS-specific thing; the caller provides it.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdint.h>

namespace af {
namespace git {

std::string sha1Hex(const std::string &data);          // 40 lower-case hex digits
std::string blobId(const std::string &content);        // the id git gives a file with this content
std::string pathJoin(const std::string &dir, const std::string &name);   // Amiga- and Unix-style

struct Commit {
    std::string id, tree;
    std::vector<std::string> parents;
    std::string author, email;
    int64_t time = 0;          // seconds since 1970 (UTC)
    std::string tz;            // "+0100"
    std::string message;       // the whole message
    std::string summary() const;   // its first line
};

struct TreeEntry {
    std::string mode, name, id;
    bool isTree() const { return mode == "40000" || mode == "040000"; }
};

struct IndexEntry {
    std::string path, id;
    uint32_t mode = 0;
    uint32_t mtime = 0, size = 0;
    int stage = 0;
};
bool parseIndex(const std::string &data, std::vector<IndexEntry> &out, std::string *err = nullptr);

class Repo {
public:
    // List a directory's entries (names only; directories with a trailing '/').
    using ListDirFn = std::function<bool(const std::string &dir, std::vector<std::string> &names)>;

    Repo();
    ~Repo();
    Repo(const Repo &) = delete;
    Repo &operator=(const Repo &) = delete;

    // workDir is the working tree (the drawer holding .git).
    bool open(const std::string &workDir, ListDirFn listDir, std::string *err = nullptr);
    bool isOpen() const { return !gitDir.empty(); }
    const std::string &workDir() const { return work; }

    bool readObject(const std::string &id, std::string &type, std::string &data);
    std::string resolve(const std::string &ref);      // "HEAD", "main", "refs/heads/x", or an id -> id ("" if none)
    std::string headBranch();                          // branch HEAD points at; "" when detached
    std::vector<std::string> branches();               // local branch names, sorted
    bool readCommit(const std::string &id, Commit &out);
    bool readTree(const std::string &id, std::vector<TreeEntry> &out);
    // All files of a tree (or a commit's tree): "dir/file" -> blob id.
    bool treeFiles(const std::string &treeOrCommit, std::map<std::string, std::string> &out);
    // The blob id of path (relative to the work tree, '/'-separated) in a commit; "" if absent.
    std::string blobAt(const std::string &commit, const std::string &path);
    bool readBlob(const std::string &id, std::string &out);
    bool readIndex(std::vector<IndexEntry> &out);
    // Commits that changed `path`, newest first, following first parents from `start`, at most
    // `limit`. Each comes with the file's blob id in that commit.
    struct FileVersion { Commit commit; std::string blob; };
    std::vector<FileVersion> fileHistory(const std::string &start, const std::string &path, int limit);

    std::string lastError;

private:
    struct Pack;
    bool readLoose(const std::string &id, std::string &type, std::string &data);
    bool readPacked(const std::string &id, std::string &type, std::string &data);
    bool packObjectAt(Pack &p, uint64_t offset, int &type, std::string &data, int depth);
    void loadPacks();
    std::string readRefFile(const std::string &ref);
    std::string gitDir, work;
    ListDirFn listDir;
    std::vector<Pack *> packs;
    bool packsLoaded = false;
    std::map<std::string, std::string> packedRefs;
    bool packedRefsLoaded = false;
    // decoded objects, by id (trees and commits are read again and again while walking history)
    std::map<std::string, std::pair<std::string, std::string>> cache;
    size_t cacheBytes = 0;
};

// The status of each changed file: X = index vs HEAD, Y = work tree vs index, like
// `git status --short`: 'M' modified, 'A' added, 'D' deleted, '?' untracked, ' ' unchanged.
struct FileStatus {
    std::string path;
    char staged = ' ', work = ' ';
};
// head: HEAD's files; index: the index; workIds: blob ids of the files in the work tree
// (missing = deleted; files not in the index are untracked).
std::vector<FileStatus> computeStatus(const std::map<std::string, std::string> &head,
                                      const std::vector<IndexEntry> &index,
                                      const std::map<std::string, std::string> &workIds);

} // namespace git
} // namespace af
