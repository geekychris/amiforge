// Reading git repositories directly (see GitCore.h).
#include "GitCore.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <set>

#include <zlib.h>

namespace af {
namespace git {

// --- SHA-1 ---

namespace {
struct Sha1 {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;
    static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void block(const uint8_t *p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void add(const void *data, size_t n)
    {
        const uint8_t *p = (const uint8_t *)data;
        total += n;
        while (n) {
            size_t k = std::min(n, 64 - used);
            memcpy(buf + used, p, k);
            used += k; p += k; n -= k;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::string hex()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        add(&pad, 1);
        uint8_t zero = 0;
        while (used != 56) add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(len, 8);
        static const char *dig = "0123456789abcdef";
        std::string s;
        for (int i = 0; i < 5; i++)
            for (int k = 28; k >= 0; k -= 4) s += dig[(h[i] >> k) & 15];
        return s;
    }
};

std::string toHex(const uint8_t *p, int n)
{
    static const char *dig = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; i++) { s += dig[p[i] >> 4]; s += dig[p[i] & 15]; }
    return s;
}

bool fromHex(const std::string &s, uint8_t *out)
{
    if (s.size() != 40) return false;
    for (int i = 0; i < 20; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

bool isHexId(const std::string &s)
{
    uint8_t b[20];
    return fromHex(s, b);
}

bool readWhole(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string trimEol(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Inflate a whole zlib stream held in memory.
bool inflateAll(const uint8_t *src, size_t len, std::string &out, size_t expect = 0)
{
    z_stream z;
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) return false;
    out.clear();
    if (expect) out.reserve(expect);
    z.next_in = (Bytef *)src;
    z.avail_in = (uInt)len;
    char buf[32768];
    int r;
    do {
        z.next_out = (Bytef *)buf;
        z.avail_out = sizeof buf;
        r = inflate(&z, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&z); return false; }
        out.append(buf, sizeof buf - z.avail_out);
    } while (r != Z_STREAM_END && (z.avail_in > 0 || z.avail_out == 0));
    inflateEnd(&z);
    return r == Z_STREAM_END;
}

// Inflate a zlib stream starting at the current position of a file (compressed size unknown).
bool inflateFromFile(FILE *f, size_t expect, std::string &out)
{
    z_stream z;
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) return false;
    out.clear();
    out.reserve(expect);
    uint8_t in[16384];
    char buf[32768];
    int r = Z_OK;
    while (r != Z_STREAM_END) {
        if (z.avail_in == 0) {
            size_t n = fread(in, 1, sizeof in, f);
            if (n == 0) break;
            z.next_in = in;
            z.avail_in = (uInt)n;
        }
        z.next_out = (Bytef *)buf;
        z.avail_out = sizeof buf;
        r = inflate(&z, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) break;
        out.append(buf, sizeof buf - z.avail_out);
        if (r == Z_BUF_ERROR && z.avail_in) break;
    }
    inflateEnd(&z);
    return r == Z_STREAM_END && (expect == 0 || out.size() == expect);
}

// git's delta format: base size, result size, then copy/insert instructions
bool applyDelta(const std::string &base, const std::string &delta, std::string &out)
{
    const uint8_t *p = (const uint8_t *)delta.data(), *e = p + delta.size();
    auto varint = [&](uint64_t &v) {
        v = 0;
        int shift = 0;
        while (p < e) {
            uint8_t c = *p++;
            v |= (uint64_t)(c & 0x7F) << shift;
            shift += 7;
            if (!(c & 0x80)) return true;
        }
        return false;
    };
    uint64_t bsz, rsz;
    if (!varint(bsz) || !varint(rsz) || bsz != base.size()) return false;
    out.clear();
    out.reserve(rsz);
    while (p < e) {
        uint8_t op = *p++;
        if (op & 0x80) {
            uint32_t off = 0, size = 0;
            for (int i = 0; i < 4; i++) if (op & (1 << i)) { if (p >= e) return false; off |= (uint32_t)*p++ << (8 * i); }
            for (int i = 0; i < 3; i++) if (op & (0x10 << i)) { if (p >= e) return false; size |= (uint32_t)*p++ << (8 * i); }
            if (size == 0) size = 0x10000;
            if ((uint64_t)off + size > base.size()) return false;
            out.append(base, off, size);
        } else if (op) {
            if (p + op > e) return false;
            out.append((const char *)p, op);
            p += op;
        } else {
            return false;
        }
    }
    return out.size() == rsz;
}

const char *typeName(int t)
{
    switch (t) { case 1: return "commit"; case 2: return "tree"; case 3: return "blob"; case 4: return "tag"; }
    return "";
}
} // namespace

std::string sha1Hex(const std::string &data)
{
    Sha1 s;
    s.add(data.data(), data.size());
    return s.hex();
}

std::string blobId(const std::string &content)
{
    Sha1 s;
    std::string hdr = "blob " + std::to_string(content.size());
    s.add(hdr.c_str(), hdr.size() + 1);   // with its NUL
    s.add(content.data(), content.size());
    return s.hex();
}

std::string pathJoin(const std::string &dir, const std::string &name)
{
    if (dir.empty()) return name;
    char last = dir.back();
    return (last == ':' || last == '/') ? dir + name : dir + "/" + name;
}

std::string Commit::summary() const
{
    size_t e = message.find('\n');
    return e == std::string::npos ? message : message.substr(0, e);
}

// --- the index (.git/index, versions 2-3; version 4's prefix compression isn't supported) ---

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool parseIndex(const std::string &data, std::vector<IndexEntry> &out, std::string *err)
{
    out.clear();
    const uint8_t *d = (const uint8_t *)data.data();
    size_t n = data.size();
    auto fail = [&](const char *m) { if (err) *err = m; return false; };
    if (n < 12 || memcmp(d, "DIRC", 4) != 0) return fail("not a git index");
    uint32_t ver = be32(d + 4), count = be32(d + 8);
    if (ver < 2 || ver > 3) return fail("unsupported index version");
    size_t p = 12;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 62 > n) return fail("index truncated");
        IndexEntry e;
        e.mtime = be32(d + p + 8);
        e.mode = be32(d + p + 24);
        e.size = be32(d + p + 36);
        e.id = toHex(d + p + 40, 20);
        uint16_t flags = be16(d + p + 60);
        e.stage = (flags >> 12) & 3;
        size_t hdr = 62;
        if (ver >= 3 && (flags & 0x4000)) hdr += 2;     // extended flags
        size_t nameLen = flags & 0xFFF;
        const char *name = (const char *)d + p + hdr;
        if (nameLen == 0xFFF) nameLen = strnlen(name, n - p - hdr);
        if (p + hdr + nameLen > n) return fail("index truncated");
        e.path.assign(name, nameLen);
        // entries are padded with 1-8 NULs to a multiple of 8 bytes
        size_t len = (hdr + nameLen + 8) & ~(size_t)7;
        p += len;
        out.push_back(e);
    }
    return true;
}

// --- packs ---

struct Repo::Pack {
    std::string packPath;
    std::vector<uint8_t> idx;     // the whole .idx (small)
    uint32_t count = 0;
    FILE *f = nullptr;
    // offset of the object with this id, or -1
    int64_t find(const uint8_t *sha) const
    {
        const uint8_t *fan = idx.data() + 8;
        uint32_t lo = sha[0] ? be32(fan + 4 * (sha[0] - 1)) : 0, hi = be32(fan + 4 * sha[0]);
        const uint8_t *names = idx.data() + 8 + 1024;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            int c = memcmp(names + 20 * mid, sha, 20);
            if (c == 0) {
                const uint8_t *offs = names + 20 * count + 4 * count;
                uint32_t o = be32(offs + 4 * mid);
                if (!(o & 0x80000000)) return o;
                const uint8_t *big = offs + 4 * count;
                uint32_t k = o & 0x7FFFFFFF;
                return (int64_t)((uint64_t)be32(big + 8 * k) << 32 | be32(big + 8 * k + 4));
            }
            if (c < 0) lo = mid + 1;
            else hi = mid;
        }
        return -1;
    }
};

Repo::Repo() {}

Repo::~Repo()
{
    for (auto *p : packs) {
        if (p->f) fclose(p->f);
        delete p;
    }
}

bool Repo::open(const std::string &workDir, ListDirFn ld, std::string *err)
{
    work = workDir;
    gitDir = pathJoin(workDir, ".git");
    listDir = ld;
    std::string head;
    if (!readWhole(pathJoin(gitDir, "HEAD"), head)) {
        if (err) *err = "no .git/HEAD in " + workDir;
        gitDir.clear();
        return false;
    }
    return true;
}

void Repo::loadPacks()
{
    if (packsLoaded) return;
    packsLoaded = true;
    std::string dir = pathJoin(gitDir, "objects/pack");
    std::vector<std::string> names;
    if (!listDir || !listDir(dir, names)) return;
    for (auto &nm : names) {
        if (nm.size() < 5 || nm.compare(nm.size() - 4, 4, ".idx") != 0) continue;
        std::string idxData;
        if (!readWhole(pathJoin(dir, nm), idxData)) continue;
        const uint8_t *d = (const uint8_t *)idxData.data();
        if (idxData.size() < 8 + 1024 || memcmp(d, "\377tOc", 4) != 0 || be32(d + 4) != 2) continue;
        Pack *p = new Pack;
        p->idx.assign(d, d + idxData.size());
        p->count = be32(d + 8 + 4 * 255);
        p->packPath = pathJoin(dir, nm.substr(0, nm.size() - 4) + ".pack");
        p->f = fopen(p->packPath.c_str(), "rb");
        if (!p->f) { delete p; continue; }
        packs.push_back(p);
    }
}

bool Repo::packObjectAt(Pack &p, uint64_t offset, int &type, std::string &data, int depth)
{
    if (depth > 64) return false;
    if (fseek(p.f, (long)offset, SEEK_SET) != 0) return false;
    int c = fgetc(p.f);
    if (c == EOF) return false;
    type = (c >> 4) & 7;
    uint64_t size = c & 15;
    int shift = 4;
    while (c & 0x80) {
        if ((c = fgetc(p.f)) == EOF) return false;
        size |= (uint64_t)(c & 0x7F) << shift;
        shift += 7;
    }
    if (type == 6) {            // OFS_DELTA: base at a negative offset
        if ((c = fgetc(p.f)) == EOF) return false;
        uint64_t rel = c & 0x7F;
        while (c & 0x80) {
            if ((c = fgetc(p.f)) == EOF) return false;
            rel = ((rel + 1) << 7) | (c & 0x7F);
        }
        std::string delta;
        if (!inflateFromFile(p.f, (size_t)size, delta)) return false;
        std::string base;
        if (!packObjectAt(p, offset - rel, type, base, depth + 1)) return false;
        return applyDelta(base, delta, data);
    }
    if (type == 7) {            // REF_DELTA: base by id
        uint8_t sha[20];
        if (fread(sha, 1, 20, p.f) != 20) return false;
        std::string delta;
        if (!inflateFromFile(p.f, (size_t)size, delta)) return false;
        std::string bt, base;
        if (!readObject(toHex(sha, 20), bt, base)) return false;
        type = bt == "commit" ? 1 : bt == "tree" ? 2 : bt == "blob" ? 3 : 4;
        return applyDelta(base, delta, data);
    }
    return inflateFromFile(p.f, (size_t)size, data);
}

bool Repo::readPacked(const std::string &id, std::string &type, std::string &data)
{
    loadPacks();
    uint8_t sha[20];
    if (!fromHex(id, sha)) return false;
    for (auto *p : packs) {
        int64_t off = p->find(sha);
        if (off < 0) continue;
        int t;
        if (!packObjectAt(*p, (uint64_t)off, t, data, 0)) return false;
        type = typeName(t);
        return true;
    }
    return false;
}

bool Repo::readLoose(const std::string &id, std::string &type, std::string &data)
{
    std::string raw;
    if (!readWhole(pathJoin(gitDir, "objects/" + id.substr(0, 2) + "/" + id.substr(2)), raw)) return false;
    std::string all;
    if (!inflateAll((const uint8_t *)raw.data(), raw.size(), all)) return false;
    size_t sp = all.find(' '), nul = all.find('\0');
    if (sp == std::string::npos || nul == std::string::npos || sp > nul) return false;
    type = all.substr(0, sp);
    data = all.substr(nul + 1);
    return true;
}

bool Repo::readObject(const std::string &id, std::string &type, std::string &data)
{
    if (!isOpen() || !isHexId(id)) return false;
    auto it = cache.find(id);
    if (it != cache.end()) {
        type = it->second.first;
        data = it->second.second;
        return true;
    }
    if (!readLoose(id, type, data) && !readPacked(id, type, data)) {
        lastError = "object " + id + " not found";
        return false;
    }
    if (type != "blob") {
        if (cacheBytes > 4 * 1024 * 1024) { cache.clear(); cacheBytes = 0; }
        cache[id] = { type, data };
        cacheBytes += data.size() + 64;
    }
    return true;
}

// --- refs ---

std::string Repo::readRefFile(const std::string &ref)
{
    std::string s;
    if (readWhole(pathJoin(gitDir, ref), s)) return trimEol(s);
    if (!packedRefsLoaded) {
        packedRefsLoaded = true;
        std::string pr;
        if (readWhole(pathJoin(gitDir, "packed-refs"), pr)) {
            size_t p = 0;
            while (p < pr.size()) {
                size_t e = pr.find('\n', p);
                std::string line = pr.substr(p, e == std::string::npos ? std::string::npos : e - p);
                p = e == std::string::npos ? pr.size() : e + 1;
                if (line.size() > 41 && line[0] != '#' && line[0] != '^' && line[40] == ' ')
                    packedRefs[trimEol(line.substr(41))] = line.substr(0, 40);
            }
        }
    }
    auto it = packedRefs.find(ref);
    return it == packedRefs.end() ? "" : it->second;
}

std::string Repo::resolve(const std::string &refIn)
{
    if (isHexId(refIn)) return refIn;
    std::string ref = refIn;
    if (ref != "HEAD" && ref.compare(0, 5, "refs/") != 0) {
        std::string h = resolve("refs/heads/" + ref);
        if (!h.empty()) return h;
        ref = "refs/tags/" + ref;
    }
    for (int i = 0; i < 8; i++) {
        std::string v = readRefFile(ref);
        if (v.compare(0, 5, "ref: ") == 0) { ref = trimEol(v.substr(5)); continue; }
        return isHexId(v) ? v : "";
    }
    return "";
}

std::string Repo::headBranch()
{
    std::string v = readRefFile("HEAD");
    const char *pfx = "ref: refs/heads/";
    if (v.compare(0, strlen(pfx), pfx) == 0) return v.substr(strlen(pfx));
    return "";
}

std::vector<std::string> Repo::branches()
{
    std::set<std::string> out;
    std::function<void(const std::string &, const std::string &)> walk = [&](const std::string &dir, const std::string &prefix) {
        std::vector<std::string> names;
        if (!listDir || !listDir(dir, names)) return;
        for (auto &n : names) {
            if (!n.empty() && n.back() == '/') walk(pathJoin(dir, n.substr(0, n.size() - 1)), prefix + n);
            else out.insert(prefix + n);
        }
    };
    walk(pathJoin(gitDir, "refs/heads"), "");
    readRefFile("refs/heads/");   // loads packed-refs
    for (auto &kv : packedRefs)
        if (kv.first.compare(0, 11, "refs/heads/") == 0) out.insert(kv.first.substr(11));
    std::string cur = headBranch();
    if (!cur.empty()) out.insert(cur);   // a new repository's branch before its first commit
    return std::vector<std::string>(out.begin(), out.end());
}

// --- objects ---

bool Repo::readCommit(const std::string &id, Commit &out)
{
    std::string type, data;
    if (!readObject(id, type, data) || type != "commit") return false;
    out = Commit();
    out.id = id;
    size_t p = 0;
    while (p < data.size()) {
        size_t e = data.find('\n', p);
        if (e == std::string::npos) e = data.size();
        std::string line = data.substr(p, e - p);
        p = e + 1;
        if (line.empty()) {
            out.message = data.substr(std::min(p, data.size()));
            while (!out.message.empty() && out.message.back() == '\n') out.message.pop_back();
            break;
        }
        if (line.compare(0, 5, "tree ") == 0) out.tree = line.substr(5);
        else if (line.compare(0, 7, "parent ") == 0) out.parents.push_back(line.substr(7));
        else if (line.compare(0, 7, "author ") == 0) {
            // author Name <email> 1700000000 +0100
            size_t lt = line.find('<'), gt = line.find('>');
            if (lt != std::string::npos && gt != std::string::npos) {
                out.author = trimEol(line.substr(7, lt - 7));
                out.email = line.substr(lt + 1, gt - lt - 1);
                const char *t = line.c_str() + gt + 1;
                out.time = strtoll(t, (char **)&t, 10);
                while (*t == ' ') t++;
                out.tz = t;
            }
        }
    }
    return true;
}

bool Repo::readTree(const std::string &id, std::vector<TreeEntry> &out)
{
    std::string type, data;
    out.clear();
    if (!readObject(id, type, data) || type != "tree") return false;
    size_t p = 0;
    while (p < data.size()) {
        size_t sp = data.find(' ', p), nul = data.find('\0', p);
        if (sp == std::string::npos || nul == std::string::npos || nul + 21 > data.size()) return false;
        TreeEntry e;
        e.mode = data.substr(p, sp - p);
        e.name = data.substr(sp + 1, nul - sp - 1);
        e.id = toHex((const uint8_t *)data.data() + nul + 1, 20);
        out.push_back(e);
        p = nul + 21;
    }
    return true;
}

static std::string treeOf(Repo &r, const std::string &id)
{
    std::string type, data;
    if (!r.readObject(id, type, data)) return "";
    if (type == "tree") return id;
    Commit c;
    return r.readCommit(id, c) ? c.tree : "";
}

bool Repo::treeFiles(const std::string &treeOrCommit, std::map<std::string, std::string> &out)
{
    out.clear();
    std::string root = treeOf(*this, treeOrCommit);
    if (root.empty()) return false;
    std::function<bool(const std::string &, const std::string &)> walk = [&](const std::string &tree, const std::string &prefix) {
        std::vector<TreeEntry> es;
        if (!readTree(tree, es)) return false;
        for (auto &e : es) {
            if (e.isTree()) { if (!walk(e.id, prefix + e.name + "/")) return false; }
            else if (e.mode != "160000") out[prefix + e.name] = e.id;   // not submodules
        }
        return true;
    };
    return walk(root, "");
}

std::string Repo::blobAt(const std::string &commit, const std::string &path)
{
    std::string tree = treeOf(*this, commit);
    size_t p = 0;
    while (!tree.empty()) {
        size_t slash = path.find('/', p);
        std::string part = path.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
        std::vector<TreeEntry> es;
        if (!readTree(tree, es)) return "";
        std::string next;
        bool isTree = false;
        for (auto &e : es)
            if (e.name == part) { next = e.id; isTree = e.isTree(); break; }
        if (next.empty()) return "";
        if (slash == std::string::npos) return isTree ? "" : next;
        if (!isTree) return "";
        tree = next;
        p = slash + 1;
    }
    return "";
}

bool Repo::readBlob(const std::string &id, std::string &out)
{
    std::string type;
    return readObject(id, type, out) && type == "blob";
}

bool Repo::readIndex(std::vector<IndexEntry> &out)
{
    std::string data;
    out.clear();
    if (!readWhole(pathJoin(gitDir, "index"), data)) return true;   // no index yet: empty
    return parseIndex(data, out, &lastError);
}

std::vector<Repo::FileVersion> Repo::fileHistory(const std::string &start, const std::string &path, int limit)
{
    std::vector<FileVersion> out;
    std::string id = resolve(start);
    std::string prevBlob;
    Commit prev;
    bool havePrev = false;
    int walked = 0;
    // Walk first parents. A commit "changed" the file when its blob differs from its parent's.
    while (!id.empty() && (int)out.size() < limit && walked < 5000) {
        Commit c;
        if (!readCommit(id, c)) break;
        walked++;
        std::string blob = blobAt(id, path);
        if (havePrev && blob != prevBlob && !prevBlob.empty()) out.push_back({ prev, prevBlob });
        if (blob.empty()) { havePrev = false; break; }   // the file didn't exist before this
        prev = c;
        prevBlob = blob;
        havePrev = true;
        id = c.parents.empty() ? "" : c.parents[0];
    }
    if (havePrev && (int)out.size() < limit && !prevBlob.empty() && id.empty()) out.push_back({ prev, prevBlob });
    return out;
}

// --- status ---

std::vector<FileStatus> computeStatus(const std::map<std::string, std::string> &head,
                                      const std::vector<IndexEntry> &index,
                                      const std::map<std::string, std::string> &workIds)
{
    // like `git status --porcelain`: a file deleted from the index but still on disk is
    // listed twice, as "D " and as "??"
    std::vector<FileStatus> out;
    std::map<std::string, std::string> idx;
    for (auto &e : index) if (e.stage == 0 || !idx.count(e.path)) idx[e.path] = e.id;
    std::set<std::string> paths;
    for (auto &kv : idx) paths.insert(kv.first);
    for (auto &kv : head) paths.insert(kv.first);
    for (auto &kv : workIds) paths.insert(kv.first);
    for (auto &p : paths) {
        auto i = idx.find(p);
        auto h = head.find(p);
        auto w = workIds.find(p);
        if (i != idx.end()) {
            char x = h == head.end() ? 'A' : h->second != i->second ? 'M' : ' ';
            char y = w == workIds.end() ? 'D' : w->second != i->second ? 'M' : ' ';
            if (x != ' ' || y != ' ') out.push_back({ p, x, y });
            continue;
        }
        if (h != head.end()) out.push_back({ p, 'D', ' ' });
        if (w != workIds.end()) out.push_back({ p, '?', '?' });
    }
    return out;
}

} // namespace git
} // namespace af
