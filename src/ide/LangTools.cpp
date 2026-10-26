// --- lang --- Python project support that doesn't need the OS (see LangTools.h).
#include "LangTools.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace af {
namespace lang {

const char pyCheckScript[] = R"PY(# AmiForge: syntax check of a Python project (Build). Runs nothing, writes no .pyc files.
import sys, warnings
bad = 0
names = sys.argv[1:]
for name in names:
    try:
        with open(name, 'rb') as f:
            src = f.read()
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            compile(src, name, 'exec', dont_inherit=True)
        for w in caught:
            print('%s:%d:1: warning: %s: %s' % (name, w.lineno or 1, w.category.__name__, w.message))
    except SyntaxError as e:
        bad += 1
        print('%s:%d:%d: error: %s: %s' % (name, e.lineno or 1, e.offset or 1, type(e).__name__, e.msg))
        if e.text:
            print('    ' + e.text.rstrip())
    except (OSError, ValueError) as e:
        bad += 1
        print('%s:1:1: error: %s: %s' % (name, type(e).__name__, e))
n = len(names)
print('Checked %d file%s: %s.' % (n, '' if n == 1 else 's', 'no errors' if not bad else '%d with errors' % bad))
sys.exit(1 if bad else 0)
)PY";

const char pyRunScript[] = R"PY(# AmiForge: runs a Python project's main script (Build > Run).
# argv: STOPFILE TRACEBACKFILE script args...
import sys, os
_stop, _tb = sys.argv[1], sys.argv[2]
sys.argv = sys.argv[3:]
_script = sys.argv[0]
_dir = os.path.dirname(_script)
sys.path[0] = _dir
# Stop Program creates STOPFILE. No thread watches it: on the AmigaOS port a second thread can
# starve the whole machine. time.sleep() looks instead, so a script that sleeps stops at once.
import time as _time
_sleep = _time.sleep
def _stopping_sleep(seconds):
    end = _time.monotonic() + seconds
    while True:
        if os.path.exists(_stop):
            raise KeyboardInterrupt
        left = end - _time.monotonic()
        if left <= 0:
            return
        _sleep(min(left, 0.25))
_time.sleep = _stopping_sleep
import traceback, types, builtins
# like runpy.run_path, without making the path absolute (the AmigaOS port can't open the
# volume-name paths abspath() gives)
_main = types.ModuleType('__main__')
_main.__file__ = _script
_main.__builtins__ = builtins
sys.modules['__main__'] = _main
try:
    with open(_script, 'rb') as _f:
        _code = compile(_f.read(), _script, 'exec', dont_inherit=True)
    exec(_code, _main.__dict__)
except KeyboardInterrupt:
    sys.stderr.write('\nStopped (KeyboardInterrupt).\n')
    sys.exit(5)
except SystemExit:
    raise
except BaseException as e:
    tb = e.__traceback__
    while tb is not None and tb.tb_frame.f_code.co_filename != _script:
        tb = tb.tb_next
    # no frame of the script (a SyntaxError, a missing file): just the exception, with its location
    text = ''.join(traceback.format_exception(type(e), e, tb))
    sys.stderr.write(text)
    try:
        with open(_tb, 'w') as f:
            f.write(text)
    except OSError:
        pass
    sys.exit(10)
)PY";

std::string exceptionName(const std::string &line)
{
    size_t i = 0;
    while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_' || line[i] == '.')) i++;
    if (i == 0 || (i < line.size() && line[i] != ':')) return "";
    std::string name = line.substr(0, i);
    // the last part of a dotted name ("json.decoder.JSONDecodeError") is the class
    size_t dot = name.rfind('.');
    std::string base = dot == std::string::npos ? name : name.substr(dot + 1);
    if (base.empty() || !std::isupper((unsigned char)base[0])) return "";
    static const char *const endings[] = { "Error", "Exception", "Warning", "Interrupt", "Exit", "Iteration", nullptr };
    for (const char *const *e = endings; *e; e++) {
        size_t n = std::strlen(*e);
        if (base.size() >= n && base.compare(base.size() - n, n, *e) == 0) return name;
    }
    return "";
}

// `File "x.py", line 12, in f` (leading blanks allowed) -> file, line
static bool parseFileLine(const std::string &l, std::string &file, int &line)
{
    size_t p = l.find_first_not_of(" \t");
    if (p == std::string::npos || l.compare(p, 6, "File \"") != 0) return false;
    size_t q = l.find('"', p + 6);
    if (q == std::string::npos) return false;
    file = l.substr(p + 6, q - p - 6);
    size_t k = l.find(", line ", q);
    if (k != q + 1) return false;
    const char *s = l.c_str() + k + 7;
    if (!std::isdigit((unsigned char)*s)) return false;
    line = (int)std::strtol(s, nullptr, 10);
    return !file.empty() && file[0] != '<';   // "<string>", "<frozen ...>" aren't files
}

Diag TracebackParser::feed(const std::string &raw)
{
    std::string l = raw;
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
    Diag d;
    if (l.compare(0, 34, "Traceback (most recent call last):") == 0) {
        inTrace = true;
        lastFile.clear();
        lastLine = 0;
        d.kind = DiagKind::Info;
        d.text = l;
        return d;
    }
    std::string file;
    int line = 0;
    if (parseFileLine(l, file, line)) {
        lastFile = file;
        lastLine = line;
        d.kind = DiagKind::Location;
        d.file = file;
        d.line = line;
        d.text = l;
        return d;
    }
    // "x.py:3: SyntaxWarning: msg" (warnings module format)
    {
        size_t c1 = l.find(".py:");
        if (c1 != std::string::npos && l.find(' ') > c1) {
            size_t ls = c1 + 4;
            size_t le = ls;
            while (le < l.size() && std::isdigit((unsigned char)l[le])) le++;
            if (le > ls && le + 2 < l.size() && l[le] == ':' && l[le + 1] == ' ') {
                std::string rest = l.substr(le + 2);
                std::string ex = exceptionName(rest);
                if (!ex.empty() && ex.find("Warning") != std::string::npos) {
                    d.kind = DiagKind::Warning;
                    d.file = l.substr(0, c1 + 3);
                    d.line = std::atoi(l.c_str() + ls);
                    d.text = rest;
                    return d;
                }
            }
        }
    }
    std::string ex = exceptionName(l);
    if (!ex.empty() && (inTrace || !lastFile.empty())) {
        d.kind = ex.find("Warning") != std::string::npos ? DiagKind::Warning : DiagKind::Error;
        d.file = lastFile;
        d.line = lastLine;
        d.text = l;
        inTrace = false;
        lastFile.clear();
        lastLine = 0;
        return d;
    }
    return d;
}

const std::vector<std::string> &interpreterCandidates()
{
    static const std::vector<std::string> list = {
        // Python 3 only: AmigaOS 4.1's own C:python is Python 2.5, which can't run Python 3 code
        "C:python3", "SYS:Utilities/Python/python3", "Python3:python3", "Python:python3",
        "DH1:python-os4", "DH1:python3", "Work:Python/python3",
    };
    return list;
}

static std::string trimmed(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string findInterpreter(const std::string &projectSetting, const std::string &envSetting,
                            const ExistsFn &exists, std::string *how)
{
    std::string p = trimmed(projectSetting), e = trimmed(envSetting);
    if (!p.empty()) {
        if (how) *how = "project setting";
        return p;
    }
    if (!e.empty()) {
        if (how) *how = "ENV:AmiForge/Python";
        return e;
    }
    for (auto &c : interpreterCandidates())
        if (exists(c)) {
            if (how) *how = "found";
            return c;
        }
    if (how) *how = "";
    return "";
}

static std::string drawerOf(const std::string &path)
{
    size_t s = path.find_last_of("/:");
    if (s == std::string::npos) return "";
    return path[s] == ':' ? path.substr(0, s + 1) : path.substr(0, s);
}

static std::string join(const std::string &dir, const std::string &name)
{
    if (dir.empty()) return name;
    char last = dir.back();
    return last == ':' || last == '/' ? dir + name : dir + "/" + name;
}

std::string pythonHomeFor(const std::string &interpreter, const ExistsFn &exists)
{
    std::string dir = drawerOf(interpreter);
    if (dir.empty()) return "";
    return exists(join(join(dir, "lib"), "os.py")) ? dir : "";
}

std::string pythonPathFor(const std::string &home)
{
    return home.empty() ? "" : join(home, "lib");
}

std::string quoteArg(const std::string &s)
{
    if (!s.empty() && s.find_first_of(" \t\"*;=") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += "*\"";
        else if (c == '*') o += "**";
        else o += c;
    }
    return o + "\"";
}

std::string checkCommand(const std::string &interpreter, const std::string &checker,
                         const std::vector<std::string> &files)
{
    std::string c = quoteArg(interpreter) + " " + quoteArg(checker);
    for (auto &f : files) c += " " + quoteArg(f);
    return c;
}

std::string runCommand(const std::string &interpreter, const std::string &runner, const std::string &stopFile,
                       const std::string &tracebackFile, const std::string &script, const std::string &args)
{
    std::string c = quoteArg(interpreter) + " -u " + quoteArg(runner) + " " + quoteArg(stopFile) + " " +
                    quoteArg(tracebackFile) + " " + quoteArg(script);
    std::string a = trimmed(args);
    if (!a.empty()) c += " " + a;   // as typed: the user quotes them for the shell
    return c;
}

std::string pdbCommand(const std::string &interpreter, const std::string &script, const std::string &args,
                       const std::vector<std::string> &commands)
{
    std::string c = quoteArg(interpreter) + " -m pdb";
    for (auto &k : commands) c += " -c " + quoteArg(k);
    c += " " + quoteArg(script);
    std::string a = trimmed(args);
    if (!a.empty()) c += " " + a;
    return c;
}

std::vector<std::string> pdbBreakCommands(const std::vector<PyBreakpoint> &bps)
{
    std::vector<std::string> out;
    for (auto &b : bps) {
        if (b.line <= 0 || b.file.empty()) continue;
        std::string k = "b " + b.file + ":" + std::to_string(b.line);
        std::string cond = trimmed(b.condition);
        if (!cond.empty()) k += ", " + cond;
        out.push_back(k);
    }
    if (!out.empty()) out.push_back("continue");
    return out;
}

bool isPythonFile(const std::string &path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || path.find_first_of("/:", dot) != std::string::npos) return false;
    std::string e = path.substr(dot + 1);
    for (auto &c : e) c = (char)std::tolower((unsigned char)c);
    return e == "py" || e == "pyw";
}

} // namespace lang
} // namespace af
