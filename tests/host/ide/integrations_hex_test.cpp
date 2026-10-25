// The hex editor's model.
#include "aftest.h"
#include "ide/HexData.h"

using namespace af;

TEST(hex_format_line)
{
    std::string b = "Hello, Amiga!\n";
    b += '\0';
    b += '\xFF';
    std::string l = hex::formatLine(b, 0);
    CHECK_EQ(l, std::string("00000000  48 65 6C 6C 6F 2C 20 41  6D 69 67 61 21 0A 00 FF |Hello, Amiga!..\xFF|"));
    CHECK_EQ((int)l.size(), hex::kLineLen);
    // a partial last line keeps the layout
    std::string p = hex::formatLine("AB", 0);
    CHECK_EQ((int)p.size(), hex::kLineLen);
    CHECK_EQ(p.substr(hex::kTextCol - 1), std::string("|AB              |"));
    CHECK_EQ(hex::formatLine(std::string(40, 'x'), 32).substr(0, 8), std::string("00000020"));
}

TEST(hex_format_all)
{
    CHECK_EQ(hex::formatAll(""), hex::formatLine("", 0));
    std::string b(32, 'z');
    std::string all = hex::formatAll(b);
    // 32 bytes = 2 full lines + the append line
    int nl = 0;
    for (char c : all) nl += c == '\n';
    CHECK_EQ(nl, 2);
    CHECK_EQ(hex::lineCount(32), (size_t)3);
    CHECK_EQ(hex::lineCount(31), (size_t)2);
}

TEST(hex_columns_round_trip)
{
    for (int b = 0; b < 16; b++) {
        for (int n = 0; n < 2; n++) {
            hex::Hit h = hex::hitTest(hex::columnOf(b, hex::Area::Hex, n));
            CHECK(h.area == hex::Area::Hex && h.byte == b && h.nibble == n);
        }
        hex::Hit t = hex::hitTest(hex::columnOf(b, hex::Area::Text, 0));
        CHECK(t.area == hex::Area::Text && t.byte == b);
    }
    // every hex column matches the digit the formatter put there
    std::string bytes;
    for (int i = 0; i < 16; i++) bytes += (char)(i * 17);
    std::string line = hex::formatLine(bytes, 0);
    for (int b = 0; b < 16; b++) {
        char hi = line[hex::columnOf(b, hex::Area::Hex, 0)], lo = line[hex::columnOf(b, hex::Area::Hex, 1)];
        CHECK(hi == lo);   // i*17 = 0x00, 0x11, 0x22...
    }
    CHECK(hex::hitTest(3).area == hex::Area::Hex);      // offset column snaps to byte 0
    CHECK_EQ(hex::hitTest(3).byte, 0);
}

TEST(hex_typing)
{
    std::string b = "ABCD";
    size_t off = 1;
    int nib = 0, old = 0;
    CHECK(hex::typeChar(b, off, hex::Area::Hex, nib, 'f', old));
    CHECK_EQ(old, (int)'B');
    CHECK_EQ(nib, 1);
    CHECK_EQ(off, (size_t)1);
    CHECK(hex::typeChar(b, off, hex::Area::Hex, nib, '0', old));
    CHECK_EQ((unsigned char)b[1], 0xF0);
    CHECK_EQ(off, (size_t)2);
    CHECK_EQ(nib, 0);
    CHECK(!hex::typeChar(b, off, hex::Area::Hex, nib, 'g', old));
    CHECK(hex::typeChar(b, off, hex::Area::Text, nib, 'z', old));
    CHECK_EQ(b, std::string("A\xF0zD"));
    // append at the end
    off = 4;
    CHECK(hex::typeChar(b, off, hex::Area::Text, nib, '!', old));
    CHECK_EQ(old, -1);
    CHECK_EQ(b, std::string("A\xF0zD!"));
    off = 5; nib = 0;
    CHECK(hex::typeChar(b, off, hex::Area::Hex, nib, '7', old));
    CHECK_EQ(b.size(), (size_t)6);
    CHECK_EQ((unsigned char)b[5], 0x70);
    CHECK(!hex::typeChar(b, off = 99, hex::Area::Hex, nib, '1', old));
}

TEST(hex_save_is_byte_exact)
{
    // editing one byte never touches the others: format -> edit -> compare
    std::string orig;
    for (int i = 0; i < 300; i++) orig += (char)(i * 7 + 3);
    std::string b = orig;
    size_t off = 123;
    int nib = 0, old;
    hex::typeChar(b, off, hex::Area::Hex, nib, '5', old);
    hex::typeChar(b, off, hex::Area::Hex, nib, 'a', old);
    for (size_t i = 0; i < orig.size(); i++)
        if (i != 123) CHECK(b[i] == orig[i]);
    CHECK_EQ((unsigned char)b[123], 0x5A);
    CHECK_EQ(b.size(), orig.size());
}

TEST(hex_patterns_and_find)
{
    CHECK_EQ(hex::parsePattern("4E 75"), std::string("\x4E\x75"));
    CHECK_EQ(hex::parsePattern("4e75"), std::string("\x4E\x75"));
    CHECK_EQ(hex::parsePattern("0x4E75"), std::string("\x4E\x75"));
    CHECK_EQ(hex::parsePattern("\"4E75\""), std::string("4E75"));
    CHECK_EQ(hex::parsePattern("FORM"), std::string("FORM"));
    CHECK_EQ(hex::parsePattern("abc"), std::string("abc"));        // odd number of digits: text
    std::string data = "xxFORMyyFORM";
    CHECK_EQ(hex::find(data, "FORM", 0), (size_t)2);
    CHECK_EQ(hex::find(data, "FORM", 3), (size_t)8);
    CHECK_EQ(hex::find(data, "FORM", 9), (size_t)2);   // wraps
    CHECK_EQ(hex::find(data, "nope", 0), std::string::npos);
}

TEST(hex_binary_detection)
{
    CHECK(!hex::looksBinary("int main(void)\n{\n\treturn 0;\n}\n"));
    CHECK(!hex::looksBinary("Latin-1: \xE9\xE8\xFC\n"));
    CHECK(hex::looksBinary(std::string("\x7F" "ELF\x01\x02\x01\0\0", 9)));
    CHECK(hex::looksBinary(std::string("FORM\0\0\0\x20ILBM", 12)));
    std::string ctl(100, '\x01');
    CHECK(hex::looksBinary(ctl));
    CHECK(!hex::looksBinary(""));
}

TEST(hex_offsets)
{
    size_t o;
    CHECK(hex::parseOffset("1A", o) && o == 0x1A);
    CHECK(hex::parseOffset("0x100", o) && o == 0x100);
    CHECK(hex::parseOffset("$ff", o) && o == 0xFF);
    CHECK(hex::parseOffset("#100", o) && o == 100);
    CHECK(hex::parseOffset("100d", o) && o == 100);
    CHECK(!hex::parseOffset("xyz", o));
    CHECK(!hex::parseOffset("", o));
}
