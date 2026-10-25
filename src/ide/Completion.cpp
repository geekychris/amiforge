// Code completion: Scintilla's autocompletion list and calltips, fed from
//  - the identifiers in the open documents and the project's files,
//  - C/C++ keywords,
//  - the AutoDocs (every OS function), and per interface after "IExec->", "IIntuition->"...,
// and calltips with the AutoDoc prototype when "(" follows an OS function.
#include "App.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <algorithm>
#include <set>

#include <proto/intuition.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

static const char *cKeywords =
    "auto break case char const continue default do double else enum extern float for goto if inline int "
    "long register restrict return short signed sizeof static struct switch typedef union unsigned void "
    "volatile while bool class namespace template typename public private protected virtual override "
    "nullptr new delete this operator friend using try catch throw const_cast static_cast dynamic_cast "
    "reinterpret_cast true false TRUE FALSE NULL TAG_DONE TAG_END uint8 uint16 uint32 int8 int16 int32 "
    "STRPTR CONST_STRPTR APTR BPTR BOOL";

static bool identChar(int c) { return isalnum(c) || c == '_'; }

static void collectWords(const std::string &text, std::set<std::string> &out)
{
    size_t i = 0, n = text.size();
    while (i < n) {
        if (isalpha((unsigned char)text[i]) || text[i] == '_') {
            size_t a = i;
            while (i < n && identChar((unsigned char)text[i])) i++;
            if (i - a >= 3 && i - a < 64) out.insert(text.substr(a, i - a));
        } else if (isdigit((unsigned char)text[i])) {
            while (i < n && identChar((unsigned char)text[i])) i++;
        } else {
            i++;
        }
    }
}

// Words from the project's files and the other open documents (cached until a file changes).
void App::refreshCompletionWords()
{
    std::vector<std::string> files;
    for (auto &f : project.files) files.push_back(project.abs(f));
    for (auto &d : docs) if (!d->path.empty() && d.get() != cur) files.push_back(d->path);
    for (auto &f : files) {
        int64_t t = fileTime(f);
        auto &c = wordCache[f];
        if (c.first == t) continue;
        std::string text;
        if (!readFile(f, text)) continue;
        std::set<std::string> w;
        collectWords(text, w);
        c.first = t;
        c.second.assign(w.begin(), w.end());
    }
}

// "IExec" -> "exec.library", "IIntuition" -> "intuition.library", "IDOS" -> "dos.library"
static std::string libraryForInterface(const std::string &iface)
{
    if (iface.size() < 2 || iface[0] != 'I') return "";
    std::string n = iface.substr(1);
    for (auto &c : n) c = tolower((unsigned char)c);
    return n;
}

void App::showCompletion(bool explicitRequest)
{
    if (!editor || !cur) return;
    intptr_t pos = S(editor, SCI_GETCURRENTPOS);
    intptr_t start = S(editor, SCI_WORDSTARTPOSITION, pos, 1);
    intptr_t len = pos - start;
    std::string prefix;
    if (len > 0) {
        std::vector<char> buf(len + 1);
        struct Sci_TextRangeFull tr = { { start, pos }, buf.data() };
        S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
        prefix = buf.data();
    }
    // after "IFoo->": that library's functions
    std::string iface;
    if (start >= 2 && (char)S(editor, SCI_GETCHARAT, start - 1) == '>' && (char)S(editor, SCI_GETCHARAT, start - 2) == '-') {
        intptr_t ie = start - 2, is = S(editor, SCI_WORDSTARTPOSITION, ie, 1);
        if (ie > is) {
            std::vector<char> buf(ie - is + 1);
            struct Sci_TextRangeFull tr = { { is, ie }, buf.data() };
            S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
            iface = buf.data();
        }
    }
    std::set<std::string> words;
    std::string lib = libraryForInterface(iface);
    std::string scopeName;
    int scopeKind = lib.empty() ? scopeAt(start, scopeName) : 0;
    if (!lib.empty() && loadDocIndex()) {
        for (auto &e : docIndex) {
            std::string l = e.lib;
            for (auto &c : l) c = tolower((unsigned char)c);
            if (l.compare(0, lib.size(), lib) == 0 && (l.size() == lib.size() || l[lib.size()] == '.')) {
                words.insert(e.func);
                // the varargs forms that the interfaces also have
                if (e.func.size() > 7 && e.func.compare(e.func.size() - 7, 7, "TagList") == 0)
                    words.insert(e.func.substr(0, e.func.size() - 7) + "Tags");
            }
        }
    } else if (scopeKind == 1) {
        // "os::", "AppWindow::", "std::"
        busyPointer(true);
        scopeMembers(scopeName, words);
        busyPointer(false);
        if (words.empty()) setStatus("Nothing found inside " + scopeName);
    } else if (scopeKind == 2) {
        // "win." / "win->": the members of its type
        busyPointer(true);
        std::string type = typeOfVariable(scopeName, S(editor, SCI_LINEFROMPOSITION, start));
        if (!type.empty()) typeMembers(type, words);
        busyPointer(false);
        if (type.empty()) setStatus("Can't tell the type of " + scopeName);
        else if (words.empty()) setStatus("No members found for " + type);
        else setStatus(scopeName + " is a " + type);
    } else {
        if (!explicitRequest && len < 3) return;
        // this document
        intptr_t dlen = S(editor, SCI_GETLENGTH);
        std::vector<char> text(dlen + 1);
        S(editor, SCI_GETTEXT, dlen + 1, (intptr_t)text.data());
        std::set<std::string> local;
        collectWords(std::string(text.data(), dlen), local);
        local.erase(prefix);                  // the word being typed isn't a suggestion
        words.insert(local.begin(), local.end());
        refreshCompletionWords();
        for (auto &c : wordCache) words.insert(c.second.second.begin(), c.second.second.end());
        std::set<std::string> kw;
        collectWords(cKeywords, kw);
        words.insert(kw.begin(), kw.end());
        if (loadDocIndex())
            for (auto &e : docIndex)
                if (isalpha((unsigned char)e.func[0]) && e.func.find('-') == std::string::npos) words.insert(e.func);
    }
    // keep those that start with the prefix
    std::string list;
    int count = 0;
    for (auto &w : words) {
        if (!prefix.empty() && strncasecmp(w.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        if (w == prefix) continue;
        if (!list.empty()) list += ' ';
        list += w;
        if (++count >= 400) break;
    }
    if (count == 0) {
        if (explicitRequest) IIntuition->DisplayBeep(nullptr);
        return;
    }
    S(editor, SCI_AUTOCSETIGNORECASE, 1);
    S(editor, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    S(editor, SCI_AUTOCSETSEPARATOR, ' ');
    S(editor, SCI_AUTOCSETMAXHEIGHT, 12);
    S(editor, SCI_AUTOCSETCANCELATSTART, 0);
    S(editor, SCI_AUTOCSETFILLUPS, 0, (intptr_t)"(;");
    S(editor, SCI_AUTOCSHOW, len, (intptr_t)list.c_str());
}

// "(" after an OS function: its prototype from the AutoDoc's SYNOPSIS.
void App::showCallTip()
{
    if (!editor) return;
    intptr_t pos = S(editor, SCI_GETCURRENTPOS) - 1;            // the "("
    intptr_t e = pos;
    while (e > 0 && (char)S(editor, SCI_GETCHARAT, e - 1) == ' ') e--;
    intptr_t s = S(editor, SCI_WORDSTARTPOSITION, e, 1);
    if (e <= s) return;
    std::vector<char> buf(e - s + 1);
    struct Sci_TextRangeFull tr = { { s, e }, buf.data() };
    S(editor, SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    std::string name = buf.data();
    if (name.size() < 3) return;
    const DocEntry *d = findDoc(name);
    if (!d) return;
    std::string proto = docPrototype(*d, name);
    if (proto.empty()) return;
    S(editor, SCI_CALLTIPSHOW, pos + 1, (intptr_t)proto.c_str());
}

// The C prototype in an entry's SYNOPSIS: the line with "name(" that ends in ";" (prefer the
// exact name typed, e.g. the Tags form), joined if it continues on the next line.
std::string App::docPrototype(const DocEntry &e, const std::string &name)
{
    std::string text = docEntryText(e);
    size_t syn = text.find("SYNOPSIS");
    if (syn == std::string::npos) return "";
    size_t end = text.find("\n   FUNCTION", syn);
    if (end == std::string::npos) end = std::min(text.size(), syn + 1500);
    std::string sec = text.substr(syn, end - syn);
    std::string best;
    size_t pos = 0;
    while (pos < sec.size()) {
        size_t nl = sec.find('\n', pos);
        if (nl == std::string::npos) nl = sec.size();
        std::string line = trim(sec.substr(pos, nl - pos));
        // a prototype: has a "(" and a type before the name
        size_t p = line.find('(');
        if (p != std::string::npos && line.find(' ') < p) {
            std::string full = line;
            size_t next = nl;
            while (full.find(';') == std::string::npos && full.find(')') == std::string::npos && next < sec.size()) {
                size_t nl2 = sec.find('\n', next + 1);
                if (nl2 == std::string::npos) nl2 = sec.size();
                full += " " + trim(sec.substr(next + 1, nl2 - next - 1));
                next = nl2;
            }
            bool named = full.find(name + "(") != std::string::npos || full.find(name + " (") != std::string::npos;
            bool decl = full.find(';') != std::string::npos;   // a C declaration, with the types
            if (named && decl) return full;
            if (named && (best.empty() || best.find(name) == std::string::npos)) best = full;
            else if (best.empty()) best = full;
        }
        pos = nl + 1;
    }
    return best;
}

void App::completionCharAdded(int ch)
{
    if (!editor || !cur) return;
    std::string e = extension(cur->path.empty() ? "x.c" : cur->path);
    if (e != "c" && e != "h" && !isCppFile(cur->path.empty() ? "x.c" : cur->path) && e != "hpp") return;
    // not inside comments or strings (LexCPP styles 1-3 comments, 6/7 strings/chars)
    intptr_t pos = S(editor, SCI_GETCURRENTPOS);
    int style = (int)S(editor, SCI_GETSTYLEAT, pos > 0 ? pos - 1 : 0);
    if (style == 1 || style == 2 || style == 3 || style == 6 || style == 7 || style == 15) return;
    if (ch == '(') {
        showCallTip();
        return;
    }
    if (ch == ')') {
        S(editor, SCI_CALLTIPCANCEL);
        return;
    }
    // right after "->", "::" or "." (not a number's point): what's in that scope
    bool arrow = ch == '>' && pos >= 2 && (char)S(editor, SCI_GETCHARAT, pos - 2) == '-';
    bool colons = ch == ':' && pos >= 2 && (char)S(editor, SCI_GETCHARAT, pos - 2) == ':';
    bool dot = ch == '.' && pos >= 2 && identChar((unsigned char)S(editor, SCI_GETCHARAT, pos - 2));
    if (arrow || colons || dot) {
        showCompletion(false);
        return;
    }
    if (identChar(ch) && !S(editor, SCI_AUTOCACTIVE)) {
        intptr_t start = S(editor, SCI_WORDSTARTPOSITION, pos, 1);
        std::string scope;
        if (pos - start == 3 || scopeAt(start, scope)) showCompletion(false);
    }
}

} // namespace af
