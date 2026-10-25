// --- robust ---
// Keeping AmiForge from looking hung: the logic, without OS calls, so it is host-tested
// (tests/host/ide/robust.mk). The exec/dos/intuition side is in RunControl.cpp (Stop Program)
// and Modal.cpp (modal dialogs that can't get lost behind the main window).
#pragma once

#include <stdint.h>
#include <string>

namespace af {
namespace robust {

// --- Build > Run's programs and Stop Program ---
//
//   Idle --started--> Running --stop--> Stopping --(grace ms pass)--> Stubborn
//     ^                  |                 |                              |
//     +----all exited----+-----------------+------------------------------+
//
// Stop Program sends Ctrl-C. Programs have to check for it themselves; AmigaOS has no safe
// way to force one to quit, so when a program is still there after `grace` ms the IDE offers
// to send Ctrl-C again, or Ctrl-D/E/F too, or to leave it running (and says why).
enum class RunState { Idle, Running, Stopping, Stubborn };

enum RunAction : int {
    RA_NONE = 0,
    RA_SEND_CTRL_C = 1,       // signal SIGBREAKF_CTRL_C to every running program
    RA_ARM_TIMER = 2,         // wake up after `grace` ms (then call tick)
    RA_OFFER_MORE = 4,        // ask: Ctrl-C again / Ctrl-C+D+E+F / leave it running
};

// The answer to the RA_OFFER_MORE requester (its buttons, left to right; 0 = the last one)
enum StubbornChoice { SC_AGAIN = 1, SC_ALL = 2, SC_LEAVE = 0 };

class RunModel {
public:
    explicit RunModel(uint32_t graceMs = 3000) : grace(graceMs) {}
    void started(const std::string &name);              // Run started a program
    int exited(int returnCode);                         // one ended; returns the number still running
    int stop(uint32_t nowMs);                           // Stop Program: RA_* bits
    int tick(uint32_t nowMs);                           // the timer went off: RA_* bits
    int choose(StubbornChoice c, uint32_t nowMs);       // the RA_OFFER_MORE answer: RA_* bits (RA_SEND_CTRL_C, plus
                                                        // `alsoDEF()` true for SC_ALL)
    bool alsoDEF() const { return sendDEF; }            // the last RA_SEND_CTRL_C also wants Ctrl-D/E/F
    RunState state() const { return st; }
    bool running() const { return count > 0; }
    int programs() const { return count; }
    const std::string &name() const { return newest; }
    int lastReturnCode() const { return lastRc; }
    bool stopWasAsked() const { return stopAsked; }     // the last program to end had been asked to stop
    std::string status() const;                         // for the status bar
    std::string endedStatus() const;                    // after the last program ended
    std::string stubbornText(uint32_t nowMs) const;     // the RA_OFFER_MORE requester's body
    static const char *stubbornButtons() { return "Send Ctrl-_C Again|Send Ctrl-C, _D, E and F|_Leave It Running"; }
private:
    RunState st = RunState::Idle;
    uint32_t grace;
    uint32_t stopAt = 0;
    int count = 0;
    int lastRc = 0;
    bool sendDEF = false;
    bool stopAsked = false;
    std::string newest;
};

// --- modal dialogs ---
//
// While a modal dialog runs its own loop, input for the IDE's other windows must not be lost
// or acted on: the dialog is brought back in front instead. Refreshes are still handled so a
// window redraws when it is uncovered.
enum class ModalReaction { Handle, RaiseDialog, Ignore };
// `wmhiClass`: what WM_HANDLEINPUT returned (result & WMHI_CLASSMASK) for another IDE window
// while a dialog is modal. window.class handles refreshes itself inside WM_HANDLEINPUT.
ModalReaction modalReaction(uint32_t wmhiClass);

} // namespace robust
} // namespace af
// --- end robust ---
