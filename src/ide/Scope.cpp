// Scope-aware completion: what's inside a namespace/class/struct ("os::", "Foo::"), and the
// members of a variable's type ("win.", "win->"). Declarations are found by a light scan of
// C/C++ text — the current document, the project's files and the headers they include (the
// SDK's too) — not by a compiler, so it is a good guess rather than an exact answer.
#include "App.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <set>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static bool identStart(int c) { return isalpha(c) || c == '_'; }
static bool identChar(int c) { return isalnum(c) || c == '_'; }

static const char *keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
    "float", "for", "goto", "if", "inline", "int", "long", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "bool", "class",
    "namespace", "template", "typename", "public", "private", "protected", "virtual", "override", "explicit",
    "operator", "friend", "using", "delete", "new", "this", "const_cast", "static_cast", "noexcept", "constexpr",
    "final", "mutable", "nullptr", "true", "false", "default", "throw", "try", "catch", nullptr
};
static bool isKeyword(const std::string &w)
{
    for (int i = 0; keywords[i]; i++) if (w == keywords[i]) return true;
    return false;
}

static const char *stdNames =
    "vector string map multimap set multiset unordered_map unordered_set list deque array pair tuple "
    "unique_ptr shared_ptr weak_ptr make_unique make_shared move forward swap min max sort stable_sort find "
    "find_if count count_if for_each transform copy fill reverse accumulate begin end size printf snprintf "
    "sprintf fprintf puts fputs fopen fclose fread fwrite fgets strlen strcmp strncmp strcpy strncpy strcat "
    "memcpy memmove memset malloc calloc realloc free abs rand srand atoi atol strtol strtoul to_string stoi "
    "stol stod getline cout cin cerr endl ostream istream stringstream ostringstream istringstream function "
    "bind thread mutex lock_guard unique_lock atomic exception runtime_error logic_error out_of_range "
    "invalid_argument numeric_limits initializer_list optional variant any nullptr_t size_t ptrdiff_t "
    "uint8_t uint16_t uint32_t int8_t int16_t int32_t int64_t uint64_t sqrt pow sin cos tan atan2 floor ceil";
static const char *vectorMembers =
    "push_back emplace_back pop_back size empty clear resize reserve capacity at front back data begin end "
    "rbegin rend insert erase swap assign shrink_to_fit";
static const char *stringMembers =
    "c_str data size length empty clear append push_back pop_back substr find rfind find_first_of "
    "find_last_of find_first_not_of compare replace insert erase begin end front back at resize reserve "
    "capacity swap";
static const char *mapMembers = "find count insert emplace erase clear size empty begin end at operator[] lower_bound upper_bound";
static const char *ptrMembers = "get reset release swap";

static void addWords(std::set<std::string> &out, const char *list)
{
    std::string w;
    for (const char *p = list;; p++) {
        if (*p == ' ' || !*p) {
            if (!w.empty()) out.insert(w);
            w.clear();
            if (!*p) break;
        } else {
            w += *p;
        }
    }
}

// Comments, strings and character literals blanked out (same length), so scans only see code.
static std::string codeOnly(const std::string &t)
{
    std::string s = t;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') s[i++] = ' ';
        } else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
            while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/')) { if (s[i] != '\n') s[i] = ' '; i++; }
            if (i < n) { s[i] = s[i + 1] = ' '; i += 2; }
        } else if (s[i] == '"' || s[i] == '\'') {
            char q = s[i++];
            while (i < n && s[i] != q && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n) s[i++] = ' '; s[i++] = ' '; }
            i++;
        } else if (s[i] == '#') {
            // preprocessor lines: keep #define names (they are members of nothing), drop the rest
            size_t e = s.find('\n', i);
            if (s.compare(i, 7, "#define") != 0) while (i < n && i < e) s[i++] = ' ';
            else i = e == std::string::npos ? n : e;
        } else {
            i++;
        }
    }
    return s;
}

// --- the source texts to look in ---

const std::string &App::codeText(const std::string &path)
{
    int64_t t = fileTime(path);
    auto &c = codeCache[path];
    if (c.first != t || t < 0) {
        std::string raw;
        c.second = readFile(path, raw) ? codeOnly(raw) : std::string();
        c.first = t;
    }
    return c.second;
}

// The current document, the project's files, and (transitively) the headers they include.
std::vector<std::string> App::scopeSources()
{
    std::vector<std::string> files;
    std::set<std::string> seen;
    auto add = [&](const std::string &p) {
        std::string key = p;
        for (auto &c : key) c = tolower((unsigned char)c);
        if (seen.insert(key).second) files.push_back(p);
    };
    if (cur && !cur->path.empty()) add(cur->path);
    for (auto &f : project.files) add(project.abs(f));
    // follow #includes (raw text: the include lines themselves are blanked in codeText)
    for (size_t i = 0; i < files.size() && files.size() < 250; i++) {
        std::string raw;
        if (!readFile(files[i], raw)) continue;
        size_t pos = 0;
        while ((pos = raw.find("#include", pos)) != std::string::npos) {
            size_t e = raw.find('\n', pos);
            std::string line = raw.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos += 8;
            size_t a = line.find_first_of("<\"");
            if (a == std::string::npos) continue;
            size_t b = line.find(line[a] == '<' ? '>' : '"', a + 1);
            if (b == std::string::npos) continue;
            std::string p = resolveInclude(line.substr(a + 1, b - a - 1), line[a] == '"', files[i]);
            if (!p.empty()) add(p);
        }
    }
    return files;
}

// --- members of a block ---

// The body of "namespace|class|struct|union|enum NAME ... {" ... "}" in text, from `from`.
static bool findBlock(const std::string &t, const std::string &name, size_t &from, size_t &bodyStart, size_t &bodyEnd)
{
    size_t p = from;
    while ((p = t.find(name, p)) != std::string::npos) {
        size_t e = p + name.size();
        bool whole = (p == 0 || !identChar((unsigned char)t[p - 1])) && (e >= t.size() || !identChar((unsigned char)t[e]));
        if (!whole) { p = e; continue; }
        // the keyword before
        size_t k = p;
        while (k > 0 && isspace((unsigned char)t[k - 1])) k--;
        size_t ks = k;
        while (ks > 0 && identChar((unsigned char)t[ks - 1])) ks--;
        std::string kw = t.substr(ks, k - ks);
        if (kw != "namespace" && kw != "class" && kw != "struct" && kw != "union" && kw != "enum") { p = e; continue; }
        // then "{" (after an optional ": base" list), not ";" (a forward declaration)
        size_t q = e;
        while (q < t.size() && t[q] != '{' && t[q] != ';' && t[q] != '(' && t[q] != ')') q++;
        if (q >= t.size() || t[q] != '{') { p = e; continue; }
        int depth = 0;
        size_t r = q;
        for (; r < t.size(); r++) {
            if (t[r] == '{') depth++;
            else if (t[r] == '}' && --depth == 0) break;
        }
        bodyStart = q + 1;
        bodyEnd = r;
        from = r;
        return true;
    }
    return false;
}

// Names declared at the top level of a body: "int x, y;" -> x y, "void f(...)" -> f,
// "class C {" -> C, enum constants, nested type names.
static void bodyMembers(const std::string &t, size_t a, size_t b, std::set<std::string> &out)
{
    int brace = 0, paren = 0;
    std::string prev, prevPrev;
    bool inEnum = false, qualified = false, initList = false;
    int enumDepth = -1;
    char lastPunct = 0;
    for (size_t i = a; i < b;) {
        char c = t[i];
        if (identStart((unsigned char)c)) {
            size_t s = i;
            while (i < b && identChar((unsigned char)t[i])) i++;
            std::string w = t.substr(s, i - s);
            size_t j = i;
            while (j < b && isspace((unsigned char)t[j])) j++;
            char next = j < b ? t[j] : 0;
            bool next2colon = next == ':' && j + 1 < b && t[j + 1] == ':';
            // "Class::method" (a definition outside its class) and ") : member(x)" (an
            // initialiser list) don't declare anything here
            bool skip = qualified || initList;
            qualified = next2colon;
            if (next2colon) i = j + 2;
            lastPunct = 0;
            if (!skip && paren == 0 && !isKeyword(w) && !next2colon) {
                if (brace == 0 && (next == '(' || next == ';' || next == '=' || next == ',' || next == '[' ||
                                   next == '{' || (next == ':' && prev != "public" && prev != "private" && prev != "protected")))
                    out.insert(w);
                else if (inEnum && brace == enumDepth && (next == '=' || next == ',' || next == '}'))
                    out.insert(w);
            }
            if (w == "enum") { inEnum = true; enumDepth = brace + 1; }
            prevPrev = prev;
            prev = w;
            continue;
        }
        if (c == '{') { brace++; initList = false; }
        else if (c == '}') { if (inEnum && brace == enumDepth) inEnum = false; brace--; }
        else if (c == '(') paren++;
        else if (c == ')') paren--;
        else if (c == ';' && brace == 0 && paren == 0) { prev.clear(); initList = false; }
        else if (c == ':' && brace == 0 && paren == 0 && lastPunct == ')') initList = true;
        if (!isspace((unsigned char)c)) { lastPunct = c; qualified = false; }
        i++;
    }
    for (const char *acc : { "public", "private", "protected" }) out.erase(acc);
}

void App::scopeMembers(const std::string &scopeIn, std::set<std::string> &out)
{
    std::string scope = scopeIn;
    size_t cc = scope.rfind("::");
    if (cc != std::string::npos) scope = scope.substr(cc + 2);   // "a::b" -> look inside b
    if (scope == "std") { addWords(out, stdNames); return; }
    for (auto &f : scopeSources()) {
        const std::string &t = codeText(f);
        if (t.find(scope) == std::string::npos) continue;
        size_t from = 0, a, b;
        while (findBlock(t, scope, from, a, b)) bodyMembers(t, a, b, out);
        // methods defined outside the class: "Scope::name"
        std::string q = scope + "::";
        size_t p = 0;
        while ((p = t.find(q, p)) != std::string::npos) {
            bool whole = p == 0 || !identChar((unsigned char)t[p - 1]);
            p += q.size();
            if (!whole) continue;
            size_t s = p;
            while (p < t.size() && (identChar((unsigned char)t[p]) || t[p] == '~')) p++;
            if (p > s && t[s] != '~') out.insert(t.substr(s, p - s));
        }
    }
}

// The declared type of `var`, looking back from `line` in the editor, then in the project's
// files (class members). "os::AppWindow win(...)" -> "os::AppWindow", "struct Window *w" -> "Window".
std::string App::typeOfVariable(const std::string &var, intptr_t line)
{
    auto typeIn = [&](const std::string &l) -> std::string {
        size_t p = 0;
        while ((p = l.find(var, p)) != std::string::npos) {
            size_t e = p + var.size();
            bool whole = (p == 0 || !identChar((unsigned char)l[p - 1])) && (e >= l.size() || !identChar((unsigned char)l[e]));
            if (whole) {
                size_t a = e;
                while (a < l.size() && l[a] == ' ') a++;
                if (a >= l.size() || strchr("=(;,[):{", l[a])) {
                    size_t b = p;
                    while (b > 0 && (l[b - 1] == ' ' || l[b - 1] == '*' || l[b - 1] == '&')) b--;
                    size_t te = b;
                    int angle = 0;
                    while (b > 0) {
                        char c = l[b - 1];
                        if (c == '>') angle++;
                        else if (c == '<') angle--;
                        else if (!(identChar((unsigned char)c) || c == ':' || (angle > 0 && (c == ',' || c == ' ' || c == '*'))))
                            break;
                        b--;
                    }
                    std::string type = l.substr(b, te - b);
                    if (!type.empty() && identStart((unsigned char)type[0]) && !isKeyword(type) && te < p &&
                        (b == 0 || (l[b - 1] != '.' && l[b - 1] != '>' && l[b - 1] != '-')))
                        return type;
                    // "struct Window *win": skip the struct/class word
                    if (type == "struct" || type == "class") return "";
                }
            }
            p = e;
        }
        return "";
    };
    if (editor) {
        for (intptr_t l = line; l >= 0 && l > line - 600; l--) {
            intptr_t a = S(editor, SCI_POSITIONFROMLINE, l), b = S(editor, SCI_GETLINEENDPOSITION, l);
            if (b <= a) continue;
            std::vector<char> buf(b - a + 1);
            struct Sci_TextRangeFull tr = { { a, b }, buf.data() };
            S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
            std::string text = codeOnly(buf.data());
            if (text.find(var) == std::string::npos) continue;
            std::string t = typeIn(text);
            if (!t.empty()) return t;
        }
    }
    // the headers this file includes (its own class's members are usually there), then the
    // rest of the project
    std::vector<std::string> order;
    if (cur && !cur->path.empty()) {
        std::string raw;
        if (readFile(cur->path, raw)) {
            size_t pos = 0;
            while ((pos = raw.find("#include \"", pos)) != std::string::npos) {
                size_t a = pos + 10, b = raw.find('"', a);
                pos = a;
                if (b == std::string::npos) break;
                std::string p = resolveInclude(raw.substr(a, b - a), true, cur->path);
                if (!p.empty()) order.push_back(p);
            }
        }
    }
    for (auto &f : project.files) {
        std::string a = project.abs(f);
        if (!(cur && sameFile(a, cur->path))) order.push_back(a);
    }
    for (auto &f : order) {
        const std::string &t = codeText(f);
        size_t pos = 0;
        while ((pos = t.find(var, pos)) != std::string::npos) {
            size_t ls = t.rfind('\n', pos);
            ls = ls == std::string::npos ? 0 : ls + 1;
            size_t le = t.find('\n', pos);
            std::string ty = typeIn(t.substr(ls, le == std::string::npos ? std::string::npos : le - ls));
            if (!ty.empty()) return ty;
            pos += var.size();
        }
    }
    return "";
}

void App::typeMembers(const std::string &typeIn, std::set<std::string> &out)
{
    std::string type = typeIn;
    for (const char *pfx : { "const ", "struct ", "class ", "union " })
        if (type.compare(0, strlen(pfx), pfx) == 0) type = type.substr(strlen(pfx));
    if (type.compare(0, 11, "std::vector") == 0 || type.compare(0, 10, "std::deque") == 0 ||
        type.compare(0, 10, "std::array") == 0 || type.compare(0, 9, "std::list") == 0)
        addWords(out, vectorMembers);
    else if (type == "std::string" || type == "string")
        addWords(out, stringMembers);
    else if (type.compare(0, 8, "std::map") == 0 || type.compare(0, 18, "std::unordered_map") == 0 ||
             type.compare(0, 8, "std::set") == 0)
        addWords(out, mapMembers);
    else if (type.find("_ptr<") != std::string::npos)
        addWords(out, ptrMembers);
    else {
        size_t lt = type.find('<');
        if (lt != std::string::npos) type = type.substr(0, lt);
        scopeMembers(type, out);
    }
}

// The scope before the word at `start`: "os::" -> ("os", ::), "win." / "win->" -> (type, member).
// Returns 0 none, 1 qualifier, 2 member access; name is the qualifier or variable.
int App::scopeAt(intptr_t start, std::string &name)
{
    if (!editor || start < 1) return 0;
    char c1 = (char)S(editor, SCI_GETCHARAT, start - 1), c2 = start >= 2 ? (char)S(editor, SCI_GETCHARAT, start - 2) : 0;
    intptr_t end;
    int kind;
    if (c1 == ':' && c2 == ':') { end = start - 2; kind = 1; }
    else if (c1 == '.') { end = start - 1; kind = 2; }
    else if (c1 == '>' && c2 == '-') { end = start - 2; kind = 2; }
    else return 0;
    // the identifier (or a::b qualifier) before it
    intptr_t s = end;
    while (s > 0) {
        char c = (char)S(editor, SCI_GETCHARAT, s - 1);
        if (identChar((unsigned char)c) || (kind == 1 && c == ':')) s--;
        else break;
    }
    if (s >= end) return 0;
    std::vector<char> buf(end - s + 1);
    struct Sci_TextRangeFull tr = { { s, end }, buf.data() };
    S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    name = buf.data();
    while (!name.empty() && name[0] == ':') name.erase(0, 1);
    if (name.empty() || isdigit((unsigned char)name[0])) return 0;   // "1.5"
    return kind;
}

} // namespace af
