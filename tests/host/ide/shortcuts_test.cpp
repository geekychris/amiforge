// The shortcuts: no Amiga key on two menu items (Intuition only ever runs the first, so the
// other silently never works), and no function key bound twice in KeyBindings.cpp. Reads the
// tables from the source; the tests run in tests/host.
#include "aftest.h"
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

static std::string readFile(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// key -> the labels of the menu items that have it
static std::map<std::string, std::vector<std::string>> menuKeys(const std::string &src)
{
    std::map<std::string, std::vector<std::string>> keys;
    // ITEM("label", "K", CMD) / SUB(...), and raw { NM_ITEM, (STRPTR)"label", (STRPTR)"K", ... }
    std::regex macro(R"re(\b(?:ITEM|SUB)\(\s*"([^"]*)"\s*,\s*"([^"]+)")re");
    std::regex raw(R"re(\{\s*NM_(?:ITEM|SUB)\s*,\s*\(STRPTR\)"([^"]*)"\s*,\s*\(STRPTR\)"([^"]+)")re");
    for (const std::regex *re : { &macro, &raw })
        for (std::sregex_iterator i(src.begin(), src.end(), *re), e; i != e; ++i) {
            std::string k = (*i)[2];
            for (auto &c : k) c = (char)toupper((unsigned char)c);   // Intuition ignores case
            keys[k].push_back((*i)[1]);
        }
    return keys;
}

TEST(menu_table_is_found)
{
    auto keys = menuKeys(readFile("../../src/ide/MainWindow.cpp"));
    CHECK(keys.size() > 20);       // the parser still understands the table
    CHECK(keys.count("S") == 1);   // Project > Save
}

TEST(no_amiga_key_on_two_menu_items)
{
    for (auto &kv : menuKeys(readFile("../../src/ide/MainWindow.cpp")))
        if (kv.second.size() > 1) {
            std::string items;
            for (auto &l : kv.second) items += (items.empty() ? "" : ", ") + l;
            aftest::fail(__FILE__, __LINE__, "Amiga-" + kv.first + " is on " + items);
        }
}

TEST(debug_start_has_amiga_d)
{
    auto keys = menuKeys(readFile("../../src/ide/MainWindow.cpp"));
    CHECK(keys["D"].size() == 1 && keys["D"][0].rfind("Start / Continue", 0) == 0);
}

TEST(no_function_key_bound_twice)
{
    std::string src = readFile("../../src/ide/KeyBindings.cpp");
    std::regex row(R"re(\{\s*(RAW_\w+)\s*,\s*([^,]+?)\s*,\s*(CMD_\w+))re");
    std::map<std::string, std::string> seen;
    int n = 0;
    for (std::sregex_iterator i(src.begin(), src.end(), row), e; i != e; ++i, ++n) {
        std::string k = std::string((*i)[1]) + " + " + std::string((*i)[2]);
        if (seen.count(k)) aftest::fail(__FILE__, __LINE__, k + " is bound to " + seen[k] + " and " + std::string((*i)[3]));
        seen[k] = (*i)[3];
    }
    CHECK(n > 10);
}
