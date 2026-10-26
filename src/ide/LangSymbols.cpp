// --- lang --- Outline, masking and identifier search for the languages other than C/C++ and
// assembler (see Symbols.h). Line-based and textual, like the C outline: no parser.
#include "Symbols.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <sstream>

namespace af {

namespace {

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
bool rexxIdentChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_' || c == '!' || c == '?' || c == '$' || c == '@' || c == '#';
}

std::string lowerStr(const std::string &s)
{
    std::string o = s;
    for (auto &c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

struct Masker {
    const std::string &t;
    std::string m;
    size_t n;
    explicit Masker(const std::string &text) : t(text), m(text), n(text.size()) {}
    void blank(size_t a, size_t b)
    {
        for (size_t k = a; k < b && k < n; k++)
            if (m[k] != '\n') m[k] = ' ';
    }
    size_t lineEnd(size_t i) const
    {
        size_t e = t.find('\n', i);
        return e == std::string::npos ? n : e;
    }
    // a quoted string from i (t[i] is the quote); `escape` is the escape character (0: none);
    // `doubled`: a doubled quote stands for one (ARexx). Strings end at the end of the line.
    size_t quoted(size_t i, char escape, bool doubled) const
    {
        char q = t[i];
        size_t e = i + 1;
        while (e < n && t[e] != '\n') {
            if (escape && t[e] == escape && e + 1 < n) { e += 2; continue; }
            if (t[e] == q) {
                if (doubled && e + 1 < n && t[e + 1] == q) { e += 2; continue; }
                return e + 1;
            }
            e++;
        }
        return e;
    }
};

std::string maskPython(const std::string &t)
{
    Masker k(t);
    size_t i = 0;
    while (i < k.n) {
        char c = t[i];
        if (c == '#') {
            size_t e = k.lineEnd(i);
            k.blank(i, e);
            i = e;
        } else if (c == '"' || c == '\'') {
            if (i + 2 < k.n && t[i + 1] == c && t[i + 2] == c) {
                std::string close(3, c);
                size_t e = t.find(close, i + 3);
                // a backslash before the closing quotes doesn't end it: good enough to ignore
                e = e == std::string::npos ? k.n : e + 3;
                k.blank(i, e);
                i = e;
            } else {
                size_t e = k.quoted(i, '\\', false);
                k.blank(i, e);
                i = e;
            }
        } else {
            i++;
        }
    }
    return k.m;
}

std::string maskLua(const std::string &t)
{
    Masker k(t);
    size_t i = 0;
    // [[ ... ]] or [==[ ... ]==] from i (t[i] == '['); returns the end, or npos if not one
    auto longBracket = [&](size_t at) -> size_t {
        size_t p = at + 1;
        size_t eq = 0;
        while (p < k.n && t[p] == '=') { p++; eq++; }
        if (p >= k.n || t[p] != '[') return std::string::npos;
        std::string close = "]" + std::string(eq, '=') + "]";
        size_t e = t.find(close, p + 1);
        return e == std::string::npos ? k.n : e + close.size();
    };
    while (i < k.n) {
        char c = t[i];
        if (c == '-' && i + 1 < k.n && t[i + 1] == '-') {
            size_t e = i + 2 < k.n && t[i + 2] == '[' ? longBracket(i + 2) : std::string::npos;
            if (e == std::string::npos) e = k.lineEnd(i);
            k.blank(i, e);
            i = e;
        } else if (c == '[') {
            size_t e = longBracket(i);
            if (e == std::string::npos) { i++; continue; }
            k.blank(i, e);
            i = e;
        } else if (c == '"' || c == '\'') {
            size_t e = k.quoted(i, '\\', false);
            k.blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return k.m;
}

// Perl, shell, YAML: '#' comments (at a word start), '...' and "..." strings; Perl's POD.
std::string maskHash(const std::string &t, Language lang)
{
    Masker k(t);
    size_t i = 0;
    bool lineStart = true;
    while (i < k.n) {
        char c = t[i];
        if (c == '\n') { lineStart = true; i++; continue; }
        if (lang == Language::Perl && lineStart && c == '=' && i + 1 < k.n && std::isalpha((unsigned char)t[i + 1])) {
            size_t e = t.find("\n=cut", i);
            e = e == std::string::npos ? k.n : k.lineEnd(e + 1);
            k.blank(i, e);
            i = e;
            continue;
        }
        lineStart = false;
        bool wordStart = i == 0 || std::isspace((unsigned char)t[i - 1]) || t[i - 1] == ';';
        if (c == '#' && (wordStart || lang == Language::Perl) && !(i > 0 && t[i - 1] == '$')) {
            size_t e = k.lineEnd(i);
            k.blank(i, e);
            i = e;
        } else if (c == '"' || (c == '\'' && lang != Language::YAML) ||
                   (c == '\'' && lang == Language::YAML && wordStart)) {
            size_t e = k.quoted(i, c == '\'' && lang != Language::Perl ? 0 : '\\', lang == Language::YAML && c == '\'');
            k.blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return k.m;
}

std::string maskARexx(const std::string &t)
{
    Masker k(t);
    size_t i = 0;
    while (i < k.n) {
        char c = t[i];
        if (c == '/' && i + 1 < k.n && t[i + 1] == '*') {
            int depth = 0;
            size_t e = i;
            while (e < k.n) {
                if (t.compare(e, 2, "/*") == 0) { depth++; e += 2; }
                else if (t.compare(e, 2, "*/") == 0) { e += 2; if (--depth == 0) break; }
                else e++;
            }
            k.blank(i, e);
            i = e;
        } else if (c == '"' || c == '\'') {
            size_t e = k.quoted(i, 0, true);
            k.blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return k.m;
}

std::string maskAmigaDOS(const std::string &t)
{
    Masker k(t);
    size_t i = 0;
    while (i < k.n) {
        char c = t[i];
        if (c == ';') {
            size_t e = k.lineEnd(i);
            k.blank(i, e);
            i = e;
        } else if (c == '"') {
            size_t e = k.quoted(i, '*', false);
            k.blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return k.m;
}

std::string maskJSON(const std::string &t)
{
    Masker k(t);
    size_t i = 0;
    while (i < k.n) {
        if (t.compare(i, 2, "//") == 0) {
            size_t e = k.lineEnd(i);
            k.blank(i, e);
            i = e;
        } else if (t.compare(i, 2, "/*") == 0) {
            size_t e = t.find("*/", i + 2);
            e = e == std::string::npos ? k.n : e + 2;
            k.blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return k.m;
}

// Lines of a text (without the newlines).
std::vector<std::string> lines(const std::string &t)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= t.size()) {
        size_t e = t.find('\n', p);
        if (e == std::string::npos) e = t.size();
        std::string l = t.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        if (e >= t.size()) break;
        p = e + 1;
    }
    return out;
}

size_t indentOf(const std::string &l)
{
    size_t w = 0;
    for (char c : l) {
        if (c == ' ') w++;
        else if (c == '\t') w = (w / 8 + 1) * 8;
        else break;
    }
    return w;
}

bool blankLine(const std::string &l) { return l.find_first_not_of(" \t") == std::string::npos; }

// The identifier at p (or "").
std::string identAt(const std::string &l, size_t p)
{
    size_t e = p;
    while (e < l.size() && identChar(l[e])) e++;
    if (e == p || std::isdigit((unsigned char)l[p])) return "";
    return l.substr(p, e - p);
}

size_t skipBlanks(const std::string &l, size_t p)
{
    while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) p++;
    return p;
}

bool startsWord(const std::string &l, size_t p, const char *w)
{
    size_t n = std::strlen(w);
    return l.compare(p, n, w) == 0 && (p + n >= l.size() || !identChar(l[p + n]));
}

std::string squeeze(const std::string &s)
{
    std::string o;
    bool sp = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { sp = true; continue; }
        if (sp && !o.empty() && o.back() != '(') o += ' ';
        sp = false;
        o += c;
    }
    return o;
}

// The parameter list starting at the '(' at line li, column p (it may go on over lines).
std::string paramList(const std::vector<std::string> &ls, size_t li, size_t p)
{
    std::string acc;
    int depth = 0;
    for (size_t i = li; i < ls.size() && i < li + 20; i++) {
        const std::string &l = ls[i];
        for (size_t k = i == li ? p : 0; k < l.size(); k++) {
            acc += l[k];
            if (l[k] == '(') depth++;
            else if (l[k] == ')' && --depth == 0) {
                std::string s = squeeze(acc);
                if (s.size() > 80) s = s.substr(0, 77) + "...";
                return s;
            }
        }
        acc += ' ';
    }
    return "(...)";
}

std::vector<Symbol> pythonOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskPython(text));
    struct Block { size_t indent; bool isClass; };
    std::vector<Block> stack;
    int paren = 0;              // inside brackets that span lines: not a statement start
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        if (blankLine(l)) continue;
        bool stmtStart = paren == 0;
        for (char c : l) {
            if (c == '(' || c == '[' || c == '{') paren++;
            else if ((c == ')' || c == ']' || c == '}') && paren > 0) paren--;
        }
        if (!stmtStart) continue;
        size_t ind = indentOf(l);
        while (!stack.empty() && ind <= stack.back().indent) stack.pop_back();
        size_t p = skipBlanks(l, 0);
        int depth = (int)stack.size();
        bool inClass = !stack.empty() && stack.back().isClass;
        if (startsWord(l, p, "async")) p = skipBlanks(l, p + 5);
        if (startsWord(l, p, "def")) {
            size_t q = skipBlanks(l, p + 3);
            std::string name = identAt(l, q);
            if (!name.empty()) {
                size_t open = l.find('(', q + name.size());
                std::string params = open == std::string::npos ? "" : paramList(ls, li, open);
                out.push_back({inClass ? SymKind::Method : SymKind::Function, name, (int)li + 1, depth, params});
                stack.push_back({ind, false});
            }
        } else if (startsWord(l, p, "class")) {
            size_t q = skipBlanks(l, p + 5);
            std::string name = identAt(l, q);
            if (!name.empty()) {
                out.push_back({SymKind::Class, name, (int)li + 1, depth, ""});
                stack.push_back({ind, true});
            }
        } else if (ind == 0 && stack.empty()) {
            // NAME = ..., NAME: type = ..., A, B = ...
            std::vector<std::string> names;
            size_t q = 0;
            while (true) {
                std::string name = identAt(l, q);
                if (name.empty() || isReservedWord(name, Language::Python)) { names.clear(); break; }
                names.push_back(name);
                q = skipBlanks(l, q + name.size());
                if (q < l.size() && l[q] == ',') { q = skipBlanks(l, q + 1); continue; }
                break;
            }
            bool assign = q < l.size() && ((l[q] == '=' && (q + 1 >= l.size() || l[q + 1] != '=')) ||
                                           (l[q] == ':' && names.size() == 1));
            if (assign)
                for (auto &n : names) out.push_back({SymKind::Global, n, (int)li + 1, 0, ""});
        }
    }
    return out;
}

std::vector<Symbol> luaOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskLua(text));
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        size_t p = skipBlanks(l, 0);
        if (startsWord(l, p, "local")) p = skipBlanks(l, p + 5);
        if (startsWord(l, p, "function")) {
            size_t q = skipBlanks(l, p + 8);
            size_t e = q;
            while (e < l.size() && (identChar(l[e]) || l[e] == '.' || l[e] == ':')) e++;
            if (e > q) {
                size_t open = l.find('(', e);
                out.push_back({SymKind::Function, l.substr(q, e - q), (int)li + 1, 0,
                               open == std::string::npos ? "" : paramList(ls, li, open)});
            }
            continue;
        }
        // name = function(...)
        size_t e = p;
        while (e < l.size() && (identChar(l[e]) || l[e] == '.' || l[e] == ':')) e++;
        if (e > p) {
            size_t q = skipBlanks(l, e);
            if (q < l.size() && l[q] == '=' && (q + 1 >= l.size() || l[q + 1] != '=')) {
                size_t f = skipBlanks(l, q + 1);
                if (startsWord(l, f, "function")) {
                    size_t open = l.find('(', f);
                    out.push_back({SymKind::Function, l.substr(p, e - p), (int)li + 1, 0,
                                   open == std::string::npos ? "" : paramList(ls, li, open)});
                }
            }
        }
    }
    return out;
}

std::vector<Symbol> perlOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskHash(text, Language::Perl));
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        size_t p = skipBlanks(l, 0);
        if (startsWord(l, p, "sub")) {
            size_t q = skipBlanks(l, p + 3);
            size_t e = q;
            while (e < l.size() && (identChar(l[e]) || l[e] == ':')) e++;
            if (e > q) out.push_back({SymKind::Function, l.substr(q, e - q), (int)li + 1, 0, ""});
        } else if (startsWord(l, p, "package")) {
            size_t q = skipBlanks(l, p + 7);
            size_t e = q;
            while (e < l.size() && (identChar(l[e]) || l[e] == ':')) e++;
            if (e > q) out.push_back({SymKind::Package, l.substr(q, e - q), (int)li + 1, 0, ""});
        }
    }
    return out;
}

std::vector<Symbol> shellOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskHash(text, Language::Shell));
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        size_t p = skipBlanks(l, 0);
        if (startsWord(l, p, "function")) {
            size_t q = skipBlanks(l, p + 8);
            size_t e = q;
            while (e < l.size() && (identChar(l[e]) || l[e] == '-' || l[e] == '.')) e++;
            if (e > q) out.push_back({SymKind::Function, l.substr(q, e - q), (int)li + 1, 0, ""});
            continue;
        }
        size_t e = p;
        while (e < l.size() && (identChar(l[e]) || l[e] == '-' || l[e] == '.')) e++;
        if (e > p) {
            size_t q = skipBlanks(l, e);
            if (l.compare(q, 1, "(") == 0) {
                size_t r = skipBlanks(l, q + 1);
                if (l.compare(r, 1, ")") == 0) out.push_back({SymKind::Function, l.substr(p, e - p), (int)li + 1, 0, ""});
            }
        }
    }
    return out;
}

std::vector<Symbol> arexxOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskARexx(text));
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        size_t p = skipBlanks(l, 0);
        size_t e = p;
        while (e < l.size() && (rexxIdentChar(l[e]) || l[e] == '.')) e++;
        if (e == p || std::isdigit((unsigned char)l[p])) continue;
        size_t q = skipBlanks(l, e);
        if (q < l.size() && l[q] == ':') {
            std::string rest = lowerStr(l.substr(q + 1));
            size_t r = skipBlanks(rest, 0);
            bool proc = startsWord(rest, r, "procedure");
            out.push_back({proc ? SymKind::Function : SymKind::Label, l.substr(p, e - p), (int)li + 1, 0, ""});
        }
    }
    return out;
}

std::vector<Symbol> amigaDosOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskAmigaDOS(text));
    for (size_t li = 0; li < ls.size(); li++) {
        std::string l = lowerStr(ls[li]);
        size_t p = skipBlanks(l, 0);
        if (startsWord(l, p, "lab")) {
            size_t q = skipBlanks(l, p + 3);
            size_t e = q;
            while (e < l.size() && !std::isspace((unsigned char)l[e])) e++;
            if (e > q) out.push_back({SymKind::Label, ls[li].substr(q, e - q), (int)li + 1, 0, ""});
        }
    }
    return out;
}

std::vector<Symbol> markdownOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(text);
    bool fence = false;
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        if (l.compare(0, 3, "```") == 0 || l.compare(0, 3, "~~~") == 0) { fence = !fence; continue; }
        if (fence || l.empty() || l[0] != '#') continue;
        size_t level = 0;
        while (level < l.size() && l[level] == '#') level++;
        if (level > 6 || (level < l.size() && l[level] != ' ' && l[level] != '\t')) continue;
        std::string title = l.substr(level);
        size_t a = title.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        title = title.substr(a);
        while (!title.empty() && (title.back() == '#' || title.back() == ' ')) title.pop_back();
        out.push_back({SymKind::Heading, title, (int)li + 1, (int)level - 1, ""});
    }
    return out;
}

std::vector<Symbol> yamlOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = lines(maskHash(text, Language::YAML));
    for (size_t li = 0; li < ls.size(); li++) {
        const std::string &l = ls[li];
        if (l.empty() || l[0] == ' ' || l[0] == '\t' || l[0] == '-' || l[0] == '#') continue;
        size_t c = l.find(':');
        if (c == std::string::npos || c == 0) continue;
        if (c + 1 < l.size() && l[c + 1] != ' ' && l[c + 1] != '\t') continue;   // "http://..."
        std::string key = l.substr(0, c);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        if (key.size() >= 2 && (key[0] == '"' || key[0] == '\'')) key = key.substr(1, key.size() - 2);
        out.push_back({SymKind::Key, key, (int)li + 1, 0, ""});
    }
    return out;
}

std::vector<Symbol> jsonOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::string m = maskJSON(text);
    int depth = 0, line = 1;
    for (size_t i = 0; i < m.size(); i++) {
        char c = m[i];
        if (c == '\n') line++;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (c == '"') {
            size_t e = i + 1;
            while (e < m.size() && m[e] != '"' && m[e] != '\n') {
                if (m[e] == '\\') e++;
                e++;
            }
            std::string s = m.substr(i + 1, e - i - 1);
            size_t q = e + 1;
            while (q < m.size() && (m[q] == ' ' || m[q] == '\t' || m[q] == '\r' || m[q] == '\n')) q++;
            if (depth == 1 && q < m.size() && m[q] == ':') out.push_back({SymKind::Key, s, line, 0, ""});
            i = e;
        }
    }
    return out;
}

} // namespace

bool hasOutline(Language lang)
{
    switch (lang) {
    case Language::C: case Language::Asm: case Language::Python: case Language::Lua: case Language::Perl:
    case Language::Shell: case Language::ARexx: case Language::AmigaDOS: case Language::Markdown:
    case Language::YAML: case Language::JSON:
        return true;
    default:
        return false;
    }
}

std::vector<Symbol> outlineSymbols(const std::string &text, Language lang)
{
    switch (lang) {
    case Language::C: return outlineSymbols(text, false);
    case Language::Asm: return outlineSymbols(text, true);
    case Language::Python: return pythonOutline(text);
    case Language::Lua: return luaOutline(text);
    case Language::Perl: return perlOutline(text);
    case Language::Shell: return shellOutline(text);
    case Language::ARexx: return arexxOutline(text);
    case Language::AmigaDOS: return amigaDosOutline(text);
    case Language::Markdown: return markdownOutline(text);
    case Language::YAML: return yamlOutline(text);
    case Language::JSON: return jsonOutline(text);
    default: return {};
    }
}

std::string maskCode(const std::string &text, Language lang)
{
    switch (lang) {
    case Language::C: return maskCode(text, false);
    case Language::Asm: return maskCode(text, true);
    case Language::Python: return maskPython(text);
    case Language::Lua: return maskLua(text);
    case Language::Perl: case Language::Shell: case Language::YAML: case Language::Make: case Language::Props:
        return maskHash(text, lang);
    case Language::ARexx: return maskARexx(text);
    case Language::AmigaDOS: return maskAmigaDOS(text);
    case Language::JSON: return maskJSON(text);
    default: return text;
    }
}

bool isReservedWord(const std::string &s, Language lang)
{
    if (lang == Language::C || lang == Language::Asm) return isReservedWord(s);
    std::string kw = std::string(" ") + languageKeywords(lang, 0) + " ";
    if (lang == Language::ARexx || lang == Language::AmigaDOS) {
        kw = lowerStr(kw);
        return kw.find(" " + lowerStr(s) + " ") != std::string::npos;
    }
    return kw.find(" " + s + " ") != std::string::npos;
}

std::vector<size_t> findIdentifier(const std::string &text, const std::string &name, Language lang)
{
    if (lang == Language::C || lang == Language::Asm) return findIdentifier(text, name, lang == Language::Asm);
    std::vector<size_t> out;
    if (name.empty()) return out;
    const bool rexx = lang == Language::ARexx;
    const bool nocase = rexx || lang == Language::AmigaDOS;
    std::string m = maskCode(text, lang);
    std::string needle = name;
    if (nocase) { m = lowerStr(m); needle = lowerStr(name); }
    auto ic = rexx ? rexxIdentChar : identChar;
    size_t p = 0;
    while ((p = m.find(needle, p)) != std::string::npos) {
        size_t e = p + needle.size();
        bool before = p == 0 || !ic(m[p - 1]);
        bool after = e >= m.size() || !ic(m[e]);
        if (before && after) out.push_back(p);
        p = e;
    }
    return out;
}

std::string replaceIdentifier(const std::string &text, const std::string &name, const std::string &newName,
                              Language lang, int *count)
{
    std::vector<size_t> hits = findIdentifier(text, name, lang);
    if (count) *count = (int)hits.size();
    std::string out;
    size_t last = 0;
    for (size_t h : hits) {
        out.append(text, last, h - last);
        out += newName;
        last = h + name.size();
    }
    out.append(text, last, std::string::npos);
    return out;
}

} // namespace af
