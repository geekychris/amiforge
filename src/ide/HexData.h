// The hex editor's model: formatting bytes as "offset | hex | text" lines, mapping editor
// columns to bytes, overtype edits, searching. Portable (tested on the host).
//
//   00000000  48 65 6C 6C 6F 2C 20 41  6D 69 67 61 21 0A 00 FF  |Hello, Amiga!...|
//   ^offset   ^hex (col 10)                                     ^text (col 60)
#pragma once

#include <string>
#include <vector>
#include <stdint.h>

namespace af {
namespace hex {

const int kBytesPerLine = 16;
const int kHexCol = 10;          // first hex digit
const int kTextCol = 60;         // first character of the text column (after '|')
const int kLineLen = 77;         // without the newline

std::string formatLine(const std::string &bytes, size_t lineStart);
std::string formatAll(const std::string &bytes);
inline size_t lineCount(size_t size) { return size / kBytesPerLine + 1; }   // the last line holds the append position

// Where a column of a line is.
enum class Area { Offset, Hex, Text };
struct Hit {
    Area area = Area::Offset;
    int byte = 0;       // 0..15 within the line
    int nibble = 0;     // 0 = high, 1 = low (hex area)
};
Hit hitTest(int column);
int columnOf(int byte, Area area, int nibble);   // the inverse

// Type `ch` at byte `offset`: a hex digit in the hex area sets that nibble, any character in
// the text area sets the byte. Typing at offset == size appends a byte. Returns false if the
// key does nothing there. On success `offset`/`nibble` move to the next position and
// `oldByte` is what was there (-1 if the byte was appended).
bool typeChar(std::string &bytes, size_t &offset, Area area, int &nibble, int ch, int &oldByte);

// "4E 71", "4e71", "0x4E71" -> bytes; "\"text\"" or anything else -> its characters.
std::string parsePattern(const std::string &pattern);
// Next occurrence at or after `from` (wrapping round); npos if none.
size_t find(const std::string &bytes, const std::string &pattern, size_t from);

// Heuristic: does this look like a binary file rather than text?
bool looksBinary(const std::string &data);

// "1A2B", "0x1A2B", "$1A2B" (hex) or "#1234" / "1234d" (decimal) -> offset. False if not a number.
bool parseOffset(const std::string &s, size_t &out);

} // namespace hex
} // namespace af
