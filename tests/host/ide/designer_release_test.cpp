// Release packaging: Aminet readme, validation, install script, flags.
#include "aftest.h"
#include "ReleaseGen.h"

using namespace af::rel;

static bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

static Info sample()
{
    Info i;
    i.name = "Bouncer";
    i.program = "bouncer";
    i.version = "1.2";
    i.date = "25.10.2026";
    i.author = "Jane Doe <jane@example.org>";
    i.shortDesc = "Bouncing balls in a window";
    i.type = "demo/misc";
    i.description = "Balls bounce around a window.\nClick to add more.";
    return i;
}

TEST(readme)
{
    std::string r = aminetReadme(sample());
    CHECK(r.compare(0, 41, "Short:        Bouncing balls in a window\n") == 0);
    CHECK(contains(r, "Author:       Jane Doe <jane@example.org>\n"));
    CHECK(contains(r, "Uploader:     Jane Doe <jane@example.org>\n"));
    CHECK(contains(r, "Type:         demo/misc\n"));
    CHECK(contains(r, "Version:      1.2\n"));
    CHECK(contains(r, "Architecture: ppc-amigaos >= 4.1.0\n"));
    CHECK(!contains(r, "Replaces:"));
    CHECK(contains(r, "\n\nBalls bounce around a window.\nClick to add more.\n"));
    CHECK(contains(r, "1.2 (25.10.2026)  First release."));
}

TEST(validation)
{
    CHECK(validate(sample()).empty());
    Info i = sample();
    i.shortDesc = "This short description is far too long for Aminet's rules";
    i.author = "Jane";
    i.type = "nonsense";
    i.version = "v1";
    CHECK_EQ((int)validate(i).size(), 4);
    i = sample();
    i.type = "util/";
    CHECK_EQ((int)validate(i).size(), 1);
    i.type = "zzz/misc";
    CHECK_EQ((int)validate(i).size(), 1);
}

TEST(names_and_versions)
{
    CHECK_EQ(versionString(sample()), std::string("$VER: Bouncer 1.2 (25.10.2026)"));
    CHECK_EQ(archiveBase(sample()), std::string("Bouncer12"));
    Info i = sample();
    i.name = "My Tool!";
    i.version = "";
    CHECK_EQ(archiveBase(i), std::string("MyTool"));
}

TEST(install_script)
{
    std::string s = installScript(sample());
    CHECK(s.compare(0, 10, ".key DEST\n") == 0);
    CHECK(contains(s, "Copy >NIL: \"Bouncer\" \"$dest/Bouncer\" ALL CLONE QUIET"));
    CHECK(contains(s, "If EXISTS \"$dest/Bouncer/bouncer\""));
    CHECK(contains(s, "RequestFile"));
}

TEST(release_flags)
{
    CHECK_EQ(releaseFlags("-O0 -gstabs -Wall"), std::string("-O2 -Wall"));
    CHECK_EQ(releaseFlags("-gstabs+ -Wall -DDEBUG -O3"), std::string("-Wall -O3"));
    CHECK_EQ(releaseFlags("-Wall"), std::string("-O2 -Wall"));
    CHECK_EQ(releaseFlags(""), std::string("-O2"));
}
