// The OS side of the Intuition inspector: reads the debuggee's screens, windows and gadgets.
#pragma once
#include <set>
#include "InspectModel.h"

struct Task;
struct Screen;
struct Window;

namespace af {

// The screens that exist now (so later ones can be told apart).
std::set<uint32_t> currentScreens();
// Fills `out` with `task`'s windows (those whose UserPort signals it), the screens they are
// on, the screens not in screensAtStart, and the task's signal state. IBase is locked only
// while the lists are copied.
void takeInspectSnapshot(struct Task *task, const std::set<uint32_t> &screensAtStart, InspectSnapshot &out);
// Finds the window again (it may have closed) and the screen box of it or of its gadget.
bool findWindowBox(uint32_t window, int gadget, struct Screen **scr, struct Window **win, int box[4]);

} // namespace af
