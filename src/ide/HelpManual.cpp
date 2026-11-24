// Help > User Manual: which file of the manual to show (see HelpManual.h).
#include "HelpManual.h"

namespace af {
namespace help {

std::vector<std::string> manualCandidates(const std::string &progdir)
{
    std::string d = progdir;
    if (!d.empty() && d.back() != ':' && d.back() != '/') d += "/";
    return { d + "Docs/AmiForge-Manual.guide", d + "Docs/AmiForge-Manual.txt" };
}

std::string findManual(const std::function<bool(const std::string &)> &exists, const std::string &progdir)
{
    for (auto &p : manualCandidates(progdir))
        if (exists(p)) return p;
    return "";
}

} // namespace help
} // namespace af
