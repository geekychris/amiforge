// The built-in editor themes.
#include "Theme.h"

#include <cmath>
#include <strings.h>

namespace af {

static const std::vector<Theme> builtIn = {
    {
        "Light", false,
        // text and margins
        0x1A1A1A, 0xFFFFFF,
        0x7A7A7A, 0xF0F0F0,
        0xF7F7F7, 0xFFFFFF, 0x909090,
        0xD0D0D0, 0x1A1A1A,
        // syntax
        0x3F8F3F, 0x4F7F9F, 0x0033B3, 0x007A7A, 0xB8261A, 0xA85E00, 0x8A2E9E, 0x806000,
        0xB00060, 0x505050, 0xC00000,
        0xFFE0E0,
        // braces
        0x0033B3, 0xC8E0FF, 0xC00000, 0xFFD0D0,
        // selection, caret
        0xB4D5FE, 0xDCDCDC, 0x000000,
        0x142060FF,
        0xD4E6FE, 0x404040,
        // calltips
        0x303030, 0xFFFFE8, 0x0033B3,
        // list
        0x000000, 0xFFFFFF, 0xFFFFFF, 0x3366CC,
        // markers
        0x9A1010, 0xE02828, 0xC03030,
        0x8A6A00, 0xFFD400, 0xFFF3A8,
        0xFFD8D8, 0xFFEACC,
        0x1E6E1E, 0x48C048,
        // other lexers
        0xEEF2FF, 0xF4ECF8,
        0xA00000, 0xFFECEC, 0x006000, 0xEAFBEA, 0x805000, 0xFFF6DC,
        // editor features
        0xC8D8F0, 0x6090E0, 0xFFC040,
        // AutoDocs
        0x204080, 0x0050C0, 0x202020,
        // --- lang --- function names, decorators, builtins, variables, headings, emphasis, code, errors
        0x795E26, 0x6F42C1, 0x1F6F8B, 0x0060A8, 0x1F4E9A, 0x7A3E9D, 0xF2F2F2, 0xC00000,
    },
    {
        "Dark", true,
        // text and margins
        0xD4D4D4, 0x1E1F22,
        0x6E7681, 0x26282C,
        0x2A2C30, 0x26282C, 0x8A9099,
        0x3A3D42, 0xD4D4D4,
        // syntax
        0x6A9955, 0x7A9EC2, 0x569CD6, 0x4EC9B0, 0xCE9178, 0xB5CEA8, 0xC586C0, 0xD4C27A,
        0xE08ACB, 0xA0A0A0, 0xFF6060,
        0x4A2020,
        // braces
        0xFFFFFF, 0x2F4F7F, 0xFF8080, 0x5A2020,
        // selection, caret
        0x264F78, 0x3A3D41, 0xF0F0F0,
        0x28FFFFFF,
        0x1F3A5A, 0xC0C0C0,
        // calltips
        0xE0E0E0, 0x2D3036, 0x6CB6FF,
        // list
        0xD4D4D4, 0x25262A, 0xFFFFFF, 0x094771,
        // markers
        0xFF8080, 0xD03030, 0xE06060,
        0xFFE070, 0xC9A000, 0x4A4210,
        0x4A1E1E, 0x4A3A16,
        0x80E080, 0x2E8B2E,
        // other lexers
        0x262E40, 0x30283A,
        0xFF9090, 0x3A1E1E, 0x90E090, 0x1E3A1E, 0xE0C080, 0x3A321E,
        // editor features
        0x3A4660, 0x4A7AC0, 0xB08020,
        // AutoDocs
        0x8CB4E6, 0x6CB6FF, 0xE8E8E8,
        // --- lang ---
        0xDCDCAA, 0xB8A1F0, 0x4FC1FF, 0x9CDCFE, 0x7CC4FF, 0xD7BA7D, 0x2A2D32, 0xFF6A6A,
    },
};

const std::vector<Theme> &themes() { return builtIn; }

const Theme &themeNamed(const std::string &name)
{
    for (auto &t : builtIn)
        if (strcasecmp(t.name, name.c_str()) == 0) return t;
    return builtIn.front();
}

static const Theme *current = &builtIn.front();

const Theme &currentTheme() { return *current; }
void setCurrentTheme(const std::string &name) { current = &themeNamed(name); }

static double luminance(uint32_t rgb)
{
    auto chan = [](uint32_t c) {
        double v = c / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * chan((rgb >> 16) & 0xFF) + 0.7152 * chan((rgb >> 8) & 0xFF) + 0.0722 * chan(rgb & 0xFF);
}

double contrastRatio(uint32_t a, uint32_t b)
{
    double la = luminance(a & 0xFFFFFF), lb = luminance(b & 0xFFFFFF);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

} // namespace af
