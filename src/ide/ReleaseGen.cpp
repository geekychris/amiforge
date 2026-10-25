// Release packaging text: Aminet readme, install script, flags (see ReleaseGen.h).
#include "ReleaseGen.h"

#include <cctype>
#include <sstream>

namespace af {
namespace rel {

std::string versionString(const Info &i)
{
    return "$VER: " + i.name + " " + (i.version.empty() ? "1.0" : i.version) + " (" + i.date + ")";
}

static std::string field(const char *k, const std::string &v)
{
    std::string key = std::string(k) + ":";
    while (key.size() < 14) key += ' ';
    return key + v + "\n";
}

std::string aminetReadme(const Info &i)
{
    std::string r;
    r += field("Short", i.shortDesc);
    r += field("Author", i.author);
    r += field("Uploader", i.uploader.empty() ? i.author : i.uploader);
    r += field("Type", i.type);
    r += field("Version", i.version.empty() ? "1.0" : i.version);
    if (!i.replaces.empty()) r += field("Replaces", i.replaces);
    r += field("Architecture", "ppc-amigaos >= 4.1.0");
    if (!i.requires.empty()) r += field("Requires", i.requires);
    r += field("Distribution", "Aminet");
    r += "\n";
    std::string body = i.description.empty() ? i.shortDesc + "." : i.description;
    r += body;
    if (body.empty() || body.back() != '\n') r += "\n";
    r += "\nInstalling\n----------\n";
    r += "Double-click the Install icon, or copy the " + i.name + " drawer wherever you like.\n";
    r += "From a Shell: Install <drawer>\n";
    r += "\nRequirements\n------------\nAmigaOS 4.1 (PowerPC)";
    if (!i.requires.empty()) r += ", " + i.requires;
    r += ".\n\nHistory\n-------\n" + (i.version.empty() ? std::string("1.0") : i.version) + " (" + i.date + ")  First release.\n";
    return r;
}

static bool hasEmail(const std::string &s)
{
    size_t at = s.find('@');
    return at != std::string::npos && at > 0 && s.find('.', at) != std::string::npos;
}

std::vector<std::string> validate(const Info &i)
{
    std::vector<std::string> p;
    if (i.name.empty()) p.push_back("The program has no name.");
    if (i.shortDesc.empty()) p.push_back("Fill in a short description.");
    else if (i.shortDesc.size() > 40)
        p.push_back("The short description is " + std::to_string(i.shortDesc.size()) +
                    " characters long; Aminet allows 40.");
    if (i.author.empty()) p.push_back("Fill in the author.");
    else if (!hasEmail(i.uploader.empty() ? i.author : i.uploader))
        p.push_back("Aminet needs an email address for the uploader: \"Name <you@example.org>\".");
    size_t slash = i.type.find('/');
    bool typeOk = slash != std::string::npos && slash > 0 && slash + 1 < i.type.size();
    if (typeOk) {
        typeOk = false;
        std::string top = i.type.substr(0, slash);
        for (auto &t : aminetTypes())
            if (t.compare(0, top.size() + 1, top + "/") == 0) typeOk = true;
    }
    if (!typeOk) p.push_back("\"" + i.type + "\" isn't an Aminet category such as util/misc.");
    bool vOk = !i.version.empty() && isdigit((unsigned char)i.version[0]);
    for (char c : i.version)
        if (!isdigit((unsigned char)c) && c != '.') vOk = false;
    if (!vOk) p.push_back("The version should look like 1.0 or 2.13.");
    return p;
}

std::string archiveBase(const Info &i)
{
    std::string b;
    for (unsigned char c : i.name)
        if (isalnum(c) && c < 128) b += (char)c;
    if (b.empty()) b = "Program";
    for (char c : i.version)
        if (isdigit((unsigned char)c)) b += c;
    return b;
}

std::string installScript(const Info &i)
{
    std::ostringstream s;
    s << ".key DEST\n"
         ".bra {\n"
         ".ket }\n"
         "; Installs " << i.name << " " << i.version << ". From a Shell: Install <drawer>,\n"
         "; or double-click the icon to choose the drawer in a requester.\n"
         "FailAt 21\n"
         "Set dest \"{DEST}\"\n"
         "If \"$dest\" EQ \"\"\n"
         "  RequestFile >ENV:AFInstallDest DRAWER SYS: DRAWERSONLY NOICONS TITLE \"Install "
      << i.name << " into which drawer?\"\n"
         "  If WARN\n"
         "    Echo \"" << i.name << " was not installed.\"\n"
         "    Quit 5\n"
         "  EndIf\n"
         "  Set dest `Type ENV:AFInstallDest`\n"
         "  Delete >NIL: ENV:AFInstallDest QUIET\n"
         "EndIf\n"
         "If NOT EXISTS \"$dest\"\n"
         "  Echo \"There is no drawer $dest.\"\n"
         "  Quit 10\n"
         "EndIf\n"
         "Copy >NIL: \"" << i.name << "\" \"$dest/" << i.name << "\" ALL CLONE QUIET\n"
         "If EXISTS \"" << i.name << ".info\"\n"
         "  Copy >NIL: \"" << i.name << ".info\" \"$dest\" CLONE QUIET\n"
         "EndIf\n"
         "If EXISTS \"$dest/" << i.name << "/" << i.program << "\"\n"
         "  Echo \"" << i.name << " is installed in $dest/" << i.name << ".\"\n"
         "Else\n"
         "  Echo \"Copying to $dest failed.\"\n"
         "  Quit 10\n"
         "EndIf\n";
    return s.str();
}

std::string releaseFlags(const std::string &debugFlags)
{
    std::istringstream in(debugFlags);
    std::string w, out;
    bool haveOpt = false;
    while (in >> w) {
        if (w.compare(0, 2, "-g") == 0) continue;            // -g, -gstabs, -gstabs+, -ggdb
        if (w == "-O0") w = "-O2";
        if (w.compare(0, 2, "-O") == 0) haveOpt = true;
        if (w == "-DDEBUG") continue;
        out += (out.empty() ? "" : " ") + w;
    }
    if (!haveOpt) out = "-O2" + std::string(out.empty() ? "" : " ") + out;
    return out;
}

const std::vector<std::string> &aminetTypes()
{
    static std::vector<std::string> t = {
        "biz/misc", "comm/misc", "comm/net", "comm/www", "demo/misc", "dev/asm", "dev/c", "dev/debug",
        "dev/gui", "dev/lib", "dev/misc", "dev/src", "disk/misc", "docs/misc", "game/2play", "game/action",
        "game/board", "game/misc", "game/shoot", "game/think", "gfx/edit", "gfx/misc", "gfx/show",
        "hard/drivr", "hard/misc", "misc/emu", "misc/misc", "mus/edit", "mus/misc", "mus/play",
        "text/edit", "text/misc", "text/show", "util/arc", "util/blank", "util/boot", "util/cdity",
        "util/cli", "util/dir", "util/libs", "util/misc", "util/sys", "util/wb",
    };
    return t;
}

} // namespace rel
} // namespace af
