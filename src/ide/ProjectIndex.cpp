// The project browser's model (see ProjectIndex.h). No OS calls: compiles on the host too.
#include "ProjectIndex.h"

#include <algorithm>
#include <cctype>

namespace af {
namespace pidx {

static std::string lower(std::string s)
{
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string trimmed(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string join(const std::string &dir, const std::string &name)
{
    if (dir.empty()) return name;
    char last = dir.back();
    return last == ':' || last == '/' ? dir + name : dir + "/" + name;
}

std::string parent(const std::string &path)
{
    std::string p = path;
    if (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.rfind('/'), colon = p.rfind(':');
    if (slash != std::string::npos && (colon == std::string::npos || slash > colon)) return p.substr(0, slash);
    if (colon != std::string::npos) return p.substr(0, colon + 1);
    return "";
}

std::string leaf(const std::string &path)
{
    size_t at = path.find_last_of(":/");
    return at == std::string::npos ? path : path.substr(at + 1);
}

bool isInside(const std::string &path, const std::string &folder)
{
    if (folder.empty() || path.empty()) return false;
    std::string p = lower(path), f = lower(folder);
    if (f.size() > 1 && f.back() == '/') f.pop_back();
    if (p.size() > 1 && p.back() == '/') p.pop_back();
    if (p == f) return true;
    if (p.compare(0, f.size(), f) != 0) return false;
    if (f.back() == ':') return true;            // the volume itself
    return p.size() > f.size() && p[f.size()] == '/';
}

// The value of key in [project], raw.
static bool projectValue(const std::string &text, const char *key, std::string &out)
{
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trimmed(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        size_t eq = line.find('=');
        if (section != "project" || eq == std::string::npos) continue;
        if (trimmed(line.substr(0, eq)) == key) {
            out = trimmed(line.substr(eq + 1));
            return true;
        }
    }
    return false;
}

std::string afpDescription(const std::string &afpText)
{
    std::string v;
    if (!projectValue(afpText, "description", v)) return "";
    // unescape (\n, \\) up to the first line break that has text before it
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char c = v[++i];
            if (c == 'n') {
                if (!trimmed(o).empty()) break;
                o.clear();
                continue;
            }
            o += c;
        } else o += v[i];
    }
    return trimmed(o);
}

std::string afpName(const std::string &afpText)
{
    std::string v;
    projectValue(afpText, "name", v);
    return v;
}

std::string readmeLine(const std::string &text, size_t max)
{
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trimmed(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty()) continue;
        // rules and setext underlines: ---- ==== ****
        if (line.find_first_not_of("-=*_~ ") == std::string::npos) continue;
        while (!line.empty() && line[0] == '#') line.erase(0, 1);
        line = trimmed(line);
        if (line.empty()) continue;
        if (line.size() > max) line = trimmed(line.substr(0, max > 3 ? max - 3 : 0)) + "...";
        return line;
    }
    return "";
}

bool isReadme(const std::string &name)
{
    std::string n = lower(name);
    return n == "readme" || n == "readme.txt" || n == "readme.md" || n == "readme.1st";
}

Project describe(const std::string &afp, const std::vector<DirItem> &items, const ReadFile &read)
{
    Project p;
    p.afp = afp;
    std::string text;
    if (read(afp, text)) {
        p.name = afpName(text);
        p.description = afpDescription(text);
    }
    if (p.name.empty()) {
        p.name = leaf(afp);
        if (lower(p.name).size() > 4 && lower(p.name).compare(p.name.size() - 4, 4, ".afp") == 0)
            p.name.resize(p.name.size() - 4);
    }
    if (p.description.empty()) {
        // the README with the shortest name first: README before README.txt
        std::vector<std::string> readmes;
        for (auto &i : items)
            if (!i.dir && isReadme(i.name)) readmes.push_back(i.name);
        std::sort(readmes.begin(), readmes.end(), [](const std::string &a, const std::string &b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
        for (auto &r : readmes) {
            std::string t;
            if (read(join(parent(afp), r), t) && !(p.description = readmeLine(t)).empty()) break;
        }
    }
    if (p.description.size() > 200) p.description.resize(200);
    return p;
}

static bool isAfp(const DirItem &i)
{
    std::string n = lower(i.name);
    return !i.dir && n.size() > 4 && n.compare(n.size() - 4, 4, ".afp") == 0;
}

static void scanDrawer(const std::string &root, const std::string &rel, int depthLeft, const ListDir &list,
                       const ReadFile &read, std::vector<Project> &out)
{
    std::string path = rel.empty() ? root : join(root, rel);
    std::vector<DirItem> items;
    if (!list(path, items)) return;
    bool isProject = false;
    for (auto &i : items)
        if (isAfp(i)) {
            Project p = describe(join(path, i.name), items, read);
            p.drawer = rel;
            out.push_back(p);
            isProject = true;
        }
    if (isProject || depthLeft <= 0) return;
    for (auto &i : items) {
        if (!i.dir) continue;
        std::string n = lower(i.name);
        if (n == "obj" || n == "obj-release" || n == ".git") continue;
        scanDrawer(root, rel.empty() ? i.name : rel + "/" + i.name, depthLeft - 1, list, read, out);
    }
}

static void sortProjects(std::vector<Project> &v)
{
    std::stable_sort(v.begin(), v.end(), [](const Project &a, const Project &b) {
        std::string x = lower(a.name), y = lower(b.name);
        if (x != y) return x < y;
        return lower(a.drawer) < lower(b.drawer);
    });
}

std::vector<Project> scan(const std::string &root, int depth, const ListDir &list, const ReadFile &read)
{
    std::vector<Project> out;
    if (root.empty()) return out;
    // the root's own .afp files (root is a project drawer), then its sub-drawers
    std::vector<DirItem> items;
    if (!list(root, items)) return out;
    for (auto &i : items)
        if (isAfp(i)) {
            Project p = describe(join(root, i.name), items, read);
            out.push_back(p);
        }
    for (auto &i : items) {
        if (!i.dir || depth <= 0) continue;
        std::string n = lower(i.name);
        if (n == "obj" || n == "obj-release" || n == ".git") continue;
        scanDrawer(root, i.name, depth - 1, list, read, out);
    }
    sortProjects(out);
    return out;
}

std::vector<Project> recent(const std::vector<std::string> &afps, const ListDir &list, const ReadFile &read)
{
    std::vector<Project> out;
    for (auto &a : afps) {
        std::string probe;
        if (!read(a, probe)) continue;   // gone
        std::vector<DirItem> items;
        list(parent(a), items);
        Project p = describe(a, items, read);
        p.drawer = parent(a);
        out.push_back(p);
    }
    return out;
}

std::vector<Row> rows(const std::vector<Project> &recentOnes, const std::vector<Project> &found,
                      const std::string &folder)
{
    std::vector<Row> out;
    auto heading = [&](const std::string &t) {
        Row r;
        r.heading = true;
        r.cols[0] = t;
        out.push_back(r);
    };
    auto add = [&](const Project &p) {
        Row r;
        r.cols[0] = p.name;
        r.cols[1] = p.description;
        r.cols[2] = p.drawer.empty() ? "." : p.drawer;
        r.afp = p.afp;
        out.push_back(r);
    };
    if (!recentOnes.empty()) {
        heading("Recent");
        for (auto &p : recentOnes) add(p);
    }
    // the folder goes in the wide description column
    heading("Projects");
    out.back().cols[1] = "in " + folder;
    if (found.empty()) {
        Row r;
        r.heading = true;
        r.cols[1] = "(no projects here: Change Folder... picks another drawer)";
        out.push_back(r);
    }
    for (auto &p : found) add(p);
    return out;
}

std::string openStart(const std::string &projectsFolder, const std::string &currentProjectDir,
                      const std::string &lastDir)
{
    if (!currentProjectDir.empty() && (projectsFolder.empty() || !isInside(currentProjectDir, projectsFolder))) {
        std::string up = parent(currentProjectDir);
        return up.empty() ? currentProjectDir : up;
    }
    if (!projectsFolder.empty()) return projectsFolder;
    return lastDir;
}

bool parseOnOff(const std::string &text, bool dflt)
{
    std::string t = lower(trimmed(text));
    if (t.empty()) return dflt;
    return !(t == "0" || t == "no" || t == "off" || t == "false");
}

} // namespace pidx
} // namespace af
