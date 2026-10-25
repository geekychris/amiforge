// The project browser's model: scanning a fake projects folder, descriptions, sorting, recent.
#include "aftest.h"
#include "ProjectIndex.h"

#include <map>

using namespace af::pidx;

namespace {
// A fake file system: paths ending in '/' are drawers, the rest files with their text.
struct FakeFs {
    std::map<std::string, std::string> files;   // full path -> text
    std::map<std::string, std::vector<DirItem>> dirs;
    int listCalls = 0;
    void dir(const std::string &path) { dirs[path]; }
    void file(const std::string &path, const std::string &text = "")
    {
        files[path] = text;
        dirs[parent(path)].push_back({leaf(path), false});
    }
    void sub(const std::string &path)
    {
        dirs[path];
        dirs[parent(path)].push_back({leaf(path), true});
    }
    ListDir lister()
    {
        return [this](const std::string &p, std::vector<DirItem> &out) {
            listCalls++;
            auto it = dirs.find(p);
            if (it == dirs.end()) return false;
            out = it->second;
            return true;
        };
    }
    ReadFile reader()
    {
        return [this](const std::string &p, std::string &out) {
            auto it = files.find(p);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
    }
};

FakeFs examples()
{
    FakeFs fs;
    fs.dir("DH1:Ex");
    fs.sub("DH1:Ex/clock");
    fs.file("DH1:Ex/clock/clock.afp", "[project]\nname=clock\ndescription=An analog clock\\nwith timer.device\n");
    fs.file("DH1:Ex/clock/clock.cpp");
    fs.sub("DH1:Ex/clock/obj");
    fs.sub("DH1:Ex/clock/obj/deep");
    fs.file("DH1:Ex/clock/obj/deep/x.afp", "[project]\nname=hidden\n");
    fs.sub("DH1:Ex/games");
    fs.sub("DH1:Ex/games/bouncer");
    fs.file("DH1:Ex/games/bouncer/bouncer.afp", "[project]\nname=Bouncer\n");
    fs.file("DH1:Ex/games/bouncer/README", "\n# Bouncer\n\nBalls.\n");
    fs.sub("DH1:Ex/games/far");
    fs.sub("DH1:Ex/games/far/away");
    fs.file("DH1:Ex/games/far/away/deep.afp", "[project]\nname=deep\n");
    fs.sub("DH1:Ex/hello");
    fs.file("DH1:Ex/hello/hello.afp", "# no name\n[project]\noutput=hello\n[files]\nfile=main.c\n");
    fs.file("DH1:Ex/hello/readme.txt", "-----\nHello: a first program to step through\n");
    fs.sub("DH1:Ex/empty");
    return fs;
}
}

TEST(paths)
{
    CHECK_EQ(join("DH1:", "x"), std::string("DH1:x"));
    CHECK_EQ(join("DH1:a", "x"), std::string("DH1:a/x"));
    CHECK_EQ(join("DH1:a/", "x"), std::string("DH1:a/x"));
    CHECK_EQ(join("", "x"), std::string("x"));
    CHECK_EQ(parent("DH1:a/b/c.afp"), std::string("DH1:a/b"));
    CHECK_EQ(parent("DH1:a"), std::string("DH1:"));
    CHECK_EQ(parent("DH1:a/"), std::string("DH1:"));
    CHECK_EQ(parent("PROGDIR:Examples"), std::string("PROGDIR:"));
    CHECK_EQ(leaf("DH1:a/b.afp"), std::string("b.afp"));
    CHECK_EQ(leaf("DH1:b"), std::string("b"));
    CHECK(isInside("DH1:Ex/clock", "DH1:Ex"));
    CHECK(isInside("dh1:ex/CLOCK", "DH1:Ex"));
    CHECK(isInside("DH1:Ex", "DH1:Ex/"));
    CHECK(isInside("DH1:Ex/a", "DH1:"));
    CHECK(!isInside("DH1:Examples2/a", "DH1:Examples"));
    CHECK(!isInside("DH2:Ex/a", "DH1:Ex"));
    CHECK(!isInside("", "DH1:Ex"));
}

TEST(afp_description_first_line)
{
    CHECK_EQ(afpDescription("[project]\ndescription=One\\nTwo\n"), std::string("One"));
    CHECK_EQ(afpDescription("[project]\ndescription=\\n\\n  Later line\\nmore\n"), std::string("Later line"));
    CHECK_EQ(afpDescription("[project]\ndescription=back\\\\slash\n"), std::string("back\\slash"));
    CHECK_EQ(afpDescription("[project]\nname=x\n"), std::string(""));
    // only [project] counts
    CHECK_EQ(afpDescription("[files]\ndescription=no\n[project]\nname=x\n"), std::string(""));
    CHECK_EQ(afpName("# c\n[project]\n name = My Prog \n"), std::string("My Prog"));
}

TEST(readme_line)
{
    CHECK_EQ(readmeLine("\n\n# Title here\nbody\n"), std::string("Title here"));
    CHECK_EQ(readmeLine("=====\n----\n  plain text  \n"), std::string("plain text"));
    CHECK_EQ(readmeLine(""), std::string(""));
    CHECK_EQ(readmeLine("#\n##\nX\n"), std::string("X"));
    std::string longLine(150, 'a');
    std::string r = readmeLine(longLine, 20);
    CHECK_EQ(r.size(), (size_t)20);
    CHECK_EQ(r.substr(17), std::string("..."));
    CHECK(isReadme("README"));
    CHECK(isReadme("ReadMe.md"));
    CHECK(isReadme("readme.txt"));
    CHECK(!isReadme("README.info"));
    CHECK(!isReadme("readme.c"));
}

TEST(scan_two_levels_sorted)
{
    FakeFs fs = examples();
    auto found = scan("DH1:Ex", 2, fs.lister(), fs.reader());
    CHECK_EQ(found.size(), (size_t)3);
    if (found.size() != 3) return;
    // case-insensitive by name: Bouncer, clock, hello
    CHECK_EQ(found[0].name, std::string("Bouncer"));
    CHECK_EQ(found[0].drawer, std::string("games/bouncer"));
    CHECK_EQ(found[0].description, std::string("Bouncer"));          // README heading
    CHECK_EQ(found[0].afp, std::string("DH1:Ex/games/bouncer/bouncer.afp"));
    CHECK_EQ(found[1].name, std::string("clock"));
    CHECK_EQ(found[1].description, std::string("An analog clock"));  // description= wins
    CHECK_EQ(found[2].name, std::string("hello"));                    // no name=: the file name
    CHECK_EQ(found[2].description, std::string("Hello: a first program to step through"));
}

TEST(scan_depth_and_skips)
{
    FakeFs fs = examples();
    auto one = scan("DH1:Ex", 1, fs.lister(), fs.reader());
    CHECK_EQ(one.size(), (size_t)2);   // clock, hello (games/bouncer is 2 deep)
    auto three = scan("DH1:Ex", 3, fs.lister(), fs.reader());
    CHECK_EQ(three.size(), (size_t)4); // + games/far/away/deep; never clock/obj/deep
    for (auto &p : three) CHECK(p.name != "hidden");
    CHECK(scan("DH1:Missing", 2, fs.lister(), fs.reader()).empty());
    CHECK(scan("", 2, fs.lister(), fs.reader()).empty());
}

TEST(scan_root_is_a_project)
{
    FakeFs fs;
    fs.dir("RAM:p");
    fs.file("RAM:p/b.afp", "[project]\nname=b\n");
    fs.file("RAM:p/A.afp", "[project]\nname=A\n");
    auto found = scan("RAM:p", 2, fs.lister(), fs.reader());
    CHECK_EQ(found.size(), (size_t)2);
    if (found.size() == 2) {
        CHECK_EQ(found[0].name, std::string("A"));
        CHECK_EQ(found[0].drawer, std::string(""));
    }
}

TEST(same_name_sorted_by_drawer)
{
    FakeFs fs;
    fs.dir("RAM:p");
    fs.sub("RAM:p/z");
    fs.file("RAM:p/z/demo.afp", "[project]\nname=demo\n");
    fs.sub("RAM:p/a");
    fs.file("RAM:p/a/demo.afp", "[project]\nname=demo\n");
    auto found = scan("RAM:p", 2, fs.lister(), fs.reader());
    CHECK_EQ(found.size(), (size_t)2);
    if (found.size() == 2) CHECK_EQ(found[0].drawer, std::string("a"));
}

TEST(recent_merge)
{
    FakeFs fs = examples();
    auto rec = recent({ "DH1:Ex/clock/clock.afp", "DH1:Gone/gone.afp", "DH1:Ex/hello/hello.afp" }, fs.lister(), fs.reader());
    CHECK_EQ(rec.size(), (size_t)2);   // the missing one is left out, the order kept
    if (rec.size() == 2) {
        CHECK_EQ(rec[0].name, std::string("clock"));
        CHECK_EQ(rec[0].drawer, std::string("DH1:Ex/clock"));
        CHECK_EQ(rec[1].description, std::string("Hello: a first program to step through"));
    }
    auto found = scan("DH1:Ex", 2, fs.lister(), fs.reader());
    auto r = rows(rec, found, "DH1:Ex");
    CHECK_EQ(r.size(), (size_t)(1 + 2 + 1 + 3));
    CHECK(r[0].heading);
    CHECK_EQ(r[0].cols[0], std::string("Recent"));
    CHECK_EQ(r[1].afp, std::string("DH1:Ex/clock/clock.afp"));
    CHECK(r[3].heading);
    CHECK_EQ(r[3].cols[0], std::string("Projects in DH1:Ex"));
    CHECK_EQ(r[4].cols[2], std::string("games/bouncer"));
    CHECK(r[4].afp.size() > 0);
    // no recent: no Recent heading; nothing found: a note row without an afp
    auto r2 = rows({}, {}, "RAM:");
    CHECK_EQ(r2.size(), (size_t)2);
    CHECK(r2[1].heading);
    CHECK(r2[1].afp.empty());
}

TEST(open_start_drawer)
{
    CHECK_EQ(openStart("DH1:Ex", "", "RAM:"), std::string("DH1:Ex"));
    CHECK_EQ(openStart("DH1:Ex", "DH1:Ex/clock", "RAM:"), std::string("DH1:Ex"));
    CHECK_EQ(openStart("DH1:Ex", "DH1:Work/mine", "RAM:"), std::string("DH1:Work"));
    CHECK_EQ(openStart("", "DH1:Work/mine", "RAM:"), std::string("DH1:Work"));
    CHECK_EQ(openStart("", "", "RAM:"), std::string("RAM:"));
}

TEST(on_off)
{
    CHECK(parseOnOff(""));
    CHECK(!parseOnOff("", false));
    CHECK(parseOnOff("1"));
    CHECK(parseOnOff("yes\n"));
    CHECK(!parseOnOff("0"));
    CHECK(!parseOnOff(" off\n"));
    CHECK(!parseOnOff("No"));
}
