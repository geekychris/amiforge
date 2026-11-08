// --- basic --- Boing BASIC projects: controls, code-behind, outline, .bbp (see BasicModel.h).
#include "BasicModel.h"
#include "sci/BasicSyntax.h"
#include "GuiModel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace af {
namespace basic {

namespace {

std::string lower(const std::string &s)
{
    std::string o;
    o.reserve(s.size());
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

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0xc0; }

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) {
            if (p < text.size()) out.push_back(text.substr(p));
            break;
        }
        std::string l = text.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        p = e + 1;
    }
    return out;
}

int indentOf(const std::string &l)
{
    int n = 0;
    for (char c : l) {
        if (c == ' ') n++;
        else if (c == '\t') n = (n / 4 + 1) * 4;
        else break;
    }
    return n;
}

// --- the built-in control table --------------------------------------------------------

Member P(const char *n, const char *type, const char *doc = "") { return { n, "", type, doc }; }
Member M(const char *n, const char *params, const char *doc = "") { return { n, params, "", doc }; }
Member E(const char *n, const char *params = "", const char *doc = "") { return { n, params, "", doc }; }

std::vector<ControlInfo> builtinControls()
{
    const std::vector<Member> common = { P("Enabled", "Boolean", "False greys it out"),
                                         P("Hint", "String", "the help bubble") };
    const std::vector<Member> focus = { M("SetFocus", "", "makes it the active gadget") };
    auto withCommon = [&](std::vector<Member> own) {
        own.insert(own.end(), common.begin(), common.end());
        return own;
    };
    std::vector<ControlInfo> t = {
        { "window", "Form",
          { P("Caption", "String", "the window title"), P("Width", "Long"), P("Height", "Long"),
            P("Left", "Long"), P("Top", "Long"), P("Visible", "Boolean") },
          { M("Show", "", "opens the window"), M("Hide", "", "closes the window, keeps the form"),
            M("Refresh", "") },
          { E("Load", "", "before the window opens"), E("Unload", "Cancel As Integer", "the window is closing; Cancel = 1 keeps it open"),
            E("Resize"), E("Activate"), E("Deactivate"),
            E("KeyDown", "KeyCode As Integer, Shift As Integer"), E("KeyUp", "KeyCode As Integer, Shift As Integer") },
          "Load" },
        { "button", "Button", withCommon({ P("Text", "String", "the label") }), focus, { E("Click") }, "Click" },
        { "string", "TextBox", withCommon({ P("Text", "String"), P("MaxLength", "Integer") }), focus,
          { E("Change", "", "the text was changed (Return or leaving the gadget)"), E("GotFocus"), E("LostFocus") },
          "Change" },
        { "integer", "IntegerBox", withCommon({ P("Value", "Long"), P("Min", "Long"), P("Max", "Long") }), focus,
          { E("Change") }, "Change" },
        { "checkbox", "CheckBox", withCommon({ P("Value", "Boolean", "True when ticked"), P("Text", "String") }), focus,
          { E("Click") }, "Click" },
        { "chooser", "ComboBox",
          withCommon({ P("ListIndex", "Integer", "the chosen item, 0 = first"), P("Text", "String", "the chosen item's text"),
                       P("ListCount", "Integer"), P("List", "String", "List(i): item i") }),
          { M("AddItem", "Item As String"), M("RemoveItem", "Index As Integer"), M("Clear", "") },
          { E("Click", "", "an item was chosen") }, "Click" },
        { "radio", "OptionGroup", withCommon({ P("ListIndex", "Integer", "the chosen button, 0 = first") }), {},
          { E("Click") }, "Click" },
        { "slider", "Slider", withCommon({ P("Value", "Long"), P("Min", "Long"), P("Max", "Long") }), {},
          { E("Change", "", "the knob was let go"), E("Scroll", "", "while it is dragged") }, "Change" },
        { "scroller", "ScrollBar",
          withCommon({ P("Value", "Long", "the top"), P("Max", "Long", "the total"), P("LargeChange", "Long", "the visible part") }), {},
          { E("Change"), E("Scroll") }, "Change" },
        { "listbrowser", "ListBox",
          withCommon({ P("ListIndex", "Integer", "the selected row, -1 = none"), P("Text", "String", "the selected row's text"),
                       P("ListCount", "Integer"), P("List", "String", "List(i): row i") }),
          { M("AddItem", "Item As String"), M("RemoveItem", "Index As Integer"), M("Clear", "") },
          { E("Click"), E("DblClick") }, "Click" },
        { "label", "Label", { P("Text", "String") }, {}, {}, "" },
        { "fuelgauge", "ProgressBar", { P("Value", "Long"), P("Min", "Long"), P("Max", "Long") }, {}, {}, "" },
        { "getfile", "FileBox", withCommon({ P("FileName", "String"), P("Title", "String"), P("Pattern", "String") }),
          { M("Request", "", "opens the file requester") }, { E("Change", "", "a file was chosen") }, "Change" },
        { "texteditor", "TextEditor", withCommon({ P("Text", "String") }), focus, { E("Change") }, "Change" },
        { "timer", "Timer", { P("Interval", "Long", "milliseconds"), P("Enabled", "Boolean") }, {}, { E("Timer") }, "Timer" },
    };
    return t;
}

std::vector<ControlInfo> &table()
{
    static std::vector<ControlInfo> t = builtinControls();
    return t;
}

} // namespace

bool equalsNoCase(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

const Member *ControlInfo::event(const std::string &name) const
{
    for (auto &e : events)
        if (equalsNoCase(e.name, name)) return &e;
    return nullptr;
}

const std::vector<ControlInfo> &controls() { return table(); }

const ControlInfo *controlFor(const std::string &kind)
{
    for (auto &c : table())
        if (equalsNoCase(c.kind, kind)) return &c;
    return nullptr;
}

void setControls(const std::vector<ControlInfo> &t)
{
    // kinds the document doesn't describe keep their built-in entry
    std::vector<ControlInfo> merged = t;
    for (auto &b : builtinControls()) {
        bool have = false;
        for (auto &c : merged) have = have || equalsNoCase(c.kind, b.kind);
        if (!have) merged.push_back(b);
    }
    table() = merged;
}

void resetControls() { table() = builtinControls(); }

// forms-events.md: a section per control kind, headed "## <kind>" (the .afg kind name, maybe
// in `backquotes`, maybe followed by " - BasicType" or " (BasicType)"); in it, "Properties",
// "Methods" and "Events" (a heading, a **bold** line or "Events:") followed by list items
// "- Name(params) As Type - text" (or a table row "| Name(params) | ... |"). "(default)" in an
// event's line marks the default event; otherwise the first event is.
bool parseFormsEvents(const std::string &md, std::vector<ControlInfo> &out, std::string *err)
{
    out.clear();
    ControlInfo *cur = nullptr;
    int list = 0;   // 1 properties, 2 methods, 3 events
    bool defaultMarked = false;
    auto finish = [&]() {
        if (cur && cur->defaultEvent.empty() && !cur->events.empty()) cur->defaultEvent = cur->events.front().name;
    };
    for (const std::string &raw : splitLines(md)) {
        std::string l = trimmed(raw);
        if (l.empty()) continue;
        std::string ll = lower(l);
        if (l.compare(0, 2, "##") == 0 && l.compare(0, 4, "####") != 0 && (l.size() < 3 || l[2] != '#')) {
            // a control section
            finish();
            cur = nullptr;
            list = 0;
            defaultMarked = false;
            std::string h = trimmed(l.substr(2));
            std::string word, rest;
            size_t i = 0;
            while (i < h.size() && (h[i] == '`' || h[i] == ' ')) i++;
            while (i < h.size() && identChar(h[i])) word.push_back(h[i++]);
            rest = h.substr(i);
            std::string kind = lower(word);
            if (kind == "form") kind = "window";
            std::string type;
            // "button - Button", "button (CommandButton)", "`button` — Button"
            for (size_t k = 0; k < rest.size(); k++)
                if (std::isalpha((unsigned char)rest[k])) {
                    while (k < rest.size() && identChar(rest[k])) type.push_back(rest[k++]);
                    break;
                }
            if (kind.empty()) continue;
            static const char *const known[] = { "window", "button", "string", "integer", "checkbox", "chooser",
                                                 "radio", "slider", "scroller", "listbrowser", "label", "space",
                                                 "fuelgauge", "getfile", "texteditor", "timer", "vlayout",
                                                 "hlayout", nullptr };
            bool ok = false;
            for (const char *const *k = known; *k; k++) ok = ok || kind == *k;
            if (!ok) continue;
            out.push_back(ControlInfo());
            cur = &out.back();
            cur->kind = kind;
            cur->basicType = type.empty() ? (kind == "window" ? "Form" : word) : type;
            continue;
        }
        if (!cur) continue;
        // a list heading
        std::string bare = ll;
        bare.erase(std::remove_if(bare.begin(), bare.end(), [](char c) { return c == '#' || c == '*' || c == ':' || c == '_'; }), bare.end());
        bare = trimmed(bare);
        if (bare == "properties") { list = 1; continue; }
        if (bare == "methods") { list = 2; continue; }
        if (bare == "events") { list = 3; continue; }
        if (l[0] == '#') { list = 0; continue; }   // another sub-heading
        if (!list) continue;
        std::string item;
        if (l[0] == '-' || l[0] == '*') item = trimmed(l.substr(1));
        else if (l[0] == '|') {
            // a table row: the first cell
            size_t e = l.find('|', 1);
            item = trimmed(l.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            if (item.find("---") != std::string::npos) continue;
            std::string li = lower(item);
            if (li == "name" || li == "property" || li == "method" || li == "event") continue;
            item += " " + (e == std::string::npos ? "" : l.substr(e));
        } else continue;
        // Name[(params)] [As Type] [- doc]
        size_t i = 0;
        while (i < item.size() && (item[i] == '`' || item[i] == '*')) i++;
        Member m;
        while (i < item.size() && identChar(item[i])) m.name.push_back(item[i++]);
        if (m.name.empty()) continue;
        // a "Form_" or "<id>_" prefix in the event's name isn't part of it
        if (list == 3) {
            size_t us = m.name.find('_');
            if (us != std::string::npos && (lower(m.name.substr(0, us)) == "form" || m.name.substr(0, us) == "id" ||
                                            lower(m.name.substr(0, us)) == cur->kind))
                m.name = m.name.substr(us + 1);
        }
        while (i < item.size() && item[i] == ' ') i++;
        if (i < item.size() && item[i] == '(') {
            int depth = 0;
            size_t s = i;
            for (; i < item.size(); i++) {
                if (item[i] == '(') depth++;
                else if (item[i] == ')' && --depth == 0) break;
            }
            m.params = trimmed(item.substr(s + 1, i > s ? i - s - 1 : 0));
            if (i < item.size()) i++;
        }
        std::string rest = item.substr(std::min(i, item.size()));
        while (!rest.empty() && (rest[0] == '`' || rest[0] == '*')) rest.erase(0, 1);
        rest = trimmed(rest);
        std::string lr = lower(rest);
        if (lr.compare(0, 3, "as ") == 0) {
            size_t k = 3;
            while (k < rest.size() && rest[k] == ' ') k++;
            while (k < rest.size() && identChar(rest[k])) m.type.push_back(rest[k++]);
            rest = trimmed(rest.substr(k));
            lr = lower(rest);
        }
        bool isDefault = lr.find("(default)") != std::string::npos || lower(item).find("default event") != std::string::npos;
        // the doc text: after a dash or a table separator
        size_t d = rest.find_first_not_of(" -|:\xe2\x80\x93\x94`*");
        m.doc = d == std::string::npos ? "" : trimmed(rest.substr(d));
        while (!m.doc.empty() && (m.doc.back() == '|' || m.doc.back() == ' ')) m.doc.pop_back();
        if (list == 1) cur->properties.push_back(m);
        else if (list == 2) cur->methods.push_back(m);
        else {
            cur->events.push_back(m);
            if (isDefault && !defaultMarked) { cur->defaultEvent = m.name; defaultMarked = true; }
        }
    }
    finish();
    if (out.empty() && err) *err = "no control sections (## button ...) found";
    return !out.empty();
}

// --- procedures ------------------------------------------------------------------------------

std::vector<Proc> procedures(const std::string &text)
{
    std::vector<Proc> out;
    std::vector<std::string> ls = splitLines(text);
    Proc *open = nullptr;
    size_t i = 0;
    while (i < ls.size()) {
        // a logical line: " _" at the end continues it
        const int first = (int)i;
        std::string logical = stripComment(ls[i]);
        while (logical.size() >= 2 && logical.back() == '_' && (logical[logical.size() - 2] == ' ' || logical[logical.size() - 2] == '\t') &&
               i + 1 < ls.size()) {
            logical.pop_back();
            logical += " " + stripComment(ls[++i]);
        }
        i++;
        std::vector<std::string> w = codeWords(logical);
        if (w.empty()) continue;
        if (open) {
            if (w.size() >= 2 && w[0] == "end" && (w[1] == "sub" || w[1] == "function" || w[1] == "property")) {
                open->endLine = first + 1;
                open = nullptr;
            } else {
                open->hasCode = true;
            }
            continue;
        }
        size_t k = 0;
        Proc p;
        while (k < w.size()) {
            if (w[k] == "public") p.scope = "Public";
            else if (w[k] == "private") p.scope = "Private";
            else if (w[k] == "friend") p.scope = "Friend";
            else if (w[k] == "static") p.isStatic = true;
            else break;
            k++;
        }
        if (k >= w.size()) continue;
        size_t nameAt = k + 1;
        if (w[k] == "sub") p.kind = ProcKind::Sub;
        else if (w[k] == "function") p.kind = ProcKind::Function;
        else if (w[k] == "property" && k + 1 < w.size() && (w[k + 1] == "get" || w[k + 1] == "let" || w[k + 1] == "set")) {
            p.kind = w[k + 1] == "get" ? ProcKind::PropertyGet : w[k + 1] == "let" ? ProcKind::PropertyLet : ProcKind::PropertySet;
            nameAt = k + 2;
        } else continue;
        if (nameAt >= w.size()) continue;
        // the name and parameters as written (codeWords lowers them)
        std::string masked = maskStrings(logical);
        std::string lm = lower(masked);
        size_t pos = 0;
        for (size_t n = 0; n <= nameAt && pos != std::string::npos; n++) {
            // find word n
            while (pos < lm.size() && !identChar(lm[pos])) pos++;
            if (n == nameAt) break;
            while (pos < lm.size() && identChar(lm[pos])) pos++;
        }
        // skip a leading line number or label (codeWords dropped it)
        {
            std::string lead;
            size_t q = 0;
            while (q < lm.size() && (lm[q] == ' ' || lm[q] == '\t')) q++;
            size_t ws = q;
            while (q < lm.size() && identChar(lm[q])) q++;
            lead = lm.substr(ws, q - ws);
            bool number = !lead.empty() && std::isdigit((unsigned char)lead[0]);
            bool label = q < lm.size() && lm[q] == ':';
            if (number || label) {
                pos = q + 1;
                for (size_t n = 0; n <= nameAt; n++) {
                    while (pos < lm.size() && !identChar(lm[pos])) pos++;
                    if (n == nameAt) break;
                    while (pos < lm.size() && identChar(lm[pos])) pos++;
                }
            }
        }
        size_t ne = pos;
        while (ne < lm.size() && identChar(lm[ne])) ne++;
        p.name = logical.substr(pos, ne - pos);
        // the column on the header's first physical line
        size_t col = lower(ls[first]).find(lower(p.name));
        p.nameColumn = col == std::string::npos ? 0 : (int)col;
        size_t lp = logical.find('(', ne);
        if (lp != std::string::npos && trimmed(logical.substr(ne, lp - ne)).find_first_not_of("$%&!#@") == std::string::npos) {
            int depth = 0;
            size_t q = lp;
            for (; q < masked.size(); q++) {
                if (masked[q] == '(') depth++;
                else if (masked[q] == ')' && --depth == 0) break;
            }
            p.params = trimmed(logical.substr(lp + 1, q > lp ? q - lp - 1 : 0));
        }
        p.line = first + 1;
        out.push_back(p);
        open = &out.back();
    }
    return out;
}

int findProc(const std::vector<Proc> &procs, const std::string &name)
{
    for (size_t i = 0; i < procs.size(); i++)
        if (equalsNoCase(procs[i].name, name)) return (int)i;
    return -1;
}

std::string eventSubName(const std::string &controlId, const std::string &event, bool isForm)
{
    return (isForm ? std::string("Form") : controlId) + "_" + event;
}

bool ensureSub(std::string &text, const std::string &name, const std::string &params, SubPlace &where, int indentWidth)
{
    std::vector<Proc> procs = procedures(text);
    int i = findProc(procs, name);
    std::vector<std::string> ls = splitLines(text);
    if (i >= 0) {
        const Proc &p = procs[i];
        where.created = false;
        where.line = p.line + 1;
        // a header continued with " _": the body starts after it
        while (where.line - 2 < (int)ls.size() && where.line - 2 >= 0) {
            std::string h = stripComment(ls[where.line - 2]);
            if (h.size() >= 2 && h.back() == '_' && (h[h.size() - 2] == ' ' || h[h.size() - 2] == '\t')) where.line++;
            else break;
        }
        // the first line with code in the body, or the first body line
        int bodyEnd = p.endLine > 0 ? p.endLine : (int)ls.size() + 1;
        where.column = indentWidth;
        if (p.endLine > 0 && p.endLine == where.line) {
            // "Sub x()" right before "End Sub": make the empty line to type on
            size_t at = 0;
            for (int l = 1; l < p.endLine; l++) at = text.find('\n', at) + 1;
            text.insert(at, "\n");
            return true;
        }
        for (int l = where.line; l < bodyEnd; l++) {
            if (l - 1 >= (int)ls.size()) break;
            if (!trimmed(ls[l - 1]).empty()) {
                where.line = l;
                where.column = indentOf(ls[l - 1]);
                break;
            }
        }
        return false;
    }
    std::string add;
    if (!text.empty() && text.back() != '\n') add += "\n";
    // one blank line between procedures
    const bool blank = !ls.empty() && !trimmed(ls.back()).empty();
    if (blank) add += "\n";
    const int headerLine = (int)ls.size() + 1 + (blank ? 1 : 0);
    add += "Private Sub " + name + "(" + params + ")\n\nEnd Sub\n";
    text += add;
    where.created = true;
    where.line = headerLine + 1;
    where.column = indentWidth;
    return true;
}

static std::set<std::string> eventsMatching(const std::vector<Proc> &procs, const std::string &controlId,
                                            const ControlInfo &ci, bool isForm, bool needCode)
{
    std::set<std::string> out;
    for (auto &e : ci.events) {
        int i = findProc(procs, eventSubName(controlId, e.name, isForm));
        if (i >= 0 && procs[i].kind == ProcKind::Sub && (!needCode || procs[i].hasCode)) out.insert(e.name);
    }
    return out;
}

std::set<std::string> eventsWithSubs(const std::vector<Proc> &procs, const std::string &controlId,
                                     const ControlInfo &ci, bool isForm)
{
    return eventsMatching(procs, controlId, ci, isForm, false);
}

std::set<std::string> eventsWithCode(const std::vector<Proc> &procs, const std::string &controlId,
                                     const ControlInfo &ci, bool isForm)
{
    return eventsMatching(procs, controlId, ci, isForm, true);
}

std::vector<std::string> controlSubs(const std::vector<Proc> &procs, const std::string &oldId, const ControlInfo *ci)
{
    std::vector<std::string> out;
    const std::string prefix = lower(oldId) + "_";
    for (auto &p : procs) {
        if (p.kind != ProcKind::Sub) continue;
        std::string ln = lower(p.name);
        if (ln.compare(0, prefix.size(), prefix) != 0 || ln.size() == prefix.size()) continue;
        std::string ev = p.name.substr(prefix.size());
        if (ci && !ci->event(ev)) continue;
        out.push_back(p.name);
    }
    return out;
}

std::string maskCode(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        std::string l = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        std::string m = maskStrings(l);   // without the comment
        m.resize(l.size(), ' ');
        // a CR stays
        if (!l.empty() && l.back() == '\r') m.back() = '\r';
        out += m;
        if (e == std::string::npos) break;
        out += '\n';
        p = e + 1;
    }
    return out;
}

std::vector<size_t> findIdentifier(const std::string &text, const std::string &name)
{
    std::vector<size_t> out;
    if (name.empty()) return out;
    std::string m = lower(maskCode(text));
    std::string needle = lower(name);
    size_t p = 0;
    while ((p = m.find(needle, p)) != std::string::npos) {
        size_t e = p + needle.size();
        bool before = p == 0 || !identChar(m[p - 1]);
        // a type suffix may follow (name$)
        bool after = e >= m.size() || !identChar(m[e]);
        if (before && after) out.push_back(p);
        p = e;
    }
    return out;
}

std::string renameControl(const std::string &text, const std::string &oldId, const std::string &newId,
                          bool references, int *subs, int *refs)
{
    std::vector<Proc> procs = procedures(text);
    std::vector<std::string> names = controlSubs(procs, oldId, nullptr);
    // every identifier occurrence to replace: (offset, length, replacement)
    struct Hit { size_t at, len; std::string with; };
    std::vector<Hit> hits;
    int nSubs = 0, nRefs = 0;
    for (auto &n : names) {
        std::vector<size_t> at = findIdentifier(text, n);
        if (!at.empty()) nSubs++;
        for (size_t a : at) hits.push_back({ a, n.size(), newId + text.substr(a + oldId.size(), n.size() - oldId.size()) });
    }
    if (references)
        for (size_t a : findIdentifier(text, oldId)) {
            hits.push_back({ a, oldId.size(), newId });
            nRefs++;
        }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.at < b.at; });
    std::string out;
    size_t last = 0;
    for (auto &h : hits) {
        if (h.at < last) continue;
        out.append(text, last, h.at - last);
        out += h.with;
        last = h.at + h.len;
    }
    out.append(text, last, std::string::npos);
    if (subs) *subs = nSubs;
    if (refs) *refs = nRefs;
    return out;
}

// --- the language --------------------------------------------------------------------------

std::vector<Symbol> outline(const std::string &text)
{
    std::vector<Symbol> out;
    std::vector<std::string> ls = splitLines(text);
    std::vector<Proc> procs = procedures(text);
    size_t pi = 0;
    bool inProc = false, inType = false;
    int procEnd = 0;
    for (size_t i = 0; i < ls.size(); i++) {
        const int line = (int)i + 1;
        if (pi < procs.size() && procs[pi].line == line) {
            const Proc &p = procs[pi++];
            Symbol s{ SymKind::Function, p.name, line, 0, "(" + p.params + ")" };
            if (p.kind == ProcKind::PropertyGet) { s.kind = SymKind::Method; s.detail = "Property Get"; }
            else if (p.kind == ProcKind::PropertyLet) { s.kind = SymKind::Method; s.detail = "Property Let"; }
            else if (p.kind == ProcKind::PropertySet) { s.kind = SymKind::Method; s.detail = "Property Set"; }
            out.push_back(s);
            inProc = true;
            procEnd = p.endLine;
            continue;
        }
        if (inProc) {
            if (procEnd > 0 && line >= procEnd) inProc = false;
            continue;
        }
        std::vector<std::string> w = codeWords(ls[i]);
        if (w.empty()) continue;
        if (inType) {
            if (w.size() >= 2 && w[0] == "end" && (w[1] == "type" || w[1] == "enum")) inType = false;
            continue;
        }
        size_t k = 0;
        bool scoped = false;
        while (k < w.size() && (w[k] == "public" || w[k] == "private" || w[k] == "global" || w[k] == "friend")) { k++; scoped = true; }
        if (k >= w.size()) continue;
        // names as written: the k+1'th identifier of the line
        auto nameAt = [&](size_t idx) {
            std::string m = maskStrings(ls[i]);
            size_t pos = 0, n = 0;
            // skip the words before (and a leading label/line number, which codeWords dropped)
            std::vector<std::pair<size_t, size_t>> spans;
            while (pos < m.size()) {
                while (pos < m.size() && !identChar(m[pos])) pos++;
                size_t s = pos;
                while (pos < m.size() && identChar(m[pos])) pos++;
                if (pos > s) spans.push_back({ s, pos - s });
            }
            size_t skip = spans.size() > w.size() ? spans.size() - w.size() : 0;
            n = skip + idx;
            return n < spans.size() ? ls[i].substr(spans[n].first, spans[n].second) : std::string();
        };
        const std::string &f = w[k];
        if (f == "type" && k + 1 < w.size()) { out.push_back({ SymKind::Struct, nameAt(k + 1), line, 0, "" }); inType = true; }
        else if (f == "enum" && k + 1 < w.size()) { out.push_back({ SymKind::Enum, nameAt(k + 1), line, 0, "" }); inType = true; }
        else if (f == "declare" && k + 2 < w.size() && (w[k + 1] == "sub" || w[k + 1] == "function"))
            out.push_back({ SymKind::Function, nameAt(k + 2), line, 0, "Declare" });
        else if (f == "const" && k + 1 < w.size()) out.push_back({ SymKind::Define, nameAt(k + 1), line, 0, "" });
        else if (f == "event" && k + 1 < w.size()) out.push_back({ SymKind::Method, nameAt(k + 1), line, 0, "Event" });
        else if ((f == "dim" || (scoped && f != "sub" && f != "function")) && k + (f == "dim" ? 1 : 0) < w.size()) {
            // module-level variables: Dim a As Long, b; Public total As Long; Dim WithEvents x
            size_t at = f == "dim" ? k + 1 : k;
            if (at < w.size() && w[at] == "withevents") at++;
            if (at < w.size() && !isKeyword(w[at])) out.push_back({ SymKind::Global, nameAt(at), line, 0, "" });
        }
    }
    return out;
}

bool isBasicFile(const std::string &path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = lower(path.substr(dot + 1));
    return e == "bas" || e == "cls" || e == "bi";
}

std::vector<Symbol> parseSymbols(const std::string &output)
{
    std::vector<Symbol> out;
    for (const std::string &l : splitLines(output)) {
        std::vector<std::string> f;
        size_t p = 0;
        while (true) {
            size_t t = l.find('\t', p);
            f.push_back(l.substr(p, t == std::string::npos ? std::string::npos : t - p));
            if (t == std::string::npos) break;
            p = t + 1;
        }
        if (f.size() < 3) continue;
        int line = std::atoi(f[2].c_str());
        if (line <= 0 || f[1].empty()) continue;
        std::string k = lower(f[0]);
        Symbol s{ SymKind::Function, f[1], line, 0, "" };
        std::string sig = f.size() >= 5 ? f[4] : "";
        size_t lp = sig.find('('), rp = sig.rfind(')');
        if (k == "sub" || k == "function") s.detail = lp != std::string::npos && rp != std::string::npos && rp > lp ? sig.substr(lp, rp - lp + 1) : "()";
        else if (k == "property-get" || k == "property-let" || k == "property-set") {
            s.kind = SymKind::Method;
            s.detail = k == "property-get" ? "Property Get" : k == "property-let" ? "Property Let" : "Property Set";
        } else if (k == "type") s.kind = SymKind::Struct;
        else if (k == "enum") s.kind = SymKind::Enum;
        else if (k == "const") s.kind = SymKind::Define;
        else if (k == "var" || k == "variable" || k == "dim") s.kind = SymKind::Global;
        else if (k == "class") s.kind = SymKind::Class;
        else if (k == "event") { s.kind = SymKind::Method; s.detail = "Event"; }
        else if (k == "declare") s.detail = "Declare";
        else continue;
        out.push_back(s);
    }
    return out;
}

// --- projects ----------------------------------------------------------------------------

std::vector<std::string> Bbp::allFiles() const
{
    std::vector<std::string> v = modules;
    for (auto &f : forms) {
        v.push_back(f);
        v.push_back(codeBehindOf(f));
    }
    v.insert(v.end(), classes.begin(), classes.end());
    return v;
}

bool parseBbp(const std::string &text, Bbp &out)
{
    out = Bbp();
    bool any = false;
    for (const std::string &raw : splitLines(text)) {
        std::string l = trimmed(raw);
        if (l.empty() || l[0] == '#' || l[0] == '\'' || l[0] == ';') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimmed(l.substr(0, eq)), v = trimmed(l.substr(eq + 1));
        // a trailing ' comment (outside quotes)
        size_t c = v.find(" '");
        if (c != std::string::npos) v = trimmed(v.substr(0, c));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        std::string lk = lower(k);
        any = true;
        if (lk == "name") out.name = v;
        else if (lk == "startup") out.startup = v;
        else if (lk == "module") out.modules.push_back(v);
        else if (lk == "form") out.forms.push_back(v);
        else if (lk == "class") out.classes.push_back(v);
        else out.other.push_back({ k, v });
    }
    return any;
}

std::string writeBbp(const Bbp &b)
{
    std::string s = "# Boing BASIC project\n";
    s += "Name=" + b.name + "\n";
    if (!b.startup.empty()) s += "Startup=" + b.startup + "\n";
    for (auto &f : b.modules) s += "Module=" + f + "\n";
    for (auto &f : b.forms) s += "Form=" + f + "\n";
    for (auto &f : b.classes) s += "Class=" + f + "\n";
    for (auto &kv : b.other) s += kv.first + "=" + kv.second + "\n";
    return s;
}

std::string codeBehindOf(const std::string &afg)
{
    size_t dot = afg.rfind('.');
    size_t slash = afg.find_last_of("/:");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return afg + ".bas";
    return afg.substr(0, dot) + ".bas";
}

std::string formOf(const std::string &bas, const std::vector<std::string> &files)
{
    for (auto &f : files) {
        size_t dot = f.rfind('.');
        if (dot == std::string::npos || lower(f.substr(dot + 1)) != "afg") continue;
        if (equalsNoCase(codeBehindOf(f), bas)) return f;
    }
    return "";
}

// --- the boing command ------------------------------------------------------------------------

std::string quoteArg(const std::string &s)
{
    if (!s.empty() && s.find_first_of(" \t\";=*") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += "*\"";
        else if (c == '*') o += "**";
        else if (c == '\n') o += "*N";
        else o += c;
    }
    return o + "\"";
}

std::string checkCommand(const std::string &boing, const std::string &target)
{
    return quoteArg(boing) + " check " + quoteArg(target);
}

std::string buildCommand(const std::string &boing, const std::string &target, const std::string &output, bool release,
                         const std::string &extra)
{
    std::string c = quoteArg(boing) + " build " + quoteArg(target);
    if (!output.empty()) c += " -o " + quoteArg(output);
    if (!release) c += " --debug";
    if (!extra.empty()) c += " " + extra;
    return c;
}

std::string runCommand(const std::string &boing, const std::string &target, const std::string &args,
                       const std::string &errorFile)
{
    std::string c = quoteArg(boing) + " run ";
    if (!errorFile.empty()) c += "--error-file " + quoteArg(errorFile) + " ";
    c += quoteArg(target);
    if (!args.empty()) c += " " + args;
    return c;
}

std::string symbolsCommand(const std::string &boing, const std::string &file)
{
    return quoteArg(boing) + " symbols " + quoteArg(file);
}

bool parseRuntimeError(const std::string &line, std::string &file, int &lineNo, std::string &msg)
{
    std::string l = lower(line);
    size_t re = l.find(": runtime error");
    if (re == std::string::npos) return false;
    // file:line before it; the file may start with a device name ("DH1:x.bas:12")
    size_t c = line.rfind(':', re - 1);
    if (c == std::string::npos) return false;
    std::string num = line.substr(c + 1, re - c - 1);
    if (num.empty() || num.find_first_not_of("0123456789") != std::string::npos) return false;
    file = line.substr(0, c);
    lineNo = std::atoi(num.c_str());
    msg = trimmed(line.substr(re + 2));
    return !file.empty() && lineNo > 0;
}

const std::vector<std::string> &completionWords()
{
    static std::vector<std::string> w;
    if (!w.empty()) return w;
    const char *cased =
        // statements
        "AddressOf And As Alias Append Base Binary Boolean ByRef ByVal Call Case Class Close Compare Const "
        "DefBool DefByte DefCur DefDate DefDbl DefInt DefLng DefObj DefSng DefStr DefVar Interface Version CreateObject "
        "Declare Dim Do Each Else ElseIf Empty End Enum Eqv Erase Error Event Exit Explicit False For Friend "
        "Function Get Global GoSub GoTo If Imp Implements In Input Is Let Lib Like Line Lock Loop Me Mod New "
        "Next Not Nothing Null On Open Option Optional Or Output ParamArray Preserve Print Private Property "
        "Public Put RaiseEvent Random Read ReDim Rem Resume Return Seek Select Set Shared Static Step Stop Sub "
        "Then To True Type TypeOf Unlock Until Wend While With WithEvents Write Xor Debug Err App "
        // builtins
        "Abs Array Asc Atn CBool CByte CCur CDate CDbl ChDir Chr CInt CLng Cos CSng CStr CurDir CVar Date "
        "DateAdd DateDiff DatePart DateSerial Day Dir DoEvents Environ EOF Exp FileCopy FileLen Fix Format "
        "FreeFile Hex Hour IIf InputBox InStr InStrRev Int IsArray IsDate IsEmpty IsNull IsNumeric IsObject Join "
        "Kill LBound LCase Left Len Loc LOF Log LTrim Mid Minute MkDir Month MsgBox Name Now Oct Randomize "
        "Replace Right RmDir Rnd Round RTrim Second Sgn Shell Sin Space Split Sqr Str StrComp StrReverse String "
        "Tan Time Timer TimeSerial Trim TypeName UBound UCase Val VarType Weekday Year Beep Command Load Unload "
        "Show Hide VarPtr StrPtr ObjPtr Collection "
        // types
        "Integer Long Single Double Currency Variant Object Any Byte";
    std::string cur;
    for (const char *p = cased;; p++) {
        if (*p == ' ' || *p == 0) {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else cur.push_back(*p);
    }
    return w;
}

std::string casedWord(const std::string &word)
{
    for (auto &c : completionWords())
        if (equalsNoCase(c, word)) return c;
    return word;
}

std::map<std::string, std::string> formControls(const std::string &afgText)
{
    std::map<std::string, std::string> out;
    gui::Model m;
    std::string err;
    if (!m.parse(afgText, err)) return out;
    out[m.window.get("name")] = "window";
    std::vector<std::pair<gui::Node *, int>> all;
    m.walk(all);
    for (auto &p : all)
        if (p.first->has("id")) out[p.first->get("id")] = gui::kindInfo(p.first->kind).name;
    return out;
}

std::vector<std::string> memberCompletions(const std::string &name, const std::map<std::string, std::string> &ctls)
{
    std::vector<std::string> out;
    std::string kind;
    for (auto &kv : ctls)
        if (equalsNoCase(kv.first, name)) kind = kv.second;
    if (kind.empty()) return out;
    const ControlInfo *ci = controlFor(kind);
    if (!ci) return out;
    for (auto &m : ci->properties) out.push_back(m.name);
    for (auto &m : ci->methods) out.push_back(m.name);
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) { return lower(a) < lower(b); });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace basic
} // namespace af
