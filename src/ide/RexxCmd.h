// ARexx command parsing for AmiForge's "AMIFORGE" port. Portable (tested on the host); the
// port itself (arexx.class) is in RexxPort.cpp.
//
// Templates are a small subset of AmigaDOS ReadArgs templates: comma-separated arguments,
// each NAME with /A (required), /N (a number), /F (the rest of the line), /S (a switch).
// Arguments can be given by position or as NAME=value / NAME value; "quotes" group words.
#pragma once

#include <string>
#include <vector>
#include <map>

namespace af {
namespace rexx {

struct CommandDef {
    const char *name;
    const char *tmpl;       // "LINE/N/A,COL/N"
    const char *help;       // one line
};
const std::vector<CommandDef> &commands();   // everything the port understands
const CommandDef *findCommand(const std::string &name);   // case-insensitive

struct Args {
    std::map<std::string, std::string> values;   // upper-case NAME -> value (switches: "1")
    bool has(const std::string &n) const { return values.count(n) > 0; }
    std::string str(const std::string &n, const std::string &def = "") const;
    long num(const std::string &n, long def = 0) const;
};

std::vector<std::string> tokenize(const std::string &s);    // words, "quoted strings" (quotes removed)
// Parse `args` against `tmpl`. On error returns false with a message.
bool parseArgs(const std::string &tmpl, const std::string &args, Args &out, std::string &err);

// Split a whole command line ("GOTO 10 3") into its command word and the rest.
void splitCommand(const std::string &line, std::string &cmd, std::string &rest);

// The text HELP returns: every command with its template.
std::string helpText();

} // namespace rexx
} // namespace af
