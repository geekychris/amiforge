// --- lang --- The IDE-wide key table (see KeyBindings.h).
#include "KeyBindings.h"
#include "Commands.h"

namespace af {
namespace keys {

static const std::vector<Binding> table = {
    { RAW_F1, 0, CMD_DOC_LOOKUP, "AutoDoc for the word at the cursor" },
    { RAW_HELP, 0, CMD_DOC_LOOKUP, "AutoDoc for the word at the cursor" },
    { RAW_F2, 0, CMD_RENAME, "rename the symbol everywhere" },
    { RAW_F3, 0, CMD_FIND_NEXT, "find next" },
    { RAW_F3, SHIFT, CMD_FIND_PREV, "find previous" },
    { RAW_F4, 0, CMD_NEXT_ERROR, "next error" },
    { RAW_F4, SHIFT, CMD_PREV_ERROR, "previous error" },
    { RAW_F5, 0, CMD_DEBUG_START, "start debugging / continue" },
    { RAW_F5, SHIFT, CMD_DEBUG_STOP, "stop debugging" },
    { RAW_F5, CTRL, CMD_RUN, "build and run" },
    { RAW_F5, SHIFT | CTRL, CMD_STOP_PROGRAM, "stop the program Run started" },
    { RAW_F5, ALT, CMD_DEBUG_PAUSE, "pause the program being debugged" },
    { RAW_F5, SHIFT | ALT, CMD_DEBUG_RESTART, "restart debugging (builds first)" },
    { RAW_BREAK, 0, CMD_DEBUG_PAUSE, "pause the program being debugged" },
    { RAW_F6, 0, CMD_RUN, "build and run" },
    { RAW_F7, 0, CMD_BUILD, "build" },
    { RAW_F7, SHIFT, CMD_PROFILE, "profile the program" },
    { RAW_F8, 0, CMD_STEP_INSN, "step one instruction" },
    { RAW_F9, 0, CMD_TOGGLE_BP, "set / clear a breakpoint on the line" },
    { RAW_F9, SHIFT, CMD_QUICK_WATCH, "watch the word or selection at the cursor" },
    { RAW_F9, CTRL, CMD_APPLY_CHANGES, "apply code changes (Edit and Continue)" },
    { RAW_F9, ALT, CMD_BP_TOGGLE_ENABLED, "enable / disable the breakpoint on the line" },
    { RAW_F9, SHIFT | CTRL, CMD_BP_CONDITION, "breakpoint condition for the line" },
    { RAW_F10, 0, CMD_STEP_OVER, "step over" },
    { RAW_F10, CTRL, CMD_RUN_TO_CURSOR, "run to the cursor" },
    { RAW_F10, SHIFT | CTRL, CMD_SET_NEXT_STATEMENT, "set the next statement to the cursor's line" },
    { RAW_F10, ALT, CMD_RUN_TO_FUNCTION, "run to a function" },
    { RAW_F11, 0, CMD_STEP_INTO, "step into" },
    { RAW_F11, SHIFT, CMD_STEP_OUT, "step out" },
    { RAW_F12, 0, CMD_GOTO_DEF, "go to the definition" },
    { RAW_F12, SHIFT, CMD_FIND_REFS, "find all references" },
};

const std::vector<Binding> &bindings() { return table; }

static uint32_t exact(uint16_t rawkey, uint8_t mods)
{
    for (auto &b : table)
        if (b.rawkey == rawkey && b.mods == mods) return b.cmd;
    return 0;
}

uint32_t commandFor(uint16_t rawkey, uint8_t mods)
{
    if (uint32_t c = exact(rawkey, mods)) return c;
    if (uint32_t c = exact(rawkey, mods & ~ALT)) return c;
    if (uint32_t c = exact(rawkey, mods & ~(ALT | CTRL))) return c;
    return exact(rawkey, 0);
}

bool isGlobalKey(uint16_t rawkey)
{
    for (auto &b : table)
        if (b.rawkey == rawkey) return true;
    return false;
}

uint8_t modsFromQualifier(uint16_t q)
{
    // IEQUALIFIER_LSHIFT 0x01, RSHIFT 0x02, CONTROL 0x08, LALT 0x10, RALT 0x20
    uint8_t m = 0;
    if (q & 0x03) m |= SHIFT;
    if (q & 0x08) m |= CTRL;
    if (q & 0x30) m |= ALT;
    return m;
}

std::string keyName(uint16_t rawkey, uint8_t mods)
{
    std::string k;
    if (rawkey >= RAW_F1 && rawkey <= RAW_F10) k = "F" + std::to_string(rawkey - RAW_F1 + 1);
    else if (rawkey == RAW_F11) k = "F11";
    else if (rawkey == RAW_F12) k = "F12";
    else if (rawkey == RAW_HELP) k = "Help";
    else if (rawkey == RAW_BREAK) k = "Break";
    else k = "key " + std::to_string(rawkey);
    std::string m;
    if (mods & SHIFT) m += "Shift ";
    if (mods & CTRL) m += "Ctrl ";
    if (mods & ALT) m += "Alt ";
    return m + k;
}

std::string keyFor(uint32_t cmd)
{
    for (auto &b : table)
        if (b.cmd == cmd) return keyName(b.rawkey, b.mods);
    return "";
}

std::string describe(const std::vector<uint32_t> &cmds)
{
    std::string out;
    for (uint32_t c : cmds)
        for (auto &b : table)
            if (b.cmd == c) {
                std::string k = keyName(b.rawkey, b.mods);
                if (k.size() < 15) k.resize(15, ' ');
                out += "  " + k + " " + b.what + "\n";
                break;
            }
    return out;
}

} // namespace keys
} // namespace af
