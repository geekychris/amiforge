// --- basic --- Boing BASIC line syntax (see BasicSyntax.h).
#include "BasicSyntax.h"

#include <cctype>
#include <cstring>

namespace af {
namespace basic {

namespace {

const char kwStatements[] =
    "addressof and as alias append base binary boolean byref byval call case class close compare "
    "const declare defbool defbyte defcur defdate defdbl defint deflng defobj defsng defstr defvar dim do "
    "each else elseif empty end enum eqv erase error event exit explicit false for friend function get "
    "global gosub goto if imp implements in input interface is let lib like line lock loop me mod "
    "new next not nothing null on open option optional or output paramarray preserve print private "
    "property public put raiseevent random read redim rem resume return seek select set shared static "
    "step stop sub then to true type typeof unlock until version wend while with withevents write xor "
    "debug err app";
const char kwBuiltins[] =
    "abs array asc atn cbool cbyte ccur cdate cdbl chdir chr cint clng cos createobject csng cstr "
    "curdir cvar date dateadd datediff datepart dateserial day dir doevents environ eof exp filecopy "
    "filelen fix format freefile hex hour iif inputbox instr instrrev int isarray isdate isempty "
    "isnull isnumeric isobject join kill lbound lcase left len loc lof log ltrim mid minute mkdir "
    "month msgbox name now oct randomize replace right rmdir rnd round rtrim second sgn shell sin "
    "space split sqr str strcomp strreverse string tan time timer timeserial trim typename ubound "
    "ucase val vartype weekday year beep command load unload show hide varptr strptr objptr "
    "collection";
const char kwTypes[] = "boolean byte integer long single double currency string date variant object any";

std::string lower(const std::string &s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back((char)std::tolower((unsigned char)c));
    return o;
}

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0xc0; }

bool inList(const char *list, const std::string &w)
{
    const size_t n = w.size();
    if (!n) return false;
    for (const char *p = list; (p = std::strstr(p, w.c_str())) != nullptr; p += n) {
        bool start = p == list || p[-1] == ' ';
        bool end = p[n] == ' ' || p[n] == 0;
        if (start && end) return true;
    }
    return false;
}

// The text with strings blanked and the comment (from `cut` on) gone.
std::string scan(const std::string &line, bool keepComment, size_t *commentAt)
{
    std::string o = line;
    bool inStr = false;
    size_t cut = std::string::npos;
    bool stmtStart = true;   // where a Rem can start
    for (size_t i = 0; i < o.size(); i++) {
        char c = o[i];
        if (inStr) {
            if (c == '"') {
                if (i + 1 < o.size() && o[i + 1] == '"') { o[i] = ' '; o[i + 1] = ' '; i++; continue; }
                inStr = false;
            } else o[i] = ' ';
            continue;
        }
        if (c == '"') { inStr = true; stmtStart = false; continue; }
        if (c == '\'') { cut = i; break; }
        if (stmtStart && (c == 'r' || c == 'R') && i + 3 <= o.size() && lower(o.substr(i, 3)) == "rem" &&
            (i + 3 == o.size() || !identChar(o[i + 3]))) {
            cut = i;
            break;
        }
        if (c == ':') stmtStart = true;
        else if (c != ' ' && c != '\t') {
            // a line number or label at the start doesn't end "statement start"
            stmtStart = false;
            if (std::isdigit((unsigned char)c)) {
                size_t j = i;
                bool allDigits = true;
                for (size_t k = 0; k < i; k++) if (o[k] != ' ' && o[k] != '\t' && !std::isdigit((unsigned char)o[k])) allDigits = false;
                while (j < o.size() && std::isdigit((unsigned char)o[j])) j++;
                if (allDigits && (j == o.size() || o[j] == ' ' || o[j] == '\t')) { i = j - 1; stmtStart = true; continue; }
            }
        }
    }
    if (commentAt) *commentAt = cut;
    if (cut != std::string::npos && !keepComment) o.erase(cut);
    while (!o.empty() && (o.back() == ' ' || o.back() == '\t' || o.back() == '\r')) o.pop_back();
    return o;
}

} // namespace

std::string maskStrings(const std::string &line) { return scan(line, false, nullptr); }

std::string stripComment(const std::string &line)
{
    size_t cut;
    scan(line, false, &cut);
    std::string s = cut == std::string::npos ? line : line.substr(0, cut);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

std::vector<std::string> codeWords(const std::string &line)
{
    std::string m = maskStrings(line);
    std::vector<std::string> out;
    std::string w;
    bool first = true;
    for (size_t i = 0; i <= m.size(); i++) {
        char c = i < m.size() ? m[i] : ' ';
        if (identChar(c)) { w.push_back((char)std::tolower((unsigned char)c)); continue; }
        if (!w.empty()) {
            // a leading line number ("10 Print") or label ("retry:") isn't part of the statement
            bool number = std::isdigit((unsigned char)w[0]);
            bool label = c == ':' && (i + 1 >= m.size() || m[i + 1] != '=');
            if (first && (number || label)) { w.clear(); continue; }
            out.push_back(w);
            w.clear();
            first = false;
        } else if (c != ' ' && c != '\t') first = false;
    }
    return out;
}

LineInfo classify(const std::string &line)
{
    LineInfo li;
    std::vector<std::string> w = codeWords(line);
    size_t i = 0;
    while (i < w.size() && (w[i] == "public" || w[i] == "private" || w[i] == "friend" || w[i] == "static" ||
                            w[i] == "global"))
        i++;
    if (i >= w.size()) return li;
    const std::string &f = w[i];
    const std::string next = i + 1 < w.size() ? w[i + 1] : "";
    if (f == "sub") li.opens = Block::Sub;
    else if (f == "function") li.opens = Block::Function;
    else if (f == "property" && (next == "get" || next == "let" || next == "set")) li.opens = Block::Property;
    else if (f == "type" && !next.empty()) li.opens = Block::Type;
    else if (f == "enum" && !next.empty()) li.opens = Block::Enum;
    else if (f == "end") {
        if (next == "sub") li.closes = Block::Sub;
        else if (next == "function") li.closes = Block::Function;
        else if (next == "property") li.closes = Block::Property;
        else if (next == "if") li.closes = Block::If;
        else if (next == "select") li.closes = Block::Select;
        else if (next == "with") li.closes = Block::With;
        else if (next == "type") li.closes = Block::Type;
        else if (next == "enum") li.closes = Block::Enum;
    } else if (f == "if") {
        // a block If ends its line with Then; "If a Then b" is a single-line If
        std::string code = maskStrings(line);
        std::string lc = lower(code);
        size_t p = lc.rfind("then");
        if (p != std::string::npos && (p == 0 || !identChar(lc[p - 1])) &&
            (p + 4 == lc.size() || !identChar(lc[p + 4]))) {
            std::string rest = lc.substr(p + 4);
            bool blank = true;
            for (char c : rest) if (c != ' ' && c != '\t') blank = false;
            if (blank) li.opens = Block::If;
        }
    } else if (f == "elseif" || f == "else") li.middle = Block::If;
    else if (f == "for") li.opens = Block::For;
    else if (f == "next") li.closes = Block::For;
    else if (f == "do") li.opens = Block::Do;
    else if (f == "loop") li.closes = Block::Do;
    else if (f == "while") li.opens = Block::While;
    else if (f == "wend") li.closes = Block::While;
    else if (f == "select" && next == "case") li.opens = Block::Select;
    else if (f == "case") li.middle = Block::Select;
    else if (f == "with") li.opens = Block::With;
    return li;
}

int indentChangeAfter(const std::string &line)
{
    LineInfo li = classify(line);
    return li.opens != Block::None || li.middle != Block::None ? 1 : 0;
}

int closerIndent(const std::string &line, const std::vector<std::string> &before,
                 const std::vector<int> &indents, int indentSize)
{
    LineInfo me = classify(line);
    Block want = me.closes != Block::None ? me.closes : me.middle;
    if (want == Block::None) return -1;
    int depth = 0;
    for (size_t k = before.size(); k-- > 0;) {
        LineInfo li = classify(before[k]);
        const int ind = k < indents.size() ? indents[k] : 0;
        if (li.closes == want) { depth++; continue; }
        if (li.opens == want) {
            if (depth == 0) return want == Block::Select && me.middle == Block::Select ? ind + indentSize : ind;
            depth--;
            continue;
        }
        if (li.middle == want && depth == 0) {
            // the part before: Case after Case, Else after ElseIf; End Select is one level out
            if (want == Block::Select && me.closes == Block::Select) return ind > indentSize ? ind - indentSize : 0;
            return ind;
        }
        // a Sub/Function boundary ends the search for anything inside one
        if (want != Block::Sub && want != Block::Function && want != Block::Property &&
            (li.opens == Block::Sub || li.opens == Block::Function || li.opens == Block::Property))
            return -1;
    }
    return -1;
}

const char *keywords(int set)
{
    return set == 0 ? kwStatements : set == 1 ? kwBuiltins : set == 2 ? kwTypes : "";
}

bool isKeyword(const std::string &word)
{
    std::string w = lower(word);
    return inList(kwStatements, w) || inList(kwTypes, w);
}

} // namespace basic
} // namespace af
