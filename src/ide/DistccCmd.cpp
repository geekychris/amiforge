// distcc command lines (see DistccCmd.h).
#include "DistccCmd.h"

#include <ctype.h>
#include <stdlib.h>
#include <algorithm>

namespace af {
namespace distcc {

std::vector<std::string> parseHosts(const std::string &s)
{
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (isspace((unsigned char)c) || c == ',' || c == ';') {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

bool validHost(const std::string &h, std::string &err)
{
    if (h.empty()) { err = "empty host"; return false; }
    if (h == "localhost") return true;
    size_t colon = h.find(':'), slash = h.find('/');
    std::string name = h.substr(0, std::min(colon, slash));
    if (name.empty()) { err = "no host name in " + h; return false; }
    for (char c : name)
        if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_') { err = "bad host name: " + name; return false; }
    if (colon != std::string::npos) {
        std::string port = h.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1);
        char *e;
        long p = strtol(port.c_str(), &e, 10);
        if (port.empty() || *e || p <= 0 || p > 65535) { err = "bad port in " + h; return false; }
    }
    if (slash != std::string::npos) {
        std::string lim = h.substr(slash + 1);
        char *e;
        long l = strtol(lim.c_str(), &e, 10);
        if (lim.empty() || *e || l <= 0 || l > 64) { err = "bad job limit in " + h; return false; }
    }
    return true;
}

std::string hostsValue(const std::vector<std::string> &hosts)
{
    std::string s;
    for (auto &h : hosts) s += (s.empty() ? "" : " ") + h;
    return s;
}

std::string wrapCompile(const std::string &distccPath, const std::string &cmd)
{
    size_t sp = cmd.find(' ');
    std::string compiler = cmd.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : cmd.substr(sp);
    size_t slash = compiler.find_last_of("/:");
    std::string plain = slash == std::string::npos ? compiler : compiler.substr(slash + 1);
    // "ppc-amigaos-g++" also works on the server, but plain names are what's on the Amiga's path
    return distccPath + " " + plain + rest;
}

bool distributable(const std::string &file)
{
    size_t dot = file.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = file.substr(dot + 1);
    for (auto &c : e) c = (char)tolower((unsigned char)c);
    return e == "c" || e == "cpp" || e == "cc" || e == "cxx" || e == "c++";
}

} // namespace distcc
} // namespace af
