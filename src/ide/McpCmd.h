// --- mcp ---
// The machine-readable side of the ARexx port, for AI assistants driving AmiForge through the
// amiga-devbench MCP server (assistant -> devbench MCP -> amiga-bridge -> ARexx AMIFORGE -> IDE).
// Portable (tested on the host); the commands themselves are in McpPort.cpp.
//
// The bridge carries an ARexx RESULT as one line and turns '|', newlines and other control
// characters into spaces, drops the RESULT when RC isn't 0, and caps a line at 8 KB. So
// assistants wrap a command in "MCP <command>", which always returns RC 0 and a RESULT
//     AF1 <rc> <more> <length> <payload>
// rc: the command's own return code (0 ok, 5 warning / not found, 10 error, 20 failure);
// more: characters of the payload still to come (fetch them with MCPMORE, which returns the
// same envelope); length: the length of this chunk's payload (the transport strips trailing
// spaces; the receiver pads them back); payload: the result (or the error message when rc
// isn't 0), escaped with escape() below.
//
// Text going the other way (file names, code to insert) is escaped the same way with spaces
// too (\s), so every value is a single word: "MCP SUBST FILE=main.c OLD=x\s=\s1; NEW=x\s=\s2;".
// Long text is sent in pieces into a slot with MCPBUF and referred to as \@1 (\@2...).
//
// Results are lines ("\n"); a line of a list is tab-separated fields, each field escaped with
// fieldEscape() (\t, \n and \\ inside a field); key=value lines for STATE-like results.
#pragma once

#include <string>
#include <vector>
#include <map>

namespace af {
namespace mcp {

// \\ \n \t \r \p (|) \q (") \xHH (other control characters and bytes >= 0x7f); with
// spaces also \s for a space. The result is printable ASCII.
std::string escape(const std::string &s, bool spaces);
// The inverse; also understands \s, \e (ESC) and unknown escapes as themselves. A value that
// is exactly \@N (N = 1..9) is a slot reference: unescape leaves it alone (see resolve).
std::string unescape(const std::string &s);
// A value from the host: \@N -> slots[N], anything else unescaped. False for an unknown slot.
bool resolve(const std::string &value, const std::map<int, std::string> &slots, std::string &out);

// The envelope (see above). `payload` is the raw result; chunk: maximum escaped characters.
// Returns the first chunk and leaves what's left (already escaped) in `rest`.
std::string envelope(int rc, const std::string &payload, size_t chunk, std::string &rest);
// The next chunk of `rest` (consumed).
std::string nextChunk(std::string &rest, size_t chunk);
// Parse an envelope back (for the tests and for documentation): false if it isn't one.
bool parseEnvelope(const std::string &env, int &rc, size_t &more, std::string &payload);

// One field of a tab-separated record: \\ -> \\\\, tab -> \t, newline -> \n, CR dropped.
std::string fieldEscape(const std::string &s);
std::string record(const std::vector<std::string> &fields);   // fieldEscape'd, tab-joined
std::string kv(const std::string &key, const std::string &value);   // "key=value" (value: newlines -> spaces)

// --- line-based text helpers (used on the editor's text) ---
int lineCount(const std::string &text);   // "" = 1, "a\n" = 2 (Scintilla counts the empty last line)
// Byte offsets of lines from..to (1-based, inclusive): [start, end) covers their text and the
// newline after `to` (if any). to = from - 1: an empty range before `from` (an insertion).
// to < 0 or past the end: up to the end. False with a message if the range is impossible.
bool lineRange(const std::string &text, long from, long to, size_t &start, size_t &end, std::string &err);
// Lines from..to of text (inclusive, clamped), without the last newline.
std::string lines(const std::string &text, long from, long to);
// Replace lines from..to with `repl` (whole lines: a newline is added after it unless it ends
// the file). to = from - 1 inserts before `from`; from = count + 1 appends. Returns the new
// text in `out`, the replaced byte range in start/end and the text that replaced it in `with`.
bool replaceLines(const std::string &text, long from, long to, const std::string &repl,
                  std::string &out, size_t &start, size_t &end, std::string &with, std::string &err);
// Exact (case-sensitive) replacement of `oldText` by `newText`. Without all, `oldText` must
// occur exactly once. Returns the number of replacements (0 = error, see err) and the 1-based
// line of the first one.
int substitute(const std::string &text, const std::string &oldText, const std::string &newText, bool all,
               std::string &out, int &firstLine, std::string &err);
// Every non-overlapping occurrence of `oldText` (byte offsets).
std::vector<size_t> occurrences(const std::string &text, const std::string &oldText);

// --- formatting debugger stop reasons ---
const char *stopReasonName(int reason);    // dbg_stop_reason -> "breakpoint", "step", "entry", "pause", "exception"
const char *debugStateName(int state);     // dbg_state -> "none", "loaded", "running", "stopped", "exited"

} // namespace mcp
} // namespace af
// --- end mcp ---
