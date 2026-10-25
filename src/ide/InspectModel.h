// The live Intuition inspector's data: a snapshot of the debuggee's screens, windows and
// gadgets (taken by Inspector.cpp under LockIBase), the flag decoders, and the rows of the
// "Windows" panel. No OS calls here: tests/host/ide builds this on the host.
#pragma once
#include <string>
#include <vector>
#include <stdint.h>

namespace af {

struct GadgetInfo {
    uint32_t addr = 0;
    uint16_t id = 0, type = 0, flags = 0, activation = 0;
    int16_t left = 0, top = 0, width = 0, height = 0;
    std::string className;      // BOOPSI class ("button.gadget"), "" for plain gadgets
    std::string text;           // IntuiText / label, if any
};

struct WindowInfo {
    uint32_t addr = 0;
    std::string title;
    int16_t left = 0, top = 0, width = 0, height = 0;
    uint32_t idcmp = 0, flags = 0;
    int sigBit = -1;            // UserPort's signal bit
    uint32_t screen = 0;        // the screen it is on
    bool active = false;
    std::vector<GadgetInfo> gadgets;
};

struct ScreenInfo {
    uint32_t addr = 0;
    std::string title, pubName;
    int16_t width = 0, height = 0;
    int depth = 0;
    bool front = false;
    bool newSinceStart = false; // opened after debugging started (likely the program's)
};

struct TaskInfo {
    bool valid = false;
    std::string name;
    int state = 0;              // tc_State
    uint32_t sigAlloc = 0, sigWait = 0, sigRecvd = 0;
};

struct InspectSnapshot {
    std::vector<ScreenInfo> screens;   // screens with the program's windows, and new ones
    std::vector<WindowInfo> windows;   // the program's windows
    TaskInfo task;
};

// Flag decoders: "GADGETUP|CLOSEWINDOW|0x80000000" (unknown bits in hex), "" for 0.
std::string decodeIdcmp(uint32_t v);
std::string decodeWindowFlags(uint32_t v);
std::string decodeGadgetType(uint16_t v);     // "BOOLGADGET", "CUSTOMGADGET|GZZGADGET", "SYS:CLOSE"
std::string decodeGadgetFlags(uint16_t v);
std::string decodeActivation(uint16_t v);
std::string decodeTaskState(int state);
// Signal bits by name; window ports are named after their window ("IDCMP 'Bouncer'").
std::string decodeSignals(uint32_t mask, const InspectSnapshot &snap);

enum class InspKind { Info, Screen, Window, Gadget, Detail };
struct InspRow {
    int depth = 0;              // listbrowser generation - 1
    std::string name, value;
    InspKind kind = InspKind::Info;
    int index = -1;             // screens[] / windows[] index, gadget index within its window
    int window = -1;            // for gadgets and details: the window
    bool hasChildren = false;
};

// The panel's rows: the task's signals, then each screen with its windows, each window with
// its properties and gadgets.
std::vector<InspRow> inspectRows(const InspectSnapshot &snap);

} // namespace af
