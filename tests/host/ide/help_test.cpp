// Help > User Manual: the AmigaGuide first, then the text version, in AmiForge's Docs drawer.
#include "aftest.h"
#include "HelpManual.h"

#include <set>

using namespace af::help;

TEST(help_manual_candidates)
{
    auto c = manualCandidates();
    CHECK_EQ(c.size(), (size_t)2);
    CHECK_EQ(c[0], std::string("PROGDIR:Docs/AmiForge-Manual.guide"));
    CHECK_EQ(c[1], std::string("PROGDIR:Docs/AmiForge-Manual.txt"));
    CHECK_EQ(manualCandidates("Work:AmiForge")[0], std::string("Work:AmiForge/Docs/AmiForge-Manual.guide"));
    CHECK_EQ(manualCandidates("Work:")[0], std::string("Work:Docs/AmiForge-Manual.guide"));
}

TEST(help_find_manual)
{
    std::set<std::string> there;
    auto exists = [&](const std::string &p) { return there.count(p) > 0; };
    CHECK_EQ(findManual(exists), std::string(""));
    there.insert("PROGDIR:Docs/AmiForge-Manual.txt");
    CHECK_EQ(findManual(exists), std::string("PROGDIR:Docs/AmiForge-Manual.txt"));
    there.insert("PROGDIR:Docs/AmiForge-Manual.guide");
    CHECK_EQ(findManual(exists), std::string("PROGDIR:Docs/AmiForge-Manual.guide"));
}
