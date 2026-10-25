// Project templates: template.def parsing and substitution (see TemplateEngine.h).
#include "TemplateEngine.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace af {
namespace tmpl {

static std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

static std::string ident(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) o += (isalnum(c) && c < 128) || c == '_' ? (char)c : '_';
    if (o.empty() || isdigit((unsigned char)o[0])) o = "_" + o;
    return o;
}

static std::string mapCase(std::string s, bool up)
{
    for (auto &c : s) c = (char)(up ? toupper((unsigned char)c) : tolower((unsigned char)c));
    return s;
}

std::string substitute(const std::string &text, const Vars &vars, std::vector<std::string> *unknown)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 3, "$${") == 0) { out += "${"; i += 3; continue; }
        if (text.compare(i, 2, "${") != 0) { out += text[i++]; continue; }
        size_t e = text.find('}', i + 2);
        if (e == std::string::npos) { out += text.substr(i); break; }
        std::string spec = text.substr(i + 2, e - i - 2);
        std::string name = spec, filter;
        size_t colon = spec.find(':');
        if (colon != std::string::npos) { name = spec.substr(0, colon); filter = spec.substr(colon + 1); }
        auto it = vars.find(name);
        bool ok = it != vars.end() && (filter.empty() || filter == "upper" || filter == "lower" || filter == "ident");
        if (!ok) {
            if (unknown) unknown->push_back(spec);
            out += text.substr(i, e + 1 - i);
        } else {
            std::string v = it->second;
            if (filter == "upper") v = mapCase(v, true);
            else if (filter == "lower") v = mapCase(v, false);
            else if (filter == "ident") v = ident(v);
            out += v;
        }
        i = e + 1;
    }
    return out;
}

Vars makeVars(const std::string &name, const std::string &author, int day, int month, int year)
{
    Vars v;
    v["NAME"] = name;
    v["IDENT"] = ident(name);
    v["UPPER"] = mapCase(ident(name), true);
    v["LOWER"] = mapCase(ident(name), false);
    v["AUTHOR"] = author.empty() ? "Anonymous" : author;
    char buf[32];
    snprintf(buf, sizeof buf, "%d.%d.%d", day, month, year);
    v["DATE"] = buf;
    v["YEAR"] = std::to_string(year);
    return v;
}

bool validName(const std::string &name, std::string &why)
{
    if (trim(name).empty()) { why = "Please enter a name."; return false; }
    if (name.size() > 24) { why = "Please use a name of at most 24 characters."; return false; }
    if (!isalpha((unsigned char)name[0])) { why = "The name must start with a letter."; return false; }
    for (unsigned char c : name)
        if (!(isalnum(c) && c < 128) && c != '_' && c != '-') {
            why = "Please use only letters, digits, - and _ in the name: it becomes file and C names.";
            return false;
        }
    return true;
}

bool parseDef(const std::string &text, Def &out, std::string &err)
{
    out = Def();
    std::string section;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        lineNo++;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            section = line.substr(1, e == std::string::npos ? std::string::npos : e - 1);
            if (section != "template" && section != "project" && section != "files" && section != "create") {
                err = "line " + std::to_string(lineNo) + ": unknown section [" + section + "]";
                return false;
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) {
            err = "line " + std::to_string(lineNo) + ": expected key=value in a [section]";
            return false;
        }
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (section == "template") {
            if (k == "name") out.name = v;
            else if (k == "description") out.description = v;
            else if (k == "order") out.order = atoi(v.c_str());
        } else if (section == "project") {
            out.project.push_back({ k, v });
        } else if (section == "files") {
            if (k == "open") { out.open.push_back(v); continue; }
            FileSpec f;
            if (k == "file") f.kind = FileSpec::Project;
            else if (k == "extra") f.kind = FileSpec::Extra;
            else if (k == "generated") f.kind = FileSpec::Generated;
            else { err = "line " + std::to_string(lineNo) + ": unknown key \"" + k + "\" in [files]"; return false; }
            size_t arrow = v.find("->");
            if (arrow == std::string::npos) f.src = f.dest = v;
            else { f.src = trim(v.substr(0, arrow)); f.dest = trim(v.substr(arrow + 2)); }
            if (f.dest.empty() || f.dest[0] == '/' || f.dest.find(':') != std::string::npos) {
                err = "line " + std::to_string(lineNo) + ": the destination must be a relative path";
                return false;
            }
            out.files.push_back(f);
        } else if (section == "create") {
            if (k == "command") out.commands.push_back(v);
            else if (k == "gui") out.guis.push_back(v);
            else { err = "line " + std::to_string(lineNo) + ": unknown key \"" + k + "\" in [create]"; return false; }
        }
    }
    if (out.name.empty()) { err = "the template has no name"; return false; }
    return true;
}

Def instantiate(const Def &d, const Vars &v)
{
    Def o = d;
    for (auto &p : o.project) p.second = substitute(p.second, v);
    for (auto &f : o.files) f.dest = substitute(f.dest, v);
    for (auto &s : o.open) s = substitute(s, v);
    for (auto &s : o.commands) s = substitute(s, v);
    for (auto &s : o.guis) s = substitute(s, v);
    return o;
}

Plan plan(const Def &d, const Vars &vars, const std::function<bool(const std::string &, std::string &)> &read)
{
    Plan p;
    p.project = d.project;
    p.commands = d.commands;
    p.guis = d.guis;
    p.open = d.open;
    auto addDirs = [&](const std::string &dest) {
        for (auto &dir : parentDirs(dest)) {
            bool have = false;
            for (auto &x : p.dirs) have |= x == dir;
            if (!have) p.dirs.push_back(dir);
        }
    };
    for (auto &f : d.files) {
        addDirs(f.dest);
        if (f.kind != FileSpec::Generated) {
            std::string text;
            if (!read(f.src, text)) {
                p.errors.push_back("The template's file " + f.src + " is missing.");
                continue;
            }
            std::vector<std::string> unknown;
            Plan::Output o{ f.dest, substitute(text, vars, &unknown) };
            for (auto &u : unknown) p.errors.push_back(f.src + ": unknown substitution ${" + u + "}");
            p.files.push_back(o);
        }
        if (f.kind != FileSpec::Extra) p.projectFiles.push_back(f.dest);
    }
    return p;
}

std::vector<std::string> parentDirs(const std::string &dest)
{
    std::vector<std::string> out;
    size_t p = 0;
    while ((p = dest.find('/', p)) != std::string::npos) {
        out.push_back(dest.substr(0, p));
        p++;
    }
    return out;
}

} // namespace tmpl
} // namespace af
