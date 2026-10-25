// The hex editor's model (see HexData.h).
#include "HexData.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

namespace af {
namespace hex {

static bool printable(unsigned char c) { return (c >= 0x20 && c < 0x7F) || c >= 0xA0; }

std::string formatLine(const std::string &bytes, size_t lineStart)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%08lX  ", (unsigned long)lineStart);
    std::string s = buf;
    static const char *dig = "0123456789ABCDEF";
    for (int i = 0; i < kBytesPerLine; i++) {
        size_t o = lineStart + i;
        if (o < bytes.size()) {
            unsigned char c = bytes[o];
            s += dig[c >> 4];
            s += dig[c & 15];
        } else {
            s += "  ";
        }
        s += ' ';
        if (i == 7) s += ' ';
    }
    s += '|';
    for (int i = 0; i < kBytesPerLine; i++) {
        size_t o = lineStart + i;
        if (o < bytes.size()) {
            unsigned char c = bytes[o];
            s += printable(c) ? (char)c : '.';
        } else {
            s += ' ';
        }
    }
    s += '|';
    return s;
}

std::string formatAll(const std::string &bytes)
{
    std::string out;
    size_t lines = lineCount(bytes.size());
    out.reserve(lines * (kLineLen + 1));
    for (size_t l = 0; l < lines; l++) {
        if (l) out += '\n';
        out += formatLine(bytes, l * kBytesPerLine);
    }
    return out;
}

int columnOf(int byte, Area area, int nibble)
{
    if (area == Area::Text) return kTextCol + byte;
    if (area == Area::Offset) return 0;
    return kHexCol + byte * 3 + (byte >= 8 ? 1 : 0) + nibble;
}

Hit hitTest(int col)
{
    Hit h;
    if (col < kHexCol) { h.area = Area::Hex; return h; }   // the offset column: snap to the line's first byte
    if (col >= kTextCol - 1) {
        h.area = Area::Text;
        int b = col - kTextCol;
        h.byte = b < 0 ? 0 : b >= kBytesPerLine ? kBytesPerLine - 1 : b;
        return h;
    }
    h.area = Area::Hex;
    int c = col - kHexCol;
    if (c >= 8 * 3) c--;                     // the extra space after 8 bytes
    if (c < 0) c = 0;
    int b = c / 3, r = c % 3;
    if (b >= kBytesPerLine) { b = kBytesPerLine - 1; r = 1; }
    h.byte = b;
    h.nibble = r >= 1 ? 1 : 0;
    if (r == 2) { h.byte = b + 1 < kBytesPerLine ? b + 1 : b; h.nibble = b + 1 < kBytesPerLine ? 0 : 1; }   // the space: next byte
    return h;
}

static int hexVal(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool typeChar(std::string &bytes, size_t &offset, Area area, int &nibble, int ch, int &oldByte)
{
    if (offset > bytes.size()) return false;
    if (area == Area::Hex) {
        int v = hexVal(ch);
        if (v < 0) return false;
        bool append = offset == bytes.size();
        oldByte = append ? -1 : (unsigned char)bytes[offset];
        unsigned char b = append ? 0 : (unsigned char)bytes[offset];
        b = nibble == 0 ? (unsigned char)((v << 4) | (b & 15)) : (unsigned char)((b & 0xF0) | v);
        if (append) bytes += (char)b;
        else bytes[offset] = (char)b;
        if (nibble == 0) nibble = 1;
        else { nibble = 0; offset++; }
        return true;
    }
    if (area == Area::Text) {
        if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0) || ch > 0xFF) return false;
        bool append = offset == bytes.size();
        oldByte = append ? -1 : (unsigned char)bytes[offset];
        if (append) bytes += (char)ch;
        else bytes[offset] = (char)ch;
        offset++;
        nibble = 0;
        return true;
    }
    return false;
}

std::string parsePattern(const std::string &p)
{
    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') return p.substr(1, p.size() - 2);
    std::string digits;
    std::string s = p;
    if (s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0) s = s.substr(2);
    bool hexy = !s.empty();
    for (char c : s) {
        if (c == ' ' || c == ',') continue;
        if (hexVal((unsigned char)c) < 0) { hexy = false; break; }
        digits += c;
    }
    if (hexy && !digits.empty() && digits.size() % 2 == 0) {
        std::string out;
        for (size_t i = 0; i < digits.size(); i += 2) out += (char)(hexVal(digits[i]) * 16 + hexVal(digits[i + 1]));
        return out;
    }
    return p;
}

size_t find(const std::string &bytes, const std::string &pattern, size_t from)
{
    if (pattern.empty() || bytes.size() < pattern.size()) return std::string::npos;
    if (from > bytes.size()) from = 0;
    size_t r = bytes.find(pattern, from);
    if (r == std::string::npos && from > 0) r = bytes.find(pattern, 0);
    return r;
}

bool looksBinary(const std::string &d)
{
    size_t n = d.size() < 8192 ? d.size() : 8192;
    if (n == 0) return false;
    size_t odd = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = d[i];
        if (c == 0) return true;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != 0x1B && c != '\b') odd++;
    }
    return odd * 20 > n;   // more than 5% control characters
}

bool parseOffset(const std::string &in, size_t &out)
{
    std::string s;
    for (char c : in) if (c != ' ') s += c;
    if (s.empty()) return false;
    int base = 16;
    if (s[0] == '#') { base = 10; s = s.substr(1); }
    else if (s.back() == 'd' || s.back() == 'D') { base = 10; s.pop_back(); }
    else if (s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0) s = s.substr(2);
    else if (s[0] == '$') s = s.substr(1);
    if (s.empty()) return false;
    char *e;
    unsigned long v = strtoul(s.c_str(), &e, base);
    if (*e) return false;
    out = v;
    return true;
}

} // namespace hex
} // namespace af
