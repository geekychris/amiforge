// --- lang --- Language detection and per-language editing rules (see Languages.h).
#include "Languages.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace af {

namespace {

std::string lower(const std::string &s)
{
    std::string o;
    for (char c : s) o.push_back((char)std::tolower((unsigned char)c));
    return o;
}

std::string trimmed(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Words of a line (identifier characters), lower case.
std::vector<std::string> words(const std::string &s)
{
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (std::isalnum((unsigned char)c) || c == '_') w.push_back((char)std::tolower((unsigned char)c));
        else if (!w.empty()) { out.push_back(w); w.clear(); }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

const struct { Language lang; const char *id, *name; } names[] = {
    { Language::Text, "text", "Plain text" },
    { Language::C, "c", "C/C++" },
    { Language::Asm, "asm", "PPC assembler" },
    { Language::Make, "make", "Makefile" },
    { Language::Props, "props", "Settings (ini)" },
    { Language::Diff, "diff", "Diff" },
    { Language::Python, "python", "Python" },
    { Language::Lua, "lua", "Lua" },
    { Language::Perl, "perl", "Perl" },
    { Language::Shell, "shell", "Shell script" },
    { Language::AmigaDOS, "amigados", "AmigaDOS script" },
    { Language::ARexx, "arexx", "ARexx" },
    { Language::JSON, "json", "JSON" },
    { Language::YAML, "yaml", "YAML" },
    { Language::Markdown, "markdown", "Markdown" },
};

const char pyKeywords[] =
    "False None True and as assert async await break case class continue def del elif else except "
    "finally for from global if import in is lambda match nonlocal not or pass raise return try type "
    "while with yield";
const char pyBuiltins[] =
    "abs aiter all anext any ascii bin bool breakpoint bytearray bytes callable chr classmethod compile "
    "complex delattr dict dir divmod enumerate eval exec exit filter float format frozenset getattr "
    "globals hasattr hash help hex id input int isinstance issubclass iter len list locals map max "
    "memoryview min next object oct open ord pow print property quit range repr reversed round set "
    "setattr slice sorted staticmethod str sum super tuple type vars zip __import__ self cls "
    "__name__ __main__ __file__ __doc__ __init__ __repr__ __str__ __enter__ __exit__ NotImplemented Ellipsis "
    "BaseException Exception ArithmeticError AssertionError AttributeError EOFError FileNotFoundError "
    "ImportError IndentationError IndexError KeyError KeyboardInterrupt LookupError MemoryError "
    "ModuleNotFoundError NameError NotImplementedError OSError OverflowError PermissionError "
    "RecursionError RuntimeError StopIteration SyntaxError SystemExit TabError TimeoutError TypeError "
    "UnicodeDecodeError UnicodeEncodeError UnicodeError ValueError ZeroDivisionError";
const char luaKeywords[] =
    "and break do else elseif end false for function goto if in local nil not or repeat return then "
    "true until while";
const char luaBuiltins[] =
    "assert collectgarbage dofile error getmetatable ipairs load loadfile next pairs pcall print rawequal "
    "rawget rawlen rawset require select setmetatable tonumber tostring type xpcall _G _VERSION";
const char luaLibrary[] =
    "coroutine.create coroutine.resume coroutine.status coroutine.wrap coroutine.yield io.close io.lines "
    "io.open io.read io.write math.abs math.ceil math.floor math.huge math.max math.min math.pi "
    "math.random math.sqrt os.clock os.date os.execute os.exit os.getenv os.remove os.rename os.time "
    "string.byte string.char string.find string.format string.gmatch string.gsub string.len "
    "string.lower string.match string.rep string.reverse string.sub string.upper table.concat "
    "table.insert table.remove table.sort table.unpack";
const char perlKeywords[] =
    "if elsif else unless while until for foreach do last next redo return sub my our local use no "
    "require package BEGIN END and or not eq ne lt gt le ge cmp x qw q qq m s tr y print printf open "
    "close die warn eval defined undef exists delete shift unshift push pop splice split join keys "
    "values each scalar ref bless chomp chop length substr index sprintf lc uc sort reverse map grep "
    "wantarray exit __END__ __DATA__";
const char shKeywords[] =
    "if then else elif fi case esac for while until do done in function select time return break "
    "continue exit export local readonly declare set unset shift source alias echo printf read cd pwd "
    "test true false trap exec eval";
const char rexxInstructions[] =
    "address arg break call do drop echo else end exit if interpret iterate leave nop numeric options "
    "otherwise parse procedure pull push queue return say select shell signal then trace upper when "
    "by for forever to until while with value var source version external expose digits fuzz form "
    "scientific engineering on off error halt ioerr novalue syntax failure break_c break_d break_e "
    "break_f results prompt failat cache";
const char rexxBuiltins[] =
    "abbrev abs addlib address arg b2c bitand bitchg bitclr bitcomp bitor bitset bittst bitxor c2b "
    "c2d c2x center centre close compare compress copies d2c d2x datatype date delstr delword digits "
    "eof errortext exists export find form forbid freespace fuzz getclip getspace hash import index "
    "insert lastpos left length lines max min open overlay permit pos pragma random randu readch "
    "readln remlib reverse right seek setclip show sign sourceline space storage strip substr subword "
    "symbol time trace translate trim upper value verify word wordindex wordlength words writech "
    "writeln x2c x2d xrange";
const char rexxSpecials[] = "rc result sigl";
const char dosCommands[] =
    "if else endif skip lab echo quit failat set unset setenv unsetenv getenv run execute ask wait "
    "assign copy delete rename makedir list dir cd type version path resident stack prompt protect "
    "alias why endcli endshell newshell newcli date mount info relabel format eval search sort join "
    "which avail break status changetaskpri lock filenote setdate makelink requestchoice requestfile "
    "addbuffers setpatch iprefs conclip loadmonitors adddatatypes rexxmast mounter "
    "cpu setclock binddrivers loadwb mount c execute sdk-startup";
const char jsonKeywords[] = "true false null";
const char yamlKeywords[] = "true false yes no null on off";

} // namespace

const char *languageKeywords(Language l, int set)
{
    switch (l) {
    case Language::Python: return set == 0 ? pyKeywords : set == 1 ? pyBuiltins : "";
    case Language::Lua: return set == 0 ? luaKeywords : set == 1 ? luaBuiltins : set == 2 ? luaLibrary : "";
    case Language::Perl: return set == 0 ? perlKeywords : "";
    case Language::Shell: return set == 0 ? shKeywords : "";
    case Language::ARexx: return set == 0 ? rexxInstructions : set == 1 ? rexxBuiltins : set == 2 ? rexxSpecials : "";
    case Language::AmigaDOS: return set == 0 ? dosCommands : "";
    case Language::JSON: return set == 0 ? jsonKeywords : "";
    case Language::YAML: return set == 0 ? yamlKeywords : "";
    default: return "";
    }
}

const char *languageName(Language l)
{
    for (auto &n : names) if (n.lang == l) return n.name;
    return "Plain text";
}

const char *languageId(Language l)
{
    for (auto &n : names) if (n.lang == l) return n.id;
    return "text";
}

Language languageFromId(const std::string &id)
{
    std::string l = lower(id);
    for (auto &n : names) if (l == n.id) return n.lang;
    return Language::Text;
}

Language languageFor(const std::string &fileName, const std::string &firstLine)
{
    std::string path = fileName;
    std::string name = path;
    const size_t slash = name.find_last_of("/:");
    std::string dir = slash == std::string::npos ? "" : lower(name.substr(0, slash + 1));
    if (slash != std::string::npos) name.erase(0, slash + 1);
    std::string lname = lower(name);
    // a bare language name (documents that only have a title)
    if (lname == "c" || lname == "cpp") return Language::C;
    if (lname == "asm") return Language::Asm;
    if (lname == "makefile" || lname == "gnumakefile" || lname == "smakefile") return Language::Make;
    if (lname == "props") return Language::Props;
    if (lname == "diff") return Language::Diff;
    for (auto &n : names)
        if (lname == n.id && n.lang != Language::Text) return n.lang;
    const size_t dot = lname.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : lname.substr(dot + 1);
    if (ext == "c" || ext == "h" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++" ||
        ext == "hpp" || ext == "hh" || ext == "hxx")
        return Language::C;
    if (ext == "s" || ext == "asm" || ext == "i") return Language::Asm;
    if (ext == "mk" || ext == "mak" || lname.compare(0, 8, "makefile") == 0) return Language::Make;
    if (ext == "props" || ext == "properties" || ext == "ini" || ext == "cfg" || ext == "afp") return Language::Props;
    if (ext == "diff" || ext == "patch") return Language::Diff;
    if (ext == "py" || ext == "pyw") return Language::Python;
    if (ext == "lua") return Language::Lua;
    if (ext == "pl" || ext == "pm" || ext == "perl") return Language::Perl;
    if (ext == "sh" || ext == "bash" || ext == "zsh") return Language::Shell;
    if (ext == "rexx" || ext == "rx" || ext == "arexx" || ext == "ced" || ext == "ged") return Language::ARexx;
    if (ext == "json" || ext == "afjson") return Language::JSON;
    if (ext == "yaml" || ext == "yml") return Language::YAML;
    if (ext == "md" || ext == "markdown") return Language::Markdown;
    if (ext == "dos" || ext == "script") return Language::AmigaDOS;
    // no (known) extension: the first line decides
    std::string first = lower(trimmed(firstLine));
    if (first.compare(0, 2, "#!") == 0) {
        if (first.find("python") != std::string::npos) return Language::Python;
        if (first.find("lua") != std::string::npos) return Language::Lua;
        if (first.find("perl") != std::string::npos) return Language::Perl;
        if (first.find("sh") != std::string::npos) return Language::Shell;
        if (first.find("rx") != std::string::npos || first.find("rexx") != std::string::npos) return Language::ARexx;
    }
    if (ext.empty()) {
        // An ARexx program starts with a comment; AmigaDOS scripts with .key/.bra/.k or a ';'
        // comment, or live in S:.
        if (first.compare(0, 2, "/*") == 0 && (dir == "rexx:" || first.find("rexx") != std::string::npos))
            return Language::ARexx;
        if (first.compare(0, 4, ".key") == 0 || first.compare(0, 2, ".k") == 0 ||
            first.compare(0, 4, ".bra") == 0 || first.compare(0, 4, ".ket") == 0 ||
            first.compare(0, 4, ".def") == 0)
            return Language::AmigaDOS;
        if (dir == "s:" || (dir.size() >= 3 && dir.compare(dir.size() - 3, 3, ":s/") == 0) ||
            (dir.size() >= 3 && dir.compare(dir.size() - 3, 3, "/s/") == 0))
            return Language::AmigaDOS;
        if (dir == "rexx:") return Language::ARexx;
    }
    return Language::Text;
}

CommentStyle commentStyle(Language l)
{
    switch (l) {
    case Language::C: return { "//", "/*", "*/" };
    case Language::Asm: return { "#", "/*", "*/" };
    case Language::Lua: return { "--", "--[[", "]]" };
    case Language::AmigaDOS: return { ";", nullptr, nullptr };
    case Language::ARexx: return { nullptr, "/*", "*/" };
    case Language::JSON: return { nullptr, nullptr, nullptr };
    case Language::Markdown: return { nullptr, "<!--", "-->" };
    case Language::Text: return { "#", nullptr, nullptr };
    default: return { "#", nullptr, nullptr };   // Python, Perl, shell, YAML, make, props, diff
    }
}

IndentRule indentRule(Language l)
{
    switch (l) {
    case Language::C: case Language::Perl: case Language::Shell: case Language::JSON: return IndentRule::Braces;
    case Language::Python: return IndentRule::Colon;
    case Language::Lua: case Language::ARexx: case Language::AmigaDOS: return IndentRule::Keywords;
    case Language::YAML: return IndentRule::Colon;
    default: return IndentRule::Keep;
    }
}

int defaultIndentWidth(Language l)
{
    return l == Language::YAML || l == Language::JSON || l == Language::Lua ? 2 : 4;
}

bool usesTabs(Language l) { return l == Language::Make; }

bool foldable(Language l)
{
    switch (l) {
    case Language::C: case Language::Python: case Language::Lua: case Language::Perl: case Language::Shell:
    case Language::ARexx: case Language::JSON: case Language::YAML: return true;
    default: return false;
    }
}

std::string stripLineComment(Language l, const std::string &line)
{
    const CommentStyle cs = commentStyle(l);
    char quote = 0;
    size_t end = line.size();
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quote) {
            if (c == '\\' && l != Language::ARexx && l != Language::AmigaDOS) { i++; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || (c == '\'' && l != Language::AmigaDOS)) { quote = c; continue; }
        if (cs.line && line.compare(i, std::strlen(cs.line), cs.line) == 0) { end = i; break; }
        if (l == Language::ARexx && line.compare(i, 2, "/*") == 0) {
            size_t e = line.find("*/", i + 2);
            if (e == std::string::npos) { end = i; break; }
            i = e + 1;
        }
    }
    std::string s = line.substr(0, end);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    if (l == Language::ARexx) {
        // drop whole /* */ comments inside the line too
        std::string o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s.compare(i, 2, "/*") == 0) {
                size_t e = s.find("*/", i + 2);
                if (e == std::string::npos) break;
                i = e + 1;
                continue;
            }
            o.push_back(s[i]);
        }
        s = o;
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    }
    return s;
}

int indentChangeAfter(Language l, const std::string &line)
{
    const std::string code = trimmed(stripLineComment(l, line));
    if (code.empty()) return 0;
    const std::vector<std::string> w = words(code);
    const std::string first = w.empty() ? "" : w.front(), last = w.empty() ? "" : w.back();
    switch (l) {
    case Language::Python: {
        if (code.back() == ':') return 1;
        if (code.back() == '(' || code.back() == '[' || code.back() == '{') return 1;
        static const char *const enders[] = { "return", "pass", "break", "continue", "raise", nullptr };
        for (const char *const *e = enders; *e; e++)
            if (first == *e) return -1;
        return 0;
    }
    case Language::YAML:
        return code.back() == ':' || (code.size() >= 2 && code.compare(code.size() - 2, 2, ": ") == 0) ? 1 : 0;
    case Language::Lua: {
        int opens = 0;
        for (auto &x : w) {
            if (x == "function" || x == "then" || x == "do" || x == "repeat") opens++;
            if (x == "end" || x == "until") opens--;
            if (x == "elseif") opens--;   // "elseif ... then" opens no new block
        }
        if (first == "else") opens = 1;
        if (code.back() == '{' || code.back() == '(') opens++;
        return opens > 0 ? 1 : 0;
    }
    case Language::ARexx: {
        // DO / SELECT open a block (IF x THEN DO, DO i = 1 TO 10, DO FOREVER...); END closes it
        if (first == "select" || first == "do") return 1;
        for (size_t i = 0; i + 1 < w.size(); i++)
            if ((w[i] == "then" || w[i] == "else" || w[i] == "otherwise") && w[i + 1] == "do") return 1;
        if (first == "otherwise" && w.size() == 1) return 1;
        return 0;
    }
    case Language::AmigaDOS:
        return first == "if" || first == "else" ? 1 : 0;
    default:
        return 0;
    }
}

} // namespace af
