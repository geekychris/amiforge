// --- lang --- The IDE-wide keys (function keys, Help, Pause/Break): one table, so the key
// handler, the shortcuts list and the tests agree and no key is bound twice. Plain data (no OS
// calls), so the host tests can check it.
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace af {
namespace keys {

// Raw key codes (libraries/keymap.h). F11, F12 and Break exist on PC keyboards (and QEMU's).
enum : uint16_t {
    RAW_F1 = 0x50, RAW_F2, RAW_F3, RAW_F4, RAW_F5, RAW_F6, RAW_F7, RAW_F8, RAW_F9, RAW_F10,
    RAW_F11 = 0x4B, RAW_F12 = 0x6F, RAW_HELP = 0x5F, RAW_BREAK = 0x6E,
};
enum : uint8_t { SHIFT = 1, CTRL = 2, ALT = 4 };

struct Binding {
    uint16_t rawkey;
    uint8_t mods;        // SHIFT | CTRL | ALT
    uint32_t cmd;        // Commands.h
    const char *what;    // for the shortcuts list
};

const std::vector<Binding> &bindings();
// The command for a key: an exact match, else the same key without Alt, then without Ctrl,
// then without Shift (so an unbound combination does what the plain key does, as before). 0 if
// the key isn't one of ours.
uint32_t commandFor(uint16_t rawkey, uint8_t mods);
bool isGlobalKey(uint16_t rawkey);                // a key the table binds (goes to the IDE, not the editor)
uint8_t modsFromQualifier(uint16_t qualifier);    // IEQUALIFIER_* bits -> SHIFT/CTRL/ALT
std::string keyName(uint16_t rawkey, uint8_t mods);   // "Shift Ctrl F5", "Break" (as the menus show keys)
std::string keyFor(uint32_t cmd);                 // the first binding of a command, "" if none
// The shortcuts list: "  <key>  <what>" lines for the bindings in `cmds` order.
std::string describe(const std::vector<uint32_t> &cmds);

} // namespace keys
} // namespace af
