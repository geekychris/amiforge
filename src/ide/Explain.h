// Amiga-aware explanations for compiler/linker diagnostics and for crashes caught by the
// debugger. Rules are plain string matching, so this is portable and tested on the host.
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <stdint.h>

namespace af {

struct Explanation {
    std::string rule;     // short id of the rule that matched ("os-implicit-decl")
    std::string title;    // one line
    std::string text;     // what it means and what to do (may be several lines)
    bool empty() const { return text.empty(); }
};

// Looks up an OS function: returns its library ("intuition.library") or "".
using LibraryLookup = std::function<std::string(const std::string &function)>;

// A compiler, assembler or linker message (the text after "file:line:col: ", or a whole
// linker line). `sourceLine` is the source line it points at, if known.
Explanation explainDiagnostic(const std::string &message, const std::string &sourceLine = "",
                              const LibraryLookup &lookup = nullptr);

// The interface name for a library ("intuition.library" -> "IIntuition") and the proto
// header ("proto/intuition.h").
std::string interfaceForLibrary(const std::string &lib);
std::string protoForLibrary(const std::string &lib);
// A few common OS functions, for when the AutoDocs aren't installed.
std::string builtinLibraryOf(const std::string &function);

struct CrashInfo {
    uint32_t trapType = 0;      // 0x300 DSI, 0x400 ISI, 0x600 alignment, 0x700 program, ...
    uint32_t pc = 0, dar = 0, dsisr = 0, sp = 0, lr = 0;
    uint32_t gpr[32] = {};
    std::string insn;           // disassembly at the PC, e.g. "lwz r9,24(r3)", if known
    bool pcInProgram = true;    // the PC is in the program's own code
    std::string function;       // where it crashed ("main", or an OS function / module)
    std::string module;         // "program", "kernel", "intuition.library"...
    std::string callerFunction; // the program's function that made the call (for crashes in the OS)
    std::string callerFile;
    int callerLine = 0;
    std::string sourceLine;     // the program's source line (the top frame that has source)
    int frames = 0;             // depth of the call stack
    int deepestRepeat = 0;      // how many frames in a row are the same function (recursion)
};
Explanation explainCrash(const CrashInfo &c);

// The register an instruction uses as its base and the displacement ("lwz r9,24(r3)" -> 3, 24).
bool parseMemOperand(const std::string &insn, int &baseReg, int32_t &disp, bool &isStore);

} // namespace af
