// Outline parsing and identifier search for C, C++ and GNU assembler sources.
#include "Symbols.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace af {

const char *symKindName(SymKind k)
{
    switch (k) {
    case SymKind::Function: return "function";
    case SymKind::Struct: return "struct";
    case SymKind::Class: return "class";
    case SymKind::Union: return "union";
    case SymKind::Enum: return "enum";
    case SymKind::Namespace: return "namespace";
    case SymKind::Typedef: return "typedef";
    case SymKind::Define: return "#define";
    case SymKind::Global: return "variable";
    case SymKind::Label: return "label";
    case SymKind::Macro: return "macro";
    // --- lang ---
    case SymKind::Method: return "method";
    case SymKind::Heading: return "heading";
    case SymKind::Key: return "key";
    case SymKind::Package: return "package";
    // --- end lang ---
    }
    return "";
}

static bool identStart(char c) { return isalpha((unsigned char)c) || c == '_'; }
static bool identChar(char c) { return isalnum((unsigned char)c) || c == '_'; }
static bool asmIdentChar(char c) { return identChar(c) || c == '.' || c == '$'; }

bool isIdentifierName(const std::string &s)
{
    if (s.empty() || !identStart(s[0])) return false;
    for (char c : s)
        if (!identChar(c)) return false;
    return true;
}

static const std::set<std::string> &reserved()
{
    static const std::set<std::string> words = {
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const",
        "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend",
        "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr",
        "operator", "private", "protected", "public", "register", "reinterpret_cast", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template",
        "this", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "while", "restrict", "_Bool", "override", "final",
    };
    return words;
}

bool isReservedWord(const std::string &s) { return reserved().count(s) != 0; }

// ---------------------------------------------------------------------------------------
// masking
// ---------------------------------------------------------------------------------------

static const char *const cppDirectives[] = { "include", "define", "undef", "if", "ifdef", "ifndef", "elif",
                                              "else", "endif", "error", "warning", "pragma", "line", nullptr };

// Is text[p] the '#' of a preprocessor directive (first thing on its line)?
static bool directiveAt(const std::string &t, size_t p, std::string *name)
{
    size_t ls = p;
    while (ls > 0 && (t[ls - 1] == ' ' || t[ls - 1] == '\t')) ls--;
    if (ls > 0 && t[ls - 1] != '\n') return false;
    size_t q = p + 1;
    while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) q++;
    size_t e = q;
    while (e < t.size() && identChar(t[e])) e++;
    std::string w = t.substr(q, e - q);
    for (int i = 0; cppDirectives[i]; i++)
        if (w == cppDirectives[i]) { if (name) *name = w; return true; }
    return false;
}

std::string maskCode(const std::string &t, bool assembler)
{
    std::string m = t;
    size_t n = t.size(), i = 0;
    auto blank = [&](size_t a, size_t b) {
        for (size_t k = a; k < b && k < n; k++)
            if (m[k] != '\n') m[k] = ' ';
    };
    while (i < n) {
        char c = t[i];
        if (c == '/' && i + 1 < n && t[i + 1] == '/' && !assembler) {
            size_t e = t.find('\n', i);
            if (e == std::string::npos) e = n;
            blank(i, e);
            i = e;
        } else if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            size_t e = t.find("*/", i + 2);
            e = e == std::string::npos ? n : e + 2;
            blank(i, e);
            i = e;
        } else if (c == '#') {
            std::string dir;
            if (directiveAt(t, i, &dir)) {
                if (dir == "include") {
                    // the file name isn't code
                    size_t e = t.find('\n', i);
                    if (e == std::string::npos) e = n;
                    size_t a = t.find_first_of("<\"", i);
                    if (a != std::string::npos && a < e) blank(a, e);
                    i = e;
                } else {
                    i++;
                }
            } else if (assembler) {
                size_t e = t.find('\n', i);   // a gas comment on PowerPC
                if (e == std::string::npos) e = n;
                blank(i, e);
                i = e;
            } else {
                i++;
            }
        } else if (c == '"' || (c == '\'' && !(assembler && i > 0 && identChar(t[i - 1])))) {
            // C++11 raw string R"delim( ... )delim"
            if (c == '"' && !assembler && i > 0 && t[i - 1] == 'R' && (i < 2 || !identChar(t[i - 2]))) {
                size_t open = t.find('(', i);
                if (open != std::string::npos && open - i <= 17) {
                    std::string close = ")" + t.substr(i + 1, open - i - 1) + "\"";
                    size_t e = t.find(close, open);
                    e = e == std::string::npos ? n : e + close.size();
                    blank(i, e);
                    i = e;
                    continue;
                }
            }
            size_t e = i + 1;
            while (e < n && t[e] != c && t[e] != '\n') {
                if (t[e] == '\\' && e + 1 < n) e++;
                e++;
            }
            if (e < n && t[e] == c) e++;
            blank(i, e);
            i = e;
        } else {
            i++;
        }
    }
    return m;
}

// ---------------------------------------------------------------------------------------
// identifier search
// ---------------------------------------------------------------------------------------

std::vector<size_t> findIdentifier(const std::string &text, const std::string &name, bool assembler)
{
    std::vector<size_t> out;
    if (name.empty()) return out;
    std::string m = maskCode(text, assembler);
    auto ic = assembler ? asmIdentChar : identChar;
    size_t p = 0;
    while ((p = m.find(name, p)) != std::string::npos) {
        size_t e = p + name.size();
        bool before = p == 0 || !ic(m[p - 1]);
        bool after = e >= m.size() || !ic(m[e]);
        if (before && after) out.push_back(p);
        p = e;
    }
    return out;
}

std::string replaceIdentifier(const std::string &text, const std::string &name, const std::string &newName,
                              bool assembler, int *count)
{
    std::vector<size_t> hits = findIdentifier(text, name, assembler);
    if (count) *count = (int)hits.size();
    std::string out;
    out.reserve(text.size() + hits.size() * (newName.size() + 1));
    size_t last = 0;
    for (size_t h : hits) {
        out.append(text, last, h - last);
        out += newName;
        last = h + name.size();
    }
    out.append(text, last, std::string::npos);
    return out;
}

void lineColumn(const std::string &text, size_t offset, int &line, int &col)
{
    line = 1;
    size_t ls = 0;
    for (size_t i = 0; i < offset && i < text.size(); i++)
        if (text[i] == '\n') { line++; ls = i + 1; }
    col = (int)(offset - ls) + 1;
}

// ---------------------------------------------------------------------------------------
// C / C++ outline
// ---------------------------------------------------------------------------------------

namespace {

struct Tok {
    std::string s;   // identifier, number, or one punctuation character ("::" and "->" joined)
    int line;
    size_t pos;
    bool ident;
};

static std::string squeezeText(const std::string &s)
{
    std::string o;
    bool sp = false;
    for (char c : s) {
        if (isspace((unsigned char)c)) { sp = true; continue; }
        if (sp && !o.empty()) o += ' ';
        sp = false;
        o += c;
    }
    return o;
}

static const std::set<std::string> notFunctions = {
    "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype", "do",
    "else", "case", "__attribute__", "defined", "typeof", "__typeof__", "static_assert", "new", "delete",
};

enum class Scope { File, Namespace, Transparent, Type, Function, Other };

struct ScopeEntry {
    Scope kind;
    std::string name;   // for Type/Namespace: qualifies the members
    int depth;          // outline depth of the members
    std::vector<Tok> stmt;   // tokens of the statement being read at this level
};

class CParser {
public:
    CParser(const std::string &text) : src(text), masked(maskCode(text, false)) {}

    std::vector<Symbol> run()
    {
        directives();
        tokenize();
        scopes.push_back({Scope::File, "", 0, {}});
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok &t = toks[i];
            ScopeEntry &top = scopes.back();
            if (t.s == "{") {
                open(top);
            } else if (t.s == "}") {
                if (scopes.size() > 1) {
                    ScopeEntry closed = scopes.back();
                    scopes.pop_back();
                    // `struct X {...} var;` / `typedef struct {...} Name;` continue the statement
                    if (closed.kind == Scope::Type || closed.kind == Scope::Other)
                        scopes.back().stmt.push_back({"{}", t.line, t.pos, false});
                    else
                        scopes.back().stmt.clear();
                }
            } else if (t.s == ";") {
                statement(top);
                top.stmt.clear();
            } else if (top.kind != Scope::Function && top.kind != Scope::Other) {
                // access specifiers end a statement in a class body
                if (t.s == ":" && top.kind == Scope::Type && top.stmt.size() == 1 &&
                    (top.stmt[0].s == "public" || top.stmt[0].s == "private" || top.stmt[0].s == "protected")) {
                    top.stmt.clear();
                    continue;
                }
                top.stmt.push_back(t);
            }
        }
        std::stable_sort(syms.begin(), syms.end(), [](const Symbol &a, const Symbol &b) { return a.line < b.line; });
        return syms;
    }

private:
    const std::string &src;
    std::string masked;
    std::vector<Tok> toks;
    std::vector<ScopeEntry> scopes;
    std::vector<Symbol> syms;

    // #define NAME; the directive lines are blanked out of `masked` afterwards
    void directives()
    {
        size_t p = 0;
        int line = 1;
        while (p < masked.size()) {
            size_t e = masked.find('\n', p);
            if (e == std::string::npos) e = masked.size();
            size_t q = p;
            while (q < e && (masked[q] == ' ' || masked[q] == '\t')) q++;
            if (q < e && masked[q] == '#') {
                // the directive and its continuation lines
                size_t end = e;
                int lines = 0;
                while (end > p && masked[end - 1] == '\\' && end < masked.size()) {
                    size_t n2 = masked.find('\n', end + 1);
                    end = n2 == std::string::npos ? masked.size() : n2;
                    lines++;
                }
                std::string d = masked.substr(q + 1, end - q - 1);
                size_t a = d.find_first_not_of(" \t");
                if (a != std::string::npos && d.compare(a, 6, "define") == 0) {
                    size_t b = d.find_first_not_of(" \t", a + 6);
                    size_t c = b;
                    while (c < d.size() && identChar(d[c])) c++;
                    if (b != std::string::npos && c > b) {
                        Symbol s{SymKind::Define, d.substr(b, c - b), line, (int)scopes.size() > 1 ? 1 : 0, ""};
                        if (c < d.size() && d[c] == '(') {
                            size_t r = d.find(')', c);
                            if (r != std::string::npos) s.detail = d.substr(c, r - c + 1);
                        }
                        syms.push_back(s);
                    }
                }
                for (size_t k = p; k < end; k++)
                    if (masked[k] != '\n') masked[k] = ' ';
                line += lines;
                p = end;
                continue;
            }
            p = e + 1;
            line++;
        }
    }

    void tokenize()
    {
        int line = 1;
        size_t n = masked.size();
        for (size_t i = 0; i < n;) {
            char c = masked[i];
            if (c == '\n') { line++; i++; continue; }
            if (isspace((unsigned char)c)) { i++; continue; }
            if (identStart(c)) {
                size_t e = i;
                while (e < n && identChar(masked[e])) e++;
                toks.push_back({masked.substr(i, e - i), line, i, true});
                i = e;
            } else if (isdigit((unsigned char)c)) {
                size_t e = i;
                while (e < n && (isalnum((unsigned char)masked[e]) || masked[e] == '.' || masked[e] == '\'')) e++;
                toks.push_back({masked.substr(i, e - i), line, i, false});
                i = e;
            } else if (c == ':' && i + 1 < n && masked[i + 1] == ':') {
                toks.push_back({"::", line, i, false});
                i += 2;
            } else if (c == '-' && i + 1 < n && masked[i + 1] == '>') {
                toks.push_back({"->", line, i, false});
                i += 2;
            } else {
                toks.push_back({std::string(1, c), line, i, false});
                i++;
            }
        }
    }

    std::string qualify(const std::string &name) const
    {
        std::string q;
        for (auto &s : scopes)
            if (s.kind == Scope::Type && !s.name.empty()) q += s.name + "::";
        return q + name;
    }

    int depth() const { return scopes.back().depth; }

    // A '{': what does it open?
    void open(ScopeEntry &top)
    {
        std::vector<Tok> st = top.stmt;
        top.stmt.clear();
        int d = top.depth;
        if (top.kind == Scope::Function || top.kind == Scope::Other) {
            scopes.push_back({Scope::Other, "", d, {}});
            return;
        }
        // drop leading "template <...>"
        size_t b = 0;
        if (!st.empty() && st[0].s == "template") {
            int angle = 0;
            for (b = 1; b < st.size(); b++) {
                if (st[b].s == "<") angle++;
                else if (st[b].s == ">" && --angle == 0) { b++; break; }
            }
        }
        // extern "C" { ... }
        if (st.size() - b == 1 && st[b].s == "extern") {
            scopes.push_back({Scope::Transparent, "", d, {}});
            return;
        }
        if (b < st.size() && st[b].s == "namespace") {
            std::string name = b + 1 < st.size() && st[b + 1].ident ? st[b + 1].s : "";
            if (!name.empty()) syms.push_back({SymKind::Namespace, name, st[b].line, d, ""});
            scopes.push_back({Scope::Namespace, name, name.empty() ? d : d + 1, {}});
            return;
        }
        // a function body: NAME ( ... ) [qualifiers / init list] {
        int paren = 0;
        size_t firstParen = std::string::npos;
        bool sawEquals = false;
        for (size_t k = b; k < st.size(); k++) {
            if (st[k].s == "(" && paren++ == 0 && firstParen == std::string::npos) firstParen = k;
            else if (st[k].s == ")") paren--;
            else if (st[k].s == "=" && paren == 0) {
                // "operator=" is a name, not an initialiser
                if (!(k > 0 && st[k - 1].s == "operator") && !(k > 1 && st[k - 2].s == "operator")) sawEquals = true;
            }
        }
        if (firstParen != std::string::npos && firstParen > b && !sawEquals) {
            std::string name;
            size_t nk = firstParen - 1;
            if (st[nk].ident) {
                name = st[nk].s;
            } else {
                // operator==, operator() ...
                size_t k = nk;
                std::string op;
                while (k > b && !st[k].ident) op = st[k--].s + op;
                if (st[k].s == "operator") { name = "operator" + op; nk = k; }
            }
            // operator()(...): the first '(' belongs to the name
            if (name.empty() && nk >= 1 && st[nk].s == "operator") name = "operator()";
            bool isOperator = name.compare(0, 8, "operator") == 0;
            if ((!name.empty() && !notFunctions.count(name) && !isReservedWord(name)) || isOperator) {
                // qualified: A::B::name, ~Destructor
                size_t k = nk;
                if (k > b && st[k - 1].s == "~") { name = "~" + name; k--; }
                while (k >= b + 2 && st[k - 1].s == "::" && st[k - 2].ident) {
                    name = st[k - 2].s + "::" + name;
                    k -= 2;
                }
                // the parameter list
                std::string params;
                int pd = 0;
                for (size_t q = firstParen; q < st.size(); q++) {
                    if (st[q].s == "(") pd++;
                    if (st[q].s == ")" && --pd == 0) {
                        params = squeezeText(src.substr(st[firstParen].pos, st[q].pos + 1 - st[firstParen].pos));
                        break;
                    }
                }
                if (params.size() > 80) params = params.substr(0, 77) + "...";
                bool inType = top.kind == Scope::Type;
                syms.push_back({SymKind::Function, inType ? qualify(name) : name, st[nk].line, d, params});
                scopes.push_back({Scope::Function, "", d, {}});
                return;
            }
        }
        // struct / class / union / enum [NAME] [: bases] {   (not "struct X var[] = {": that's data)
        for (size_t k = b; k < st.size() && !sawEquals; k++) {
            const std::string &w = st[k].s;
            SymKind kind;
            if (w == "struct") kind = SymKind::Struct;
            else if (w == "class") kind = SymKind::Class;
            else if (w == "union") kind = SymKind::Union;
            else if (w == "enum") kind = SymKind::Enum;
            else continue;
            size_t nk = k + 1;
            if (nk < st.size() && (st[nk].s == "class" || st[nk].s == "struct")) nk++;   // enum class
            // skip __attribute__((...)) and alignas(...)
            while (nk < st.size() && (st[nk].s == "__attribute__" || st[nk].s == "alignas")) {
                int pd = 0;
                for (nk++; nk < st.size(); nk++) {
                    if (st[nk].s == "(") pd++;
                    else if (st[nk].s == ")" && --pd == 0) { nk++; break; }
                }
            }
            std::string name = nk < st.size() && st[nk].ident ? st[nk].s : "";
            bool typedefd = b < st.size() && st[b].s == "typedef";
            if (!name.empty()) syms.push_back({kind, qualify(name), st[k].line, d, ""});
            else if (typedefd) pendingTypedefKind = kind;
            scopes.push_back({kind == SymKind::Enum ? Scope::Other : Scope::Type, name, name.empty() ? d : d + 1, {}});
            // typedef struct Tag { } Name;  keep "typedef" in the parent statement
            if (typedefd) scopes[scopes.size() - 2].stmt.push_back(st[b]);
            return;
        }
        // an initialiser (int table[] = { ... };) or anything else: keep the statement going
        top.stmt = st;
        scopes.push_back({Scope::Other, "", d, {}});
    }

    SymKind pendingTypedefKind = SymKind::Typedef;

    // A ';' at file/namespace/class level: typedefs and global variables.
    void statement(ScopeEntry &top)
    {
        std::vector<Tok> &st = top.stmt;
        if (st.empty() || top.kind == Scope::Function || top.kind == Scope::Other) return;
        size_t b = 0;
        if (st[0].s == "template") return;
        if (st[0].s == "using" || st[0].s == "friend" || st[0].s == "return") return;
        if (st[0].s == "typedef") {
            // the name: the last identifier, or the one in "(*name)"
            std::string name;
            int line = st[0].line;
            for (size_t k = 0; k + 2 < st.size(); k++)
                if (st[k].s == "(" && st[k + 1].s == "*" && st[k + 2].ident) { name = st[k + 2].s; line = st[k + 2].line; break; }
            if (name.empty()) {
                // the last identifier outside brackets: typedef int Name[4];
                int bd = 0;
                for (size_t k = st.size(); k-- > 1;) {
                    if (st[k].s == "]" || st[k].s == ")") bd++;
                    else if (st[k].s == "[" || st[k].s == "(") bd--;
                    else if (bd == 0 && st[k].ident) { name = st[k].s; line = st[k].line; break; }
                }
            }
            if (!name.empty() && !isReservedWord(name)) syms.push_back({pendingTypedefKind, qualify(name), line, top.depth, ""});
            pendingTypedefKind = SymKind::Typedef;
            return;
        }
        if (top.kind == Scope::Type) return;   // members: not in the outline
        if (st[0].s == "extern" || st[0].s == "static_assert") return;
        // a function prototype? (a '(' before any '=' at depth 0, not "(*name)")
        for (size_t k = b; k < st.size(); k++) {
            if (st[k].s == "=") break;
            if (st[k].s == "(") {
                if (k + 2 < st.size() && st[k + 1].s == "*" && st[k + 2].ident) break;   // pointer to function variable
                return;
            }
        }
        // variables: an identifier followed by = [ , ; (at depth 0, outside initialisers)
        int pd = 0;
        bool inInit = false;
        for (size_t k = b; k < st.size(); k++) {
            const std::string &w = st[k].s;
            if (w == "(" || w == "[" || w == "<") pd++;
            else if ((w == ")" || w == "]" || w == ">") && pd > 0) pd--;
            if (pd == 0 && w == ",") { inInit = false; continue; }
            if (inInit) continue;
            if (pd == 0 && w == "=") { inInit = true; continue; }
            if (!st[k].ident || isReservedWord(w)) continue;
            if (k > 0 && (st[k - 1].s == "struct" || st[k - 1].s == "class" || st[k - 1].s == "union" || st[k - 1].s == "enum"))
                continue;   // the type's name (struct Foo; struct Foo *p;)
            const std::string next = k + 1 < st.size() ? st[k + 1].s : ";";
            bool fnPtr = k >= 2 && st[k - 1].s == "*" && st[k - 2].s == "(";
            if ((pd == 0 && (next == "=" || next == "[" || next == "," || next == ";")) || (fnPtr && next == ")")) {
                if (k == b) continue;   // a lone word isn't a declaration
                syms.push_back({SymKind::Global, qualify(w), st[k].line, top.depth, ""});
            }
        }
    }
};

// ---------------------------------------------------------------------------------------
// assembler outline
// ---------------------------------------------------------------------------------------

static std::vector<Symbol> asmOutline(const std::string &text)
{
    std::vector<Symbol> out;
    std::string m = maskCode(text, true);
    std::set<std::string> globals, functions;
    struct Lab { std::string name; int line; };
    std::vector<Lab> labels;
    size_t p = 0;
    int line = 1;
    while (p <= m.size()) {
        size_t e = m.find('\n', p);
        if (e == std::string::npos) e = m.size();
        std::string l = m.substr(p, e - p);
        size_t a = l.find_first_not_of(" \t");
        if (a != std::string::npos) {
            std::string rest = l.substr(a);
            auto word = [&](size_t from, size_t &end) {
                size_t s = rest.find_first_not_of(" \t", from);
                if (s == std::string::npos) { end = rest.size(); return std::string(); }
                end = s;
                while (end < rest.size() && asmIdentChar(rest[end])) end++;
                return rest.substr(s, end - s);
            };
            size_t we;
            std::string first = word(0, we);
            if (rest[0] == '#') {
                // #define in .S files (the masker leaves directives)
                size_t q = 1;
                while (q < rest.size() && (rest[q] == ' ' || rest[q] == '\t')) q++;
                if (rest.compare(q, 6, "define") == 0) {
                    size_t ne;
                    std::string name = word(q + 6, ne);
                    if (isIdentifierName(name)) out.push_back({SymKind::Define, name, line, 0, ""});
                }
            } else if (first == ".globl" || first == ".global") {
                size_t ne;
                std::string name = word(we, ne);
                globals.insert(name);
            } else if (first == ".type") {
                size_t ne;
                std::string name = word(we, ne);
                if (rest.find("function") != std::string::npos) functions.insert(name);
            } else if (first == ".macro") {
                size_t ne;
                std::string name = word(we, ne);
                if (!name.empty()) out.push_back({SymKind::Macro, name, line, 0, ""});
            } else if (first == ".equ" || first == ".set" || first == ".equiv") {
                size_t ne;
                std::string name = word(we, ne);
                if (!name.empty()) out.push_back({SymKind::Define, name, line, 0, ""});
            } else if (!first.empty() && we < rest.size() && rest[we] == ':' && !isdigit((unsigned char)first[0]) &&
                       first.compare(0, 2, ".L") != 0) {
                labels.push_back({first, line});
            }
            // NAME = value
            if (!first.empty() && first[0] != '.' && we < rest.size()) {
                size_t q = rest.find_first_not_of(" \t", we);
                if (q != std::string::npos && rest[q] == '=' && (q + 1 >= rest.size() || rest[q + 1] != '='))
                    out.push_back({SymKind::Define, first, line, 0, ""});
            }
        }
        if (e >= m.size()) break;
        p = e + 1;
        line++;
    }
    for (auto &l : labels) {
        Symbol s{functions.count(l.name) ? SymKind::Function : SymKind::Label, l.name, l.line, 0, ""};
        if (globals.count(l.name)) s.detail = "global";
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const Symbol &a, const Symbol &b) { return a.line < b.line; });
    return out;
}

} // namespace

std::vector<Symbol> outlineSymbols(const std::string &text, bool assembler)
{
    if (assembler) return asmOutline(text);
    CParser p(text);
    return p.run();
}

} // namespace af
