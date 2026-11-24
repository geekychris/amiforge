// Help > User Manual: which file of the manual to show (portable; tests/host/ide/help.mk).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace af {
namespace help {

// The manual's files in the order Help > User Manual tries them: the AmigaGuide in AmiForge's
// Docs drawer (as the release archive has it), then the plain-text version.
std::vector<std::string> manualCandidates(const std::string &progdir = "PROGDIR:");
// The first candidate that exists, or "".
std::string findManual(const std::function<bool(const std::string &)> &exists,
                       const std::string &progdir = "PROGDIR:");

} // namespace help
} // namespace af
