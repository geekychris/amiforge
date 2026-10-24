// Strings, Amiga paths and files.
#include "App.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

#include <proto/exec.h>
#include <proto/dos.h>

namespace af {

std::string strprintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof buf)
        return std::string(buf, n < 0 ? 0 : n);
    std::string s(n, '\0');
    va_start(ap, fmt);
    vsnprintf(&s[0], n + 1, fmt, ap);
    va_end(ap);
    return s;
}

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> splitWords(const std::string &s)
{
    std::vector<std::string> out;
    std::string w;
    bool inq = false, have = false;
    for (char c : s) {
        if (c == '"') { inq = !inq; have = true; continue; }
        if (!inq && isspace((unsigned char)c)) {
            if (have) out.push_back(w);
            w.clear(); have = false;
            continue;
        }
        w += c; have = true;
    }
    if (have) out.push_back(w);
    return out;
}

std::string fileName(const std::string &path)
{
    size_t p = path.find_last_of("/:");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string dirName(const std::string &path)
{
    size_t p = path.find_last_of("/:");
    if (p == std::string::npos) return "";
    return path[p] == ':' ? path.substr(0, p + 1) : path.substr(0, p);
}

std::string joinPath(const std::string &dir, const std::string &name)
{
    if (dir.empty() || name.find(':') != std::string::npos) return name;
    char last = dir.back();
    return (last == ':' || last == '/') ? dir + name : dir + "/" + name;
}

std::string extension(const std::string &path)
{
    std::string n = fileName(path);
    size_t p = n.rfind('.');
    if (p == std::string::npos) return "";
    std::string e = n.substr(p + 1);
    for (auto &c : e) c = tolower((unsigned char)c);
    return e;
}

std::string relativeTo(const std::string &dir, const std::string &path)
{
    if (dir.empty() || path.size() <= dir.size()) return path;
    if (strncasecmp(dir.c_str(), path.c_str(), dir.size()) != 0) return path;
    char last = dir.back();
    if (last == ':' || last == '/') return path.substr(dir.size());
    if (path[dir.size()] == '/') return path.substr(dir.size() + 1);
    return path;
}

std::string absolutePath(const std::string &path)
{
    BPTR lock = IDOS->Lock(path.c_str(), SHARED_LOCK);
    if (!lock) {
        // a file that doesn't exist yet: resolve its directory
        std::string d = dirName(path);
        if (d.empty() || d == path) return path;
        BPTR dl = IDOS->Lock(d.c_str(), SHARED_LOCK);
        if (!dl) return path;
        char buf[1024];
        std::string r = path;
        if (IDOS->NameFromLock(dl, buf, sizeof buf)) r = joinPath(buf, fileName(path));
        IDOS->UnLock(dl);
        return r;
    }
    char buf[1024];
    std::string r = path;
    if (IDOS->NameFromLock(lock, buf, sizeof buf)) r = buf;
    IDOS->UnLock(lock);
    return r;
}

bool readFile(const std::string &path, std::string &out)
{
    BPTR fh = IDOS->Open(path.c_str(), MODE_OLDFILE);
    if (!fh) return false;
    out.clear();
    char buf[16384];
    int32 n;
    while ((n = IDOS->Read(fh, buf, sizeof buf)) > 0) out.append(buf, n);
    IDOS->Close(fh);
    return n >= 0;
}

bool writeFile(const std::string &path, const std::string &data)
{
    BPTR fh = IDOS->Open(path.c_str(), MODE_NEWFILE);
    if (!fh) return false;
    bool ok = IDOS->Write(fh, data.data(), data.size()) == (int32)data.size();
    IDOS->Close(fh);
    return ok;
}

bool fileExists(const std::string &path)
{
    BPTR lock = IDOS->Lock(path.c_str(), SHARED_LOCK);
    if (lock) IDOS->UnLock(lock);
    return lock != 0;
}

int64_t fileTime(const std::string &path)
{
    struct ExamineData *ed = IDOS->ExamineObjectTags(EX_StringNameInput, path.c_str(), TAG_END);
    if (!ed) return -1;
    int64_t t = ((int64_t)ed->Date.ds_Days * 24 * 60 + ed->Date.ds_Minute) * 60 * 50 + ed->Date.ds_Tick;
    IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    return t;
}

bool sameFile(const std::string &a, const std::string &b)
{
    if (strcasecmp(a.c_str(), b.c_str()) == 0) return true;
    BPTR la = IDOS->Lock(a.c_str(), SHARED_LOCK);
    if (!la) return false;
    BPTR lb = IDOS->Lock(b.c_str(), SHARED_LOCK);
    bool same = lb && IDOS->SameLock(la, lb) == LOCK_SAME;
    if (lb) IDOS->UnLock(lb);
    IDOS->UnLock(la);
    return same;
}

bool isCppFile(const std::string &path)
{
    std::string e = extension(path);
    return e == "cpp" || e == "cc" || e == "cxx" || e == "c++";
}

bool isAsmFile(const std::string &path)
{
    std::string e = extension(path);
    return e == "s" || e == "asm";
}

bool isSourceFile(const std::string &path)
{
    return extension(path) == "c" || isCppFile(path) || isAsmFile(path);
}

} // namespace af
