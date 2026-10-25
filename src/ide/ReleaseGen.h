// Release packaging: the Aminet readme, the install script, the version string and the
// release compiler flags. Portable C++ (no OS calls); ReleaseUI.cpp does the building,
// staging and archiving.
#pragma once

#include <string>
#include <vector>

namespace af {
namespace rel {

struct Info {
    std::string name;          // the program's name ("Bouncer")
    std::string program;       // its file name in the archive ("bouncer")
    std::string version;       // "1.0"
    std::string date;          // "25.10.2026"
    std::string author;        // "Jane Doe <jane@example.org>"
    std::string uploader;      // empty = the author
    std::string shortDesc;     // Aminet's Short: line, at most 40 characters
    std::string type;          // Aminet category, "util/misc"
    std::string description;   // free text for the readme's body
    std::string replaces;      // optional: the Aminet file this one replaces
    std::string requires;      // optional: what else it needs
};

// "$VER: Bouncer 1.0 (25.10.2026)"
std::string versionString(const Info &i);

// The Aminet readme (<archive>.readme), with the fixed header fields Aminet's upload checks need.
std::string aminetReadme(const Info &i);

// Problems that would get the upload rejected or make the package wrong; empty if none.
std::vector<std::string> validate(const Info &i);

// The archive's name without extension, from the name and version: "Bouncer-1.0" -> "Bouncer"
// isn't enough on Aminet, so "Bouncer" + version digits: "Bouncer10".
std::string archiveBase(const Info &i);

// An AmigaDOS script that installs the drawer `<name>` (and its icon) into a drawer chosen in a
// requester, or given as its argument: "Install Work:Tools".
std::string installScript(const Info &i);

// Release compiler flags from the debug flags: no -g/-gstabs, -O0 becomes -O2 (kept otherwise).
std::string releaseFlags(const std::string &debugFlags);

// The Aminet categories offered in the project settings.
const std::vector<std::string> &aminetTypes();

} // namespace rel
} // namespace af
