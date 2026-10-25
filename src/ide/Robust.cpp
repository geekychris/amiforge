// --- robust --- see Robust.h
#include "Robust.h"

#include <stdio.h>

namespace af {
namespace robust {

void RunModel::started(const std::string &name)
{
    count++;
    newest = name;
    if (st == RunState::Idle) st = RunState::Running;
}

int RunModel::exited(int returnCode)
{
    if (count > 0) count--;
    lastRc = returnCode;
    if (count == 0) {
        stopAsked = st != RunState::Running;
        st = RunState::Idle;
        sendDEF = false;
    }
    return count;
}

int RunModel::stop(uint32_t nowMs)
{
    if (count == 0) return RA_NONE;
    switch (st) {
    case RunState::Idle:            // not reached: count > 0 means Running or later
    case RunState::Running:
        st = RunState::Stopping;
        stopAt = nowMs;
        sendDEF = false;
        return RA_SEND_CTRL_C | RA_ARM_TIMER;
    case RunState::Stopping:
        // pressed again before the grace time: the program had its Ctrl-C; once the
        // grace time has passed, don't make the user wait for the timer
        if (nowMs - stopAt < grace) return RA_NONE;
        st = RunState::Stubborn;
        return RA_OFFER_MORE;
    case RunState::Stubborn:
        return RA_OFFER_MORE;
    }
    return RA_NONE;
}

int RunModel::tick(uint32_t nowMs)
{
    if (count == 0 || st != RunState::Stopping) return RA_NONE;
    if (nowMs - stopAt < grace) return RA_ARM_TIMER;   // woke early
    st = RunState::Stubborn;
    return RA_OFFER_MORE;
}

int RunModel::choose(StubbornChoice c, uint32_t nowMs)
{
    if (count == 0) return RA_NONE;
    switch (c) {
    case SC_AGAIN:
    case SC_ALL:
        st = RunState::Stopping;
        stopAt = nowMs;
        sendDEF = c == SC_ALL;
        return RA_SEND_CTRL_C | RA_ARM_TIMER;
    case SC_LEAVE:
        // back to plain running: Stop Program starts over with Ctrl-C
        st = RunState::Running;
        sendDEF = false;
        return RA_NONE;
    }
    return RA_NONE;
}

std::string RunModel::status() const
{
    std::string n = newest;
    if (count > 1) n += " (and " + std::to_string(count - 1) + (count == 2 ? " other)" : " others)");
    switch (st) {
    case RunState::Idle: return "";
    case RunState::Running:
        return "Running " + n + " - Stop Program (Shift-Ctrl-F5 or the Stop button) sends it Ctrl-C";
    case RunState::Stopping:
        return "Sent Ctrl-C to " + n + " - waiting for it to quit...";
    case RunState::Stubborn:
        return n + " hasn't quit after Ctrl-C - Stop Program again for the choices";
    }
    return "";
}

std::string RunModel::endedStatus() const
{
    char rc[32];
    snprintf(rc, sizeof rc, "%d", lastRc);
    if (stopAsked) return newest + " stopped (return code " + rc + ")";
    return newest + " ended (return code " + rc + ")";
}

std::string RunModel::stubbornText(uint32_t nowMs) const
{
    unsigned secs = (unsigned)((nowMs - stopAt) / 1000);
    if (secs == 0) secs = 1;
    char s[32];
    snprintf(s, sizeof s, "%u", secs);
    std::string who = count > 1 ? newest + " and the other programs started with Run have" : newest + " has";
    return who + " not quit " + s + (secs == 1 ? " second" : " seconds") + " after Ctrl-C.\n\n"
           "A program has to check for Ctrl-C itself (SetSignal or Wait with SIGBREAKF_CTRL_C, or\n"
           "newlib's checks in printf and friends); some use Ctrl-D, E or F instead.\n\n"
           "AmigaOS can't force a program to quit safely: its memory, windows, files and\n"
           "signals would be left behind and the system could crash. If it doesn't react,\n"
           "close its windows yourself, or save your work and reboot.";
}

// The WMHI_* classes (classes/window.h); Modal.cpp checks them against the SDK's.
enum : uint32_t {
    K_CLOSEWINDOW = 1u << 16, K_GADGETUP = 2u << 16, K_INACTIVE = 3u << 16, K_ACTIVE = 4u << 16,
    K_NEWSIZE = 5u << 16, K_MENUPICK = 6u << 16, K_MENUHELP = 7u << 16, K_GADGETHELP = 8u << 16,
    K_ICONIFY = 9u << 16, K_UNICONIFY = 10u << 16, K_RAWKEY = 11u << 16, K_VANILLAKEY = 12u << 16,
    K_CHANGEWINDOW = 13u << 16, K_INTUITICK = 14u << 16, K_MOUSEMOVE = 15u << 16, K_MOUSEBUTTONS = 16u << 16,
    K_GADGETDOWN = 20u << 16,
};

ModalReaction modalReaction(uint32_t c)
{
    switch (c) {
    case K_NEWSIZE:             // the layout changed: keep the editor on its placeholder
    case K_UNICONIFY:
        return ModalReaction::Handle;
    case K_ACTIVE:              // clicked (or ClickToFront): the user wants the IDE
    case K_CLOSEWINDOW:
    case K_GADGETUP:
    case K_GADGETDOWN:
    case K_MENUPICK:
    case K_MENUHELP:
    case K_RAWKEY:
    case K_VANILLAKEY:
    case K_MOUSEBUTTONS:
    case K_ICONIFY:
        return ModalReaction::RaiseDialog;
    default:                    // inactive, ticks, mouse moves, moves and depth changes, help
        return ModalReaction::Ignore;
    }
}

} // namespace robust
} // namespace af
