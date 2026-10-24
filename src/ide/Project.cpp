// Project files (*.afp): ini-style, paths relative to the project's directory.
//
//   [project]
//   name=hello
//   output=hello
//   cflags=-O0 -gstabs -Wall
//   ...
//   [files]
//   file=main.c
//   [breakpoints]
//   bp=main.c:12:1:cond
//   [watches]
//   watch=p->next
#include "App.h"

#include <stdlib.h>
#include <strings.h>

namespace af {

bool Project::load(const std::string &afp, std::string &err)
{
    std::string text;
    if (!readFile(afp, text)) { err = "Can't read " + afp; return false; }
    *this = Project();
    path = absolutePath(afp);
    dir = dirName(path);
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (section == "project") {
            if (k == "name") name = v;
            else if (k == "output") output = v;
            else if (k == "cc") cc = v;
            else if (k == "cxx") cxx = v;
            else if (k == "cflags") cflags = v;
            else if (k == "cxxflags") cxxflags = v;
            else if (k == "asflags") asflags = v;
            else if (k == "ldflags") ldflags = v;
            else if (k == "libs") libs = v;
            else if (k == "objdir") objdir = v;
            else if (k == "args") args = v;
            else if (k == "console") console = v;
            else if (k == "stopatmain") stopAtMain = v != "0" && strcasecmp(v.c_str(), "no") != 0;
        } else if (section == "files" && k == "file") {
            files.push_back(v);
        } else if (section == "watches" && k == "watch") {
            watches.push_back(v);
        } else if (section == "breakpoints" && k == "bp") {
            // file:line:enabled[:condition] — the file itself may not contain ':' (it's relative)
            Bp b;
            size_t c1 = v.find(':');
            if (c1 == std::string::npos) continue;
            size_t c2 = v.find(':', c1 + 1);
            b.file = v.substr(0, c1);
            b.line = atoi(v.c_str() + c1 + 1);
            b.enabled = true;
            if (c2 != std::string::npos) {
                b.enabled = v[c2 + 1] != '0';
                size_t c3 = v.find(':', c2 + 1);
                if (c3 != std::string::npos) b.cond = v.substr(c3 + 1);
            }
            if (b.line > 0) breakpoints.push_back(b);
        }
    }
    return true;
}

bool Project::save(std::string &err)
{
    std::string s = "# AmiForge project\n[project]\n";
    s += "name=" + name + "\n";
    s += "output=" + output + "\n";
    s += "cc=" + cc + "\n";
    s += "cxx=" + cxx + "\n";
    s += "cflags=" + cflags + "\n";
    s += "cxxflags=" + cxxflags + "\n";
    s += "asflags=" + asflags + "\n";
    s += "ldflags=" + ldflags + "\n";
    s += "libs=" + libs + "\n";
    s += "objdir=" + objdir + "\n";
    s += "args=" + args + "\n";
    s += "console=" + console + "\n";
    s += std::string("stopatmain=") + (stopAtMain ? "1" : "0") + "\n";
    s += "\n[files]\n";
    for (auto &f : files) s += "file=" + f + "\n";
    if (!breakpoints.empty()) {
        s += "\n[breakpoints]\n";
        for (auto &b : breakpoints)
            s += strprintf("bp=%s:%d:%d", b.file.c_str(), b.line, b.enabled ? 1 : 0) +
                 (b.cond.empty() ? "" : ":" + b.cond) + "\n";
    }
    if (!watches.empty()) {
        s += "\n[watches]\n";
        for (auto &w : watches) s += "watch=" + w + "\n";
    }
    if (!writeFile(path, s)) { err = "Can't write " + path; return false; }
    return true;
}

bool Project::usesCpp() const
{
    for (auto &f : files)
        if (isCppFile(f)) return true;
    return false;
}

bool Project::contains(const std::string &absPath) const
{
    for (auto &f : files)
        if (sameFile(abs(f), absPath)) return true;
    return false;
}

} // namespace af
