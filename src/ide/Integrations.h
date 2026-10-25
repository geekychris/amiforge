// Internal glue between the integrations' files (GitPanel, HexEditor, ResourceEditors,
// RexxPort, Integrations).
#pragma once

#include "App.h"

namespace af {

bool fileTrackedOrHeadless(App::GitState *gs, const std::string &rel);
void gitIdle(App *a);
void gitCleanup(App *a);
uint32_t resourceSignals(App *a);
void resourceHandle(App *a, uint32_t got);
void resourceCleanup(App *a);
uint32_t rexxSignals(App *a);
void rexxHandle(App *a);
void appendMacroMenu(App *a, std::vector<struct NewMenu> &m);
std::string distccHostsFor(const Project &p);

} // namespace af
