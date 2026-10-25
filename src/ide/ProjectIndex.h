// The project browser's model: finds the projects in the projects folder and describes them.
// Host-testable: the file system comes in through two callbacks (list a drawer, read a file).
//
//   projects folder/            (ENVARC:AmiForge/ProjectsFolder, default PROGDIR:Examples)
//     clock/clock.afp           depth 1
//     games/
//       bouncer/bouncer.afp     depth 2
//
// A drawer that holds a .afp is a project drawer: the scan doesn't look inside it (obj/ etc.).
// The description is the first line of the .afp's description= (in [project], \n-escaped as
// Package Release writes it), else the first line of a README in the drawer.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace af {
namespace pidx {

struct DirItem {
    std::string name;
    bool dir = false;
};
// List a drawer's entries; false if it can't be read.
using ListDir = std::function<bool(const std::string &path, std::vector<DirItem> &out)>;
// Read a whole (small) file; false if it isn't there.
using ReadFile = std::function<bool(const std::string &path, std::string &out)>;

struct Project {
    std::string name;          // [project] name=, else the .afp's name without .afp
    std::string description;   // one line, may be empty
    std::string drawer;        // relative to the projects folder ("games/bouncer"); for recent ones, absolute
    std::string afp;           // full path of the .afp
};

// A row of the browser: a group heading or a project.
struct Row {
    bool heading = false;
    std::string cols[3];       // name, description, drawer (a heading has its text in cols[0])
    std::string afp;           // empty for headings
};

// AmigaDOS path helpers (no OS calls).
std::string join(const std::string &dir, const std::string &name);   // "DH1:" + "x" = "DH1:x", "a" + "x" = "a/x"
std::string parent(const std::string &path);                          // "DH1:a/b" -> "DH1:a", "DH1:a" -> "DH1:"
std::string leaf(const std::string &path);                            // "DH1:a/b.afp" -> "b.afp"
// True if `path` is `folder` or inside it (case-insensitive, as AmigaDOS names are).
bool isInside(const std::string &path, const std::string &folder);

// The first line of the .afp's [project] description= (unescaped), or "".
std::string afpDescription(const std::string &afpText);
// [project] name=, or "".
std::string afpName(const std::string &afpText);
// The first line of a README worth showing: skips blank lines and Markdown rules/underlines,
// takes "#" off headings. Cut to `max` characters (with "...").
std::string readmeLine(const std::string &text, size_t max = 100);
// Is this file name a README (README, README.txt, ReadMe.md, readme.txt ...)?
bool isReadme(const std::string &name);

// Describe the project in `afp` (a full path); `items` is the listing of its drawer (for the README).
Project describe(const std::string &afp, const std::vector<DirItem> &items, const ReadFile &read);

// Every project under `root`, `depth` drawers deep (1 = only root's sub-drawers), sorted by name
// (case-insensitive), then drawer. Project files directly in root are included too.
std::vector<Project> scan(const std::string &root, int depth, const ListDir &list, const ReadFile &read);

// The recent projects (full .afp paths, newest first) that still exist, described.
std::vector<Project> recent(const std::vector<std::string> &afps, const ListDir &list, const ReadFile &read);

// The browser's rows: "Recent" with its projects (if any), then "Projects" (description column:
// "in <folder>") with the scanned ones (or a "(no projects here ...)" note).
std::vector<Row> rows(const std::vector<Project> &recentOnes, const std::vector<Project> &found,
                      const std::string &folder);

// Where Project > Open Project's requester starts: the projects folder, unless the open project
// lives outside it - then that project's parent drawer (its siblings are one click away).
// Both paths should be absolute. lastDir is used when there is neither.
std::string openStart(const std::string &projectsFolder, const std::string &currentProjectDir,
                      const std::string &lastDir);

// The ShowBrowser setting's text ("0" / "no" / "off" = off; anything else, or unset, = on).
bool parseOnOff(const std::string &text, bool dflt = true);

} // namespace pidx
} // namespace af
