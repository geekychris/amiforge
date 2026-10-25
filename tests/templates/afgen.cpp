// afgen - the GUI designer's code generator and the template engine, on the host, for
// tests/templates/check.sh:
//   afgen gen FILE.afg OUTDIR                       write <name>_gui.c/.h (keeping user code)
//   afgen new TEMPLATES ID NAME OUTDIR              create a project from a template: its files,
//                                                   GUI code, and project.txt/files.txt/commands.txt
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "GuiModel.h"
#include "TemplateEngine.h"

using namespace af;

static bool readFile(const std::string &p, std::string &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeFile(const std::string &p, const std::string &s)
{
    std::ofstream f(p, std::ios::binary);
    f << s;
    return (bool)f;
}

static std::string dirOf(const std::string &p)
{
    size_t s = p.rfind('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}

static int gen(const std::string &afg, const std::string &outdir, std::string *cName = nullptr, std::string *hName = nullptr)
{
    std::string text, err;
    if (!readFile(afg, text)) { fprintf(stderr, "can't read %s\n", afg.c_str()); return 1; }
    gui::Model m;
    if (!m.parse(text, err)) { fprintf(stderr, "%s: %s\n", afg.c_str(), err.c_str()); return 1; }
    for (auto &p : m.validate()) { fprintf(stderr, "%s: %s\n", afg.c_str(), p.c_str()); return 1; }
    std::string old;
    gui::Generated g = gui::generate(m, "");
    readFile(outdir + "/" + g.sourceName, old);
    g = gui::generate(m, old);
    if (!writeFile(outdir + "/" + g.headerName, g.header) || !writeFile(outdir + "/" + g.sourceName, g.source)) return 1;
    if (cName) *cName = g.sourceName;
    if (hName) *hName = g.headerName;
    return 0;
}

int main(int argc, char **argv)
{
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "gen" && argc == 4) return gen(argv[2], argv[3]);
    if (cmd != "new" || argc != 6) {
        fprintf(stderr, "usage: afgen gen FILE.afg OUTDIR | afgen new TEMPLATES ID NAME OUTDIR\n");
        return 2;
    }
    std::string tdir = std::string(argv[2]) + "/" + argv[3], name = argv[4], out = argv[5];
    std::string text, err;
    tmpl::Def d;
    if (!readFile(tdir + "/template.def", text) || !tmpl::parseDef(text, d, err)) {
        fprintf(stderr, "%s: %s\n", tdir.c_str(), err.c_str());
        return 1;
    }
    tmpl::Vars v = tmpl::makeVars(name, "Test Author <test@example.org>", 25, 10, 2026);
    tmpl::Plan p = tmpl::plan(tmpl::instantiate(d, v), v, [&](const std::string &src, std::string &o) {
        return readFile(tdir + "/" + src, o);
    });
    for (auto &e : p.errors) { fprintf(stderr, "%s: %s\n", argv[3], e.c_str()); }
    if (!p.errors.empty()) return 1;
    mkdir(out.c_str(), 0755);
    for (auto &dd : p.dirs) mkdir((out + "/" + dd).c_str(), 0755);
    for (auto &f : p.files)
        if (!writeFile(out + "/" + f.path, f.content)) return 1;
    std::string files;
    for (auto &f : p.projectFiles) files += f + "\n";
    for (auto &g : p.guis) {
        std::string c, h;
        if (gen(out + "/" + g, dirOf(out + "/" + g), &c, &h)) return 1;
        files += c + "\n" + h + "\n";
    }
    std::string proj;
    for (auto &kv : p.project) proj += kv.first + "=" + kv.second + "\n";
    std::string cmds;
    for (auto &c : p.commands) cmds += c + "\n";
    writeFile(out + "/project.txt", proj);
    writeFile(out + "/files.txt", files);
    writeFile(out + "/commands.txt", cmds);
    printf("%s: %zu files\n", argv[3], p.files.size());
    return 0;
}
