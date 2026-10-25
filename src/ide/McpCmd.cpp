// --- mcp --- the portable side of the MCP commands (see McpCmd.h)
#include "McpCmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace af {
namespace mcp {

static const char *hex = "0123456789ABCDEF";

std::string escape(const std::string &s, bool spaces)
{
    std::string o;
    o.reserve(s.size() + s.size() / 8);
    for (unsigned char c : s) {
        switch (c) {
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        case '\r': o += "\\r"; break;
        case '|': o += "\\p"; break;
        case '"': o += "\\q"; break;
        case ' ':
            if (spaces) o += "\\s";
            else o += ' ';
            break;
        default:
            if (c < 0x20 || c >= 0x7f) {
                o += "\\x";
                o += hex[c >> 4];
                o += hex[c & 15];
            } else {
                o += (char)c;
            }
        }
    }
    return o;
}

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string unescape(const std::string &s)
{
    if (s.size() == 3 && s[0] == '\\' && s[1] == '@' && s[2] >= '1' && s[2] <= '9') return s;
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char n = s[++i];
        switch (n) {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case 's': o += ' '; break;
        case 'p': o += '|'; break;
        case 'q': o += '"'; break;
        case 'e': o += '\x1b'; break;
        case '\\': o += '\\'; break;
        case 'x':
            if (i + 2 < s.size() && hexDigit(s[i + 1]) >= 0 && hexDigit(s[i + 2]) >= 0) {
                o += (char)(hexDigit(s[i + 1]) * 16 + hexDigit(s[i + 2]));
                i += 2;
            } else {
                o += "\\x";
            }
            break;
        default: o += '\\'; o += n; break;
        }
    }
    return o;
}

bool resolve(const std::string &value, const std::map<int, std::string> &slots, std::string &out)
{
    if (value.size() == 3 && value[0] == '\\' && value[1] == '@' && value[2] >= '1' && value[2] <= '9') {
        auto it = slots.find(value[2] - '0');
        if (it == slots.end()) return false;
        out = it->second;
        return true;
    }
    out = unescape(value);
    return true;
}

// a chunk boundary that doesn't split an escape sequence
static size_t cut(const std::string &esc, size_t chunk)
{
    if (esc.size() <= chunk) return esc.size();
    size_t i = 0;
    while (i < esc.size()) {
        size_t len = esc[i] != '\\' ? 1 : i + 1 < esc.size() && esc[i + 1] == 'x' ? 4 : 2;
        if (i + len > chunk) break;
        i += len;
    }
    return i;
}

static std::string wrap(int rc, size_t more, const std::string &chunk)
{
    char head[64];
    snprintf(head, sizeof head, "AF1 %d %lu %lu ", rc, (unsigned long)more, (unsigned long)chunk.size());
    return head + chunk;
}

std::string envelope(int rc, const std::string &payload, size_t chunk, std::string &rest)
{
    rest = escape(payload, false);
    std::string first;
    size_t n = cut(rest, chunk);
    first = rest.substr(0, n);
    rest.erase(0, n);
    return wrap(rc, rest.size(), first);
}

std::string nextChunk(std::string &rest, size_t chunk)
{
    size_t n = cut(rest, chunk);
    std::string c = rest.substr(0, n);
    rest.erase(0, n);
    return wrap(0, rest.size(), c);
}

bool parseEnvelope(const std::string &env, int &rc, size_t &more, std::string &payload)
{
    if (env.compare(0, 4, "AF1 ") != 0) return false;
    const char *p = env.c_str() + 4;
    char *e;
    rc = (int)strtol(p, &e, 10);
    if (*e != ' ') return false;
    more = strtoul(e + 1, &e, 10);
    if (*e != ' ') return false;
    size_t len = strtoul(e + 1, &e, 10);
    if (*e != ' ' && *e != 0) return false;
    std::string body = *e ? std::string(e + 1) : std::string();
    if (body.size() > len) return false;
    body.append(len - body.size(), ' ');   // trailing spaces the transport stripped
    payload = unescape(body);
    return true;
}

std::string fieldEscape(const std::string &s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else o += c;
    }
    return o;
}

std::string record(const std::vector<std::string> &fields)
{
    std::string o;
    for (size_t i = 0; i < fields.size(); i++) {
        if (i) o += '\t';
        o += fieldEscape(fields[i]);
    }
    return o;
}

std::string kv(const std::string &key, const std::string &value)
{
    std::string v = value;
    for (auto &c : v) if (c == '\n' || c == '\r') c = ' ';
    return key + "=" + v;
}

int lineCount(const std::string &text)
{
    int n = 1;
    for (char c : text) if (c == '\n') n++;
    return n;
}

// byte offset of the start of line l (1-based); text.size() + 1 if there is no such line
static size_t lineStart(const std::string &text, long l)
{
    if (l <= 1) return 0;
    size_t p = 0;
    for (long k = 1; k < l; k++) {
        p = text.find('\n', p);
        if (p == std::string::npos) return text.size() + 1;
        p++;
    }
    return p;
}

bool lineRange(const std::string &text, long from, long to, size_t &start, size_t &end, std::string &err)
{
    int n = lineCount(text);
    if (from < 1 || from > n + 1) { err = "line " + std::to_string(from) + " is outside the file (it has " + std::to_string(n) + " lines)"; return false; }
    if (to < 0 || to > n) to = n;
    if (to < from - 1) { err = "the range ends before it starts"; return false; }
    if (from == n + 1) {
        if (to != n) { err = "line " + std::to_string(from) + " is past the end"; return false; }
        start = end = text.size();
        return true;
    }
    start = lineStart(text, from);
    if (to == from - 1) { end = start; return true; }
    end = to >= n ? text.size() : lineStart(text, to + 1);
    return true;
}

std::string lines(const std::string &text, long from, long to)
{
    int n = lineCount(text);
    if (from < 1) from = 1;
    if (to < 0 || to > n) to = n;
    if (from > n || to < from) return "";
    size_t s = lineStart(text, from);
    size_t e = to >= n ? text.size() : lineStart(text, to + 1) - 1;   // without the newline
    return text.substr(s, e - s);
}

bool replaceLines(const std::string &text, long from, long to, const std::string &repl,
                  std::string &out, size_t &start, size_t &end, std::string &with, std::string &err)
{
    if (!lineRange(text, from, to, start, end, err)) return false;
    with = repl;
    if (start == text.size() && end == text.size()) {
        // appending at the end of the file
        if (!text.empty() && text.back() != '\n' && !repl.empty()) with = "\n" + with;
    } else if (end < text.size() || (end == text.size() && !text.empty() && text.back() == '\n' && end > start)) {
        // the range ends with a newline: keep the line structure
        if (!with.empty() && with.back() != '\n') with += '\n';
    } else if (end == start && !with.empty() && with.back() != '\n') {
        with += '\n';   // insertion before a line
    }
    out = text.substr(0, start) + with + text.substr(end);
    return true;
}

std::vector<size_t> occurrences(const std::string &text, const std::string &oldText)
{
    std::vector<size_t> out;
    if (oldText.empty()) return out;
    for (size_t p = text.find(oldText); p != std::string::npos; p = text.find(oldText, p + oldText.size()))
        out.push_back(p);
    return out;
}

int substitute(const std::string &text, const std::string &oldText, const std::string &newText, bool all,
               std::string &out, int &firstLine, std::string &err)
{
    if (oldText.empty()) { err = "the text to replace is empty"; return 0; }
    std::vector<size_t> at = occurrences(text, oldText);
    if (at.empty()) { err = "the text to replace isn't in the file"; return 0; }
    if (at.size() > 1 && !all) {
        err = "the text to replace is in the file " + std::to_string(at.size()) +
              " times: give more of the surrounding text, or replace all";
        return 0;
    }
    firstLine = 1;
    for (size_t i = 0; i < at[0]; i++) if (text[i] == '\n') firstLine++;
    out.clear();
    size_t prev = 0;
    for (size_t p : at) {
        out.append(text, prev, p - prev);
        out += newText;
        prev = p + oldText.size();
    }
    out.append(text, prev, std::string::npos);
    return (int)at.size();
}

const char *stopReasonName(int reason)
{
    static const char *names[] = { "breakpoint", "step", "entry", "pause", "exception" };
    return reason >= 0 && reason < 5 ? names[reason] : "unknown";
}

const char *debugStateName(int state)
{
    static const char *names[] = { "none", "loaded", "running", "stopped", "exited" };
    return state >= 0 && state < 5 ? names[state] : "unknown";
}

} // namespace mcp
} // namespace af
// --- end mcp ---
