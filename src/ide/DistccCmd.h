// Distributed builds with distcc (SDK:C/distcc): the compile commands run as
// "distcc g++ ..." with DISTCC_HOSTS naming machines that run distccd with a PPC AmigaOS
// cross-compiler (tools/distcc-server.sh starts one in Docker). Preprocessing and linking
// stay on the Amiga; the compiling - the slow part - happens on the other machine.
// Portable (tested on the host).
#pragma once

#include <string>
#include <vector>

namespace af {
namespace distcc {

// "192.168.101.2, 10.0.2.2:3633/4 localhost" -> {"192.168.101.2", "10.0.2.2:3633/4", "localhost"}
std::vector<std::string> parseHosts(const std::string &s);
// host[:port][/limit]; false (with a reason) if it isn't one
bool validHost(const std::string &h, std::string &err);
std::string hostsValue(const std::vector<std::string> &hosts);   // for DISTCC_HOSTS
// Turn a compile command ("SDK:gcc/bin/g++ -O0 -c a.cpp -o obj/a.o") into a distcc one
// ("SDK:C/distcc g++ -O0 -c a.cpp -o obj/a.o"). The compiler is passed by its plain name
// (gcc, g++): that's the name the server maps to its cross-compiler.
std::string wrapCompile(const std::string &distccPath, const std::string &compileCmd);
// Whether a source file is worth sending (C and C++; not assembler)
bool distributable(const std::string &file);

} // namespace distcc
} // namespace af
