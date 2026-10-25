// ARexx command parsing (see RexxCmd.h).
#include "RexxCmd.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

namespace af {
namespace rexx {

const std::vector<CommandDef> &commands()
{
    static const std::vector<CommandDef> cmds = {
        { "OPEN", "FILE/A/F", "Open a file in the editor" },
        { "SAVE", "", "Save the current file" },
        { "SAVEAS", "FILE/A/F", "Save the current file under a new name" },
        { "CLOSE", "FORCE/S", "Close the current file (FORCE: without asking about changes)" },
        { "GOTO", "LINE/N/A,COL/N", "Move the cursor to a line (and column)" },
        { "GETLINE", "", "RESULT = the cursor's line number" },
        { "GETCOL", "", "RESULT = the cursor's column" },
        { "GETFILE", "", "RESULT = the current file's path" },
        { "GETTEXT", "ALL/S", "RESULT = the selection (\"\" if none); with ALL the whole file" },
        { "GETLINETEXT", "LINE/N", "RESULT = the text of a line (default: the cursor's)" },
        { "GETWORD", "", "RESULT = the word at the cursor" },
        { "INSERT", "TEXT/A/F", "Insert text at the cursor (\\n for a new line)" },
        { "REPLACE", "TEXT/F", "Replace the selection with text" },
        { "SELECT", "FROMLINE/N/A,TOLINE/N/A", "Select whole lines" },
        { "FIND", "TEXT/A/F", "Find text from the cursor; RC 5 if not found" },
        { "BUILD", "", "Build the project" },
        { "RUN", "", "Build and run the project" },
        { "DEBUG", "", "Start debugging (or continue)" },
        { "STOP", "", "Stop debugging" },
        { "PROJECT", "FILE/A/F", "Open a project (.afp)" },
        { "GETPROJECT", "", "RESULT = the project file" },
        { "STATUS", "TEXT/F", "Show text in the status bar" },
        { "REQUEST", "TEXT/A/F", "Show a message; RESULT = 1 for OK" },
        { "QUIT", "FORCE/S", "Quit AmiForge" },
        { "HELP", "COMMAND", "RESULT = the list of commands, or one command's template" },
        // --- mcp --- machine-readable commands for assistants (McpPort.cpp, McpCmd.h).
        // Text values may use the MCP escapes (\s space, \n newline...; McpCmd.h).
        { "MCP", "COMMAND/A/F", "Run a command; RESULT = \"AF1 rc more length payload\" (always RC 0)" },
        { "MCPMORE", "", "The next part of the last MCP result" },
        { "MCPBUF", "SLOT/N/A,CLEAR/S,TEXT", "Append text to slot 1-9 (refer to it as \\@1); CLEAR empties it first" },
        { "GETSTATE", "", "RESULT = key=value lines: project, file, build and debugger state" },
        { "DOCUMENTS", "", "RESULT = the open files: path, modified, current, lines" },
        { "PROJECTINFO", "", "RESULT = key=value lines of the project's settings" },
        { "PROJECTFILES", "", "RESULT = the project's files: relative path, full path" },
        { "PROJECTS", "DEPTH/N", "RESULT = the projects in the projects folder: name, .afp, description" },
        { "TEMPLATES", "", "RESULT = the project templates: id, name, description" },
        { "OPENPROJECT", "FILE/A", "Open a project (.afp); fails (and says why) if it didn't open" },
        { "ACTIVATE", "FILE/A,LINE/N", "Open a file (relative: in the project's drawer) and go to a line" },
        { "NEWPROJECT", "TEMPLATE/A,NAME/A,DRAWER/A", "Make a project from a template and open it; RESULT = the .afp" },
        { "READ", "FILE,FROMLINE/N,TOLINE/N", "RESULT = \"lines<tab>from<tab>to<tab>path\" and the text (the open buffer if open)" },
        { "EDITLINES", "FILE,FROMLINE/N/A,TOLINE/N/A,TEXT", "Replace lines (TOLINE = FROMLINE-1 inserts); RESULT = the new line count" },
        { "SUBST", "FILE,OLD/A,NEW,ALL/S", "Replace exact text (must be unique unless ALL); RESULT = count<tab>line" },
        { "SAVEALL", "", "Save every modified file" },
        { "REVERT", "FILE", "Drop unsaved changes: reload the file from disk" },
        { "STARTBUILD", "REBUILD/S,CLEAN/S", "Start a build now (no requesters); poll GETSTATE for build=idle" },
        { "STOPBUILD", "", "Stop the running build" },
        { "ERRORS", "ALL/S", "RESULT = the last build's messages: kind, file, line, col, text, explanation" },
        { "BUILDLOG", "", "RESULT = the build log as shown" },
        { "RUNPROGRAM", "CAPTURE/S", "Run the built program (no build); CAPTURE: output to a file for RUNOUTPUT" },
        { "RUNOUTPUT", "", "RESULT = \"running=0|1\" and the captured output of RUNPROGRAM CAPTURE" },
        { "STOPPROGRAM", "", "Send Ctrl-C to the program started with RUNPROGRAM CAPTURE" },
        { "DEBUGSTART", "MAIN/S,NOMAIN/S", "Start debugging the built program (no build); MAIN/NOMAIN: stop at main or not" },
        { "DEBUGSTOP", "", "Stop debugging (kills the program)" },
        { "CONTINUE", "", "Continue the stopped program" },
        { "STEP", "", "Step into (one source line)" },
        { "NEXT", "", "Step over (one source line)" },
        { "STEPOUT", "", "Run until the current function returns" },
        { "PAUSE", "", "Stop the running program where it is" },
        { "RUNTO", "FILE/A,LINE/N/A", "Continue until a line" },
        { "BREAK", "FILE/A,LINE/N/A,COND", "Set a breakpoint (COND: stop only when it's non-zero)" },
        { "UNBREAK", "FILE,LINE/N,ALL/S", "Clear a breakpoint, or ALL of them" },
        { "BREAKS", "", "RESULT = the breakpoints: file, line, enabled, condition, hits, address" },
        { "LOCALS", "", "RESULT = the selected frame's parameters and locals: name, type, value, children" },
        { "STACK", "", "RESULT = the call stack: level, function, file, line, pc, module" },
        { "FRAME", "LEVEL/N/A", "Select a stack frame for LOCALS and EVAL" },
        { "EVAL", "CHILDREN/S,EXPR/A/F", "RESULT = name, type, value of an expression (CHILDREN: and its members)" },
        { "REGS", "", "RESULT = the registers: name, value" },
        { "CRASH", "", "RESULT = the explanation of the last crash" },
        { "SYMBOLS", "FILE", "RESULT = the outline of a file: kind, name, line, detail" },
        { "REFERENCES", "NAME/A", "RESULT = every use of an identifier in the project: file, line, col, text" },
        { "GITSTATUS", "", "RESULT = \"repo<tab>branch\", then changed files: XY, path" },
        // --- end mcp ---
    };
    return cmds;
}

const CommandDef *findCommand(const std::string &name)
{
    for (auto &c : commands())
        if (strcasecmp(c.name, name.c_str()) == 0) return &c;
    return nullptr;
}

std::string Args::str(const std::string &n, const std::string &def) const
{
    auto it = values.find(n);
    return it == values.end() ? def : it->second;
}

long Args::num(const std::string &n, long def) const
{
    auto it = values.find(n);
    return it == values.end() ? def : strtol(it->second.c_str(), nullptr, 10);
}

std::vector<std::string> tokenize(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i >= s.size()) break;
        std::string w;
        if (s[i] == '"') {
            i++;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '*' && i + 1 < s.size()) {        // AmigaDOS escapes: *" *n *e **
                    char n = s[i + 1];
                    w += n == 'n' || n == 'N' ? '\n' : n == 'e' || n == 'E' ? '\x1b' : n;
                    i += 2;
                    continue;
                }
                w += s[i++];
            }
            i++;   // closing quote
        } else {
            while (i < s.size() && !isspace((unsigned char)s[i])) w += s[i++];
        }
        out.push_back(w);
    }
    return out;
}

static std::string upper(std::string s)
{
    for (auto &c : s) c = (char)toupper((unsigned char)c);
    return s;
}

struct Field { std::string name; bool required = false, number = false, rest = false, sw = false; };

static std::vector<Field> parseTemplate(const std::string &t)
{
    std::vector<Field> out;
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find(',', p);
        std::string part = t.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? t.size() : e + 1;
        Field f;
        size_t sl = part.find('/');
        f.name = upper(part.substr(0, sl));
        for (size_t k = sl; k != std::string::npos && k + 1 < part.size(); k = part.find('/', k + 1)) {
            char m = (char)toupper((unsigned char)part[k + 1]);
            if (m == 'A') f.required = true;
            else if (m == 'N') f.number = true;
            else if (m == 'F') f.rest = true;
            else if (m == 'S') f.sw = true;
        }
        if (!f.name.empty()) out.push_back(f);
    }
    return out;
}

static bool isNumber(const std::string &s)
{
    if (s.empty()) return false;
    size_t i = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

bool parseArgs(const std::string &tmpl, const std::string &args, Args &out, std::string &err)
{
    out.values.clear();
    std::vector<Field> fields = parseTemplate(tmpl);
    // a /F field takes the rest of the line verbatim (quotes are kept unless they wrap it all)
    std::string rest = args;
    while (!rest.empty() && isspace((unsigned char)rest[0])) rest.erase(0, 1);
    while (!rest.empty() && isspace((unsigned char)rest.back())) rest.pop_back();

    std::vector<bool> filled(fields.size(), false);
    auto assign = [&](size_t fi, const std::string &v) -> bool {
        if (fields[fi].number && !isNumber(v)) { err = fields[fi].name + " must be a number"; return false; }
        out.values[fields[fi].name] = v;
        filled[fi] = true;
        return true;
    };
    size_t pos = 0;   // character position in rest
    while (pos < rest.size()) {
        while (pos < rest.size() && isspace((unsigned char)rest[pos])) pos++;
        if (pos >= rest.size()) break;
        // the next unfilled positional field
        size_t next = fields.size();
        for (size_t i = 0; i < fields.size(); i++)
            if (!filled[i] && !fields[i].sw) { next = i; break; }
        // one word (or quoted string) from pos
        std::vector<std::string> words = tokenize(rest.substr(pos));
        if (words.empty()) break;
        std::string w = words[0];
        // NAME=value or NAME value or a switch
        std::string key = upper(w.substr(0, w.find('=')));
        size_t named = fields.size();
        for (size_t i = 0; i < fields.size(); i++)
            if (fields[i].name == key && rest[pos] != '"') { named = i; break; }
        auto advance = [&]() {   // past the first word
            size_t q = pos;
            if (rest[q] == '"') { q = rest.find('"', q + 1); q = q == std::string::npos ? rest.size() : q + 1; }
            else while (q < rest.size() && !isspace((unsigned char)rest[q])) q++;
            pos = q;
        };
        if (named < fields.size()) {
            if (fields[named].sw) {
                out.values[fields[named].name] = "1";
                filled[named] = true;
                advance();
                continue;
            }
            std::string value;
            size_t eq = w.find('=');
            if (eq != std::string::npos) {
                if (fields[named].rest) {
                    size_t e = rest.find('=', pos);
                    value = rest.substr(e + 1);
                    pos = rest.size();
                } else {
                    value = w.substr(eq + 1);
                    advance();
                }
            } else {
                advance();
                while (pos < rest.size() && isspace((unsigned char)rest[pos])) pos++;
                if (pos >= rest.size()) { err = "no value for " + fields[named].name; return false; }
                if (fields[named].rest) { value = rest.substr(pos); pos = rest.size(); }
                else {
                    std::vector<std::string> vw = tokenize(rest.substr(pos));
                    value = vw.empty() ? "" : vw[0];
                    advance();
                }
            }
            if (fields[named].rest && value.size() >= 2 && value.front() == '"' && value.back() == '"' &&
                value.find('"', 1) == value.size() - 1)
                value = value.substr(1, value.size() - 2);
            if (!assign(named, value)) return false;
            continue;
        }
        if (next >= fields.size()) { err = "too many arguments: " + rest.substr(pos); return false; }
        if (fields[next].rest) {
            std::string value = rest.substr(pos);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"' && value.find('"', 1) == value.size() - 1)
                value = value.substr(1, value.size() - 2);
            if (!assign(next, value)) return false;
            pos = rest.size();
            continue;
        }
        if (!assign(next, w)) return false;
        advance();
    }
    for (size_t i = 0; i < fields.size(); i++)
        if (fields[i].required && !filled[i]) { err = "missing " + fields[i].name; return false; }
    return true;
}

void splitCommand(const std::string &line, std::string &cmd, std::string &rest)
{
    size_t a = 0;
    while (a < line.size() && isspace((unsigned char)line[a])) a++;
    size_t b = a;
    while (b < line.size() && !isspace((unsigned char)line[b])) b++;
    cmd = upper(line.substr(a, b - a));
    while (b < line.size() && isspace((unsigned char)line[b])) b++;
    rest = line.substr(b);
}

std::string helpText()
{
    std::string s;
    for (auto &c : commands()) {
        if (!s.empty()) s += "\n";
        s += c.name;
        if (*c.tmpl) s += std::string(" ") + c.tmpl;
        s += std::string("  - ") + c.help;
    }
    return s;
}

} // namespace rexx
} // namespace af
