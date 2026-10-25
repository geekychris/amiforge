// dirsize - how much space a drawer takes, by file type. A C++ Shell tool using the OS:
//   - ReadArgs() for the standard AmigaDOS argument template (try "dirsize ?");
//   - ObtainDirContextTags()/ExamineDir() to walk the drawers recursively (soft links are not
//     followed);
//   - Ctrl-C: dos.library's CheckSignal(SIGBREAKF_CTRL_C) between entries stops the walk cleanly, prints
//     "***Break" and returns RETURN_WARN, as Shell commands do;
//   - std::map to total the sizes by extension, std::sort for the report.
// The OS resources (the parsed arguments, each drawer's context) are RAII classes, so an early
// return or Ctrl-C gives everything back.
//
//   dirsize [DIR] [TOP n] [NOREC]
//     DIR    the drawer (default: the current one)
//     TOP    also list the n biggest files (default 5, 0 for none)
//     NOREC  don't go into sub-drawers
// In AmiForge, Project > Settings > Program arguments sets them (e.g. "SDK:C TOP 3").
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <proto/exec.h>
#include <proto/dos.h>

static const char version[] __attribute__((used)) = "$VER: dirsize 1.0 (25.10.2026)";

// ReadArgs() and FreeArgs()
class Args {
public:
    Args(const char *tmpl, int32 *array) { rda = IDOS->ReadArgs(tmpl, array, nullptr); }
    ~Args() { if (rda) IDOS->FreeArgs(rda); }
    Args(const Args &) = delete;
    Args &operator=(const Args &) = delete;
    bool ok() const { return rda != nullptr; }
private:
    struct RDArgs *rda;
};

// One drawer being read: ObtainDirContextTags() and ReleaseDirContext()
class DirScan {
public:
    explicit DirScan(const std::string &path)
    {
        ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, path.c_str(),
                                         EX_DataFields, EXF_NAME | EXF_SIZE | EXF_TYPE, TAG_END);
    }
    ~DirScan() { if (ctx) IDOS->ReleaseDirContext(ctx); }
    DirScan(const DirScan &) = delete;
    DirScan &operator=(const DirScan &) = delete;
    bool ok() const { return ctx != nullptr; }
    struct ExamineData *next() { return IDOS->ExamineDir(ctx); }
private:
    APTR ctx;
};

struct Total {
    uint64 bytes = 0;
    uint32 files = 0;
};

struct BigFile {
    std::string path;
    uint64 bytes;
};

class Walker {
public:
    Walker(bool recurse, size_t top) : recurse(recurse), top(top) {}

    // false if Ctrl-C stopped it
    bool walk(const std::string &dir)
    {
        DirScan scan(dir);
        if (!scan.ok()) {
            IDOS->PrintFault(IDOS->IoErr(), dir.empty() ? "(current drawer)" : dir.c_str());
            unreadable++;
            return true;
        }
        drawers++;
        while (struct ExamineData *ed = scan.next()) {
            if (IDOS->CheckSignal(SIGBREAKF_CTRL_C)) return false;
            std::string name = (const char *)ed->Name;
            std::string path = join(dir, name);
            if (EXD_IS_LINK(ed)) continue;
            if (EXD_IS_DIRECTORY(ed)) {
                if (recurse && !walk(path)) return false;
                continue;
            }
            if (!EXD_IS_FILE(ed)) continue;
            uint64 size = ed->FileSize < 0 ? 0 : (uint64)ed->FileSize;
            Total &t = byType[extension(name)];
            t.bytes += size;
            t.files++;
            all.bytes += size;
            all.files++;
            remember(path, size);
        }
        // ExamineDir() returns NULL at the end, or on an error
        if (IDOS->IoErr() != ERROR_NO_MORE_ENTRIES) IDOS->PrintFault(IDOS->IoErr(), dir.c_str());
        return true;
    }

    void report(const std::string &dir) const
    {
        std::vector<std::pair<std::string, Total>> rows(byType.begin(), byType.end());
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a.second.bytes != b.second.bytes ? a.second.bytes > b.second.bytes : a.first < b.first;
        });
        std::printf("%-12s %8s %14s %6s\n", "Type", "Files", "Bytes", "%");
        for (const auto &r : rows) {
            unsigned pct = all.bytes ? (unsigned)(r.second.bytes * 1000 / all.bytes) : 0;
            std::printf("%-12s %8lu %14s %3u.%u\n", r.first.c_str(), (unsigned long)r.second.files,
                        grouped(r.second.bytes).c_str(), pct / 10, pct % 10);
        }
        std::printf("%-12s %8lu %14s\n", "Total", (unsigned long)all.files, grouped(all.bytes).c_str());
        std::printf("%lu drawer%s in %s%s\n", (unsigned long)drawers, drawers == 1 ? "" : "s",
                    dir.empty() ? "the current drawer" : dir.c_str(),
                    unreadable ? " (some couldn't be read)" : "");
        if (!biggest.empty()) {
            std::printf("\nBiggest files:\n");
            for (const auto &b : biggest) std::printf("%14s  %s\n", grouped(b.bytes).c_str(), b.path.c_str());
        }
    }

private:
    static std::string join(const std::string &dir, const std::string &name)
    {
        if (dir.empty()) return name;
        char last = dir.back();
        return last == ':' || last == '/' ? dir + name : dir + "/" + name;
    }

    static std::string extension(const std::string &name)
    {
        size_t dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return "(none)";
        std::string e = name.substr(dot);
        for (auto &c : e) c = (char)std::tolower((unsigned char)c);
        return e.size() > 12 ? e.substr(0, 12) : e;
    }

    static std::string grouped(uint64 n)   // 1234567 -> "1,234,567"
    {
        std::string s = std::to_string((unsigned long long)n), out;
        int k = 0;
        for (auto it = s.rbegin(); it != s.rend(); ++it, ++k) {
            if (k && k % 3 == 0) out.insert(out.begin(), ',');
            out.insert(out.begin(), *it);
        }
        return out;
    }

    void remember(const std::string &path, uint64 size)
    {
        if (top == 0) return;
        if (biggest.size() == top && size <= biggest.back().bytes) return;
        BigFile b{ path, size };
        auto at = std::upper_bound(biggest.begin(), biggest.end(), b,
                                   [](const BigFile &x, const BigFile &y) { return x.bytes > y.bytes; });
        biggest.insert(at, b);
        if (biggest.size() > top) biggest.pop_back();
    }

    bool recurse;
    size_t top;
    std::map<std::string, Total> byType;
    Total all;
    uint32 drawers = 0, unreadable = 0;
    std::vector<BigFile> biggest;
};

int main()
{
    enum { ARG_DIR, ARG_TOP, ARG_NOREC, ARG_COUNT };
    int32 array[ARG_COUNT] = {};
    Args args("DIR,TOP/K/N,NOREC/S", array);
    if (!args.ok()) {
        IDOS->PrintFault(IDOS->IoErr(), "dirsize");
        return RETURN_FAIL;
    }
    std::string dir = array[ARG_DIR] ? (const char *)array[ARG_DIR] : "";
    size_t top = array[ARG_TOP] ? (size_t)*(int32 *)array[ARG_TOP] : 5;
    if (top > 100) top = 100;

    Walker walker(!array[ARG_NOREC], top);
    if (!walker.walk(dir)) {
        IDOS->PutStr("***Break\n");
        return RETURN_WARN;
    }
    walker.report(dir);
    return RETURN_OK;
}
