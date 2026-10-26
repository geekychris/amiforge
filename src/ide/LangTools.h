// --- lang --- Python projects: the syntax checker and runner scripts AmiForge hands to the
// interpreter, the parser that turns Python's error output into build log entries, and finding
// the interpreter. String processing only (the file checks are passed in), so the host tests
// can check it.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace af {
namespace lang {

// Written to T: and run by the interpreter.
// The checker (Build): compile() on every file named on its command line, without running or
// writing .pyc files; prints "file:line:col: error: SyntaxError: msg" (and "warning:" for
// SyntaxWarning) in GCC's format, then a summary line; returns 1 if a file has errors.
extern const char pyCheckScript[];
// The runner (Run): runner STOPFILE TRACEBACKFILE script args... It runs the script as
// __main__ (its drawer first on sys.path), writes the traceback of an uncaught exception to
// TRACEBACKFILE as well as to the console, and turns the IDE's Stop Program (which creates
// STOPFILE) into a KeyboardInterrupt: Python on AmigaOS 4 doesn't react to Ctrl-C by itself.
extern const char pyRunScript[];

enum class DiagKind { None, Info, Location, Error, Warning };
struct Diag {
    DiagKind kind = DiagKind::None;
    std::string file;        // as Python printed it (relative to the directory it ran in, or absolute)
    int line = 0;
    std::string text;        // the message, for Error/Warning ("NameError: name 'x' is not defined")
};

// Python's tracebacks and compiler errors, one output line at a time:
//   Traceback (most recent call last):          -> Info
//     File "x.py", line 12, in main             -> Location (clickable)
//     File "x.py", line 3                       -> Location (SyntaxError form)
//   NameError: name 'y' is not defined          -> Error at the last Location
//   x.py:3: SyntaxWarning: invalid escape...    -> Warning
// Anything else is None (ordinary program output).
class TracebackParser {
public:
    Diag feed(const std::string &line);
    void reset() { lastFile.clear(); lastLine = 0; inTrace = false; }
private:
    std::string lastFile;
    int lastLine = 0;
    bool inTrace = false;
};

// "SyntaxError", "NameError", "KeyboardInterrupt"... at the start of `line` followed by ':' or
// the end: the exception's name, else "".
std::string exceptionName(const std::string &line);

// --- the interpreter ---
using ExistsFn = std::function<bool(const std::string &path)>;
// Where AmiForge looks when neither the project nor ENV:AmiForge/Python names one.
const std::vector<std::string> &interpreterCandidates();
// The project's setting, else ENV:AmiForge/Python, else the first candidate that exists.
// `how` says which ("project setting", "ENV:AmiForge/Python", "found"). Empty if none.
std::string findInterpreter(const std::string &projectSetting, const std::string &envSetting,
                            const ExistsFn &exists, std::string *how = nullptr);
// The PYTHONHOME for an interpreter whose standard library is in its own drawer's lib/
// ("DH1:python-os4" -> "DH1:" when DH1:lib/os.py exists); "" when that isn't the layout.
std::string pythonHomeFor(const std::string &interpreter, const ExistsFn &exists);
// PYTHONPATH for a PYTHONHOME ("DH1:" -> "DH1:lib", "Work:Python" -> "Work:Python/lib").
std::string pythonPathFor(const std::string &home);

// --- command lines (AmigaDOS quoting) ---
std::string quoteArg(const std::string &s);
std::string checkCommand(const std::string &interpreter, const std::string &checker,
                         const std::vector<std::string> &files);
std::string runCommand(const std::string &interpreter, const std::string &runner, const std::string &stopFile,
                       const std::string &tracebackFile, const std::string &script, const std::string &args);
// pdb with commands to run first (-c "b main.py:12", -c continue)
std::string pdbCommand(const std::string &interpreter, const std::string &script, const std::string &args,
                       const std::vector<std::string> &commands = {});
// The pdb commands for breakpoints: "b file:line" or "b file:line, condition"; then "continue"
// so the script runs to the first one (none: pdb stops at the first line).
struct PyBreakpoint { std::string file; int line; std::string condition; };
std::vector<std::string> pdbBreakCommands(const std::vector<PyBreakpoint> &bps);

bool isPythonFile(const std::string &path);    // .py, .pyw

} // namespace lang
} // namespace af
