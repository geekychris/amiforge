# AmiForge design

AmiForge is a native IDE for AmigaOS 4.1 (PowerPC). It runs on the Amiga itself, with:
- a multi-file editor with C, C++ and PPC assembler highlighting, using a port of Scintilla;
- the SDK's native GCC 11.5 (C, C++, GNU as) for building projects;
- a source-level debugger that runs on the Amiga, built on the exec debug interface.

The IDE itself is cross-compiled on a Mac or Linux host (Docker, `tools/build.sh`) and tested
in QEMU's sam460ex machine running AmigaOS 4.1 Final Edition.

## Layout

```
src/sci/     Scintilla platform layer for OS4 (PlatOS4.cpp, ScintillaOS4.cpp) + SciView.h API
src/dbg/     debugger core (dbg.h API). Portable parts (ELF/stabs reader, PPC decoder,
             expression evaluator, value formatting) compile on the host too; os4_*.c is the
             runtime that talks to exec/dos.
src/ide/     the application: ReAction GUI, projects, build runner, panels
third_party/ scintilla 5.5.7 and lexilla 5.4.5, unmodified upstream
templates/   project templates (template.def + files), shipped as PROGDIR:Templates
Rexx/        example ARexx macros, shipped as PROGDIR:Rexx
tests/os4/   test programs that run on OS4 (scitest, dbgtest, toolstest, hotpatchtest, rexx/...)
tests/targets/ small debuggee programs, always built -O0 -gstabs+
tests/host/  host unit tests: make -C tests/host (debugger core), make -C tests/host ide
             (IDE models, one runner per area in tests/host/ide/*.mk)
tests/templates/ check.sh: cross-compiles every template with -Werror
tests/examples/  check.sh: cross-compiles every example with -Werror
tools/       build.sh (Docker), m1*.sh/m1mcp.py (drive the OS4 guest on the test Mac)
```

## Build

`tools/build.sh` runs `make` in `walkero/amigagccondocker:os4-gcc11` (adtools GCC 11.5).
- C++17, newlib (`-mcrt=newlib`), `-mcpu=440fp`. **No `-D__USE_INLINE__`**: call OS4 functions through their interfaces (`IExec->AllocVecTags`, `IGraphics->Text`). The inline macros define function-like names such as `Insert`, `Remove`, `Text`, `Move` and `Draw`, which break C++ code, Scintilla and the STL.
- C++ programs link with `-athread=native`, or libstdc++'s gthread symbols are missing.
- Scintilla compiles unmodified with this toolchain.

## Testing on OS4

The test machine is `m1.local` (override with `M1=`). QEMU sam460ex runs AmigaOS
4.1 there with amiga_mcp's devbench on :3000. The bridge runs on the guest and gives file
transfer, shell commands and launching.
- `tools/m1push.sh build/AmiForge DH1:AmiForge/AmiForge` copies a file in (~160 KB/s).
- `tools/m1dos.sh 'list DH1:AmiForge'` runs a shell command and prints its output.
  Commands are run by the bridge daemon, so a program that never exits blocks the call; start
  GUI programs with `run >NIL: prog` instead.
- `tools/m1mcp.py <tool> '<json>'` calls any devbench MCP tool (`tools/m1mcp.py list`).
- `tools/m1shot.sh out.png` takes a screenshot via QEMU's monitor. The screen is 1280x720.
- `tools/m1click.sh X Y` clicks, and `tools/m1keys.sh a b ret ctrl-c f5` types, both via QEMU's
  monitor.
- Put test files under `DH1:AmiForge/` (the "chris" volume). Never use a path that may not
  exist as a volume name (e.g. `SDK:` before the SDK is installed): AmigaDOS pops up an
  "insert volume" requester, which blocks the bridge until someone clicks Cancel.
- OS4 has no memory protection to speak of. A bad debugger write can hang the guest. If the
  bridge stops answering, `ssh m1.local 'zsh -lc "retro restart os4"'`
  reboots it (about 60–90 s).

## Editor (src/sci)

- **Threading:** SciView (src/sci/SciView.h) runs on the IDE task. The IDE uses a space.gadget
  as a placeholder in its ReAction layout and routes IntuiMessages for that rectangle to the
  view. It is not a BOOPSI gadget, because gadget methods run on input.device's task.
- **Rendering:** into an off-screen bitmap (a friend of the screen's bitmap) with its own
  Layer_Info and layer, so `InstallClipRegion` gives clipping, then a blit into the window.
  Colours are 0xAARRGGBB set with `SetRPAttrs(RPTAG_APenColor/BPenColor)` on hi/true-colour
  screens, or `ObtainBestPen` on palette screens.
- **Text:**
  - fonts come from `OpenDiskFont` (DejaVu Sans Mono outline font, in FONTS:);
  - Scintilla runs in single-byte mode (code page 0, Latin-1, which is the Amiga's charset);
  - `MeasureWidths` comes from `TextLength`/`TextExtent`.
- **Platform pieces:**
  - clipboard: textclip.library;
  - timers: INTUITICKS plus `tick()`;
  - autocomplete list and calltips: small borderless windows;
  - context menu: popupmenu.class, or reported to the host.
- **Lexers:** LexCPP (C/C++), LexAsm (GNU as, with PPC mnemonics and registers as keywords),
  LexMake, LexProps, LexDiff; LexPython, LexLua, LexPerl, LexBash, LexJSON, LexYAML and
  LexMarkdown (about 250 KB together); and our own LexAmiga.cpp for ARexx and AmigaDOS scripts.
  `sci/Languages.*` decides a file's language (extension, first line, drawer) and holds each
  language's comment marks, indent rule and keyword lists; SciView's auto-indent asks it
  (`AF_SCI_SETLANGUAGE`).

## Debugger (src/dbg)

API: src/dbg/dbg.h.

**Launch.** This follows adtools GDB's amigaos-nat.c:
1. `LoadSeg`.
2. `Forbid`.
3. `CreateNewProcTags(NP_Seglist, NP_FreeSeglist=FALSE, NP_Cli=TRUE, NP_Arguments, NP_Input/NP_Output = console)`.
4. `SuspendTask`.
5. `Permit`.
6. `IDebug->AddDebugHook(task, &hook)`.
7. `RestartTask` once breakpoints are in.

`IDebug` = `GetInterface(SysBase, "debug", 1)`, and `IMMU` = `GetInterface(SysBase, "mmu", 1)`.

**Debug hook.** It is called on the debuggee's context with `(hook, task, msg)`. `msg` is
`{uint32 type; union {struct ExceptionContext *ctx; struct Library *lib;}}`:
- DBHMT_EXCEPTION: copy the context, `Signal()` the IDE, and return 1, which suspends the task.
- DBHMT_REMTASK: the program ended.

The hook must be short and must not call DOS. Read and write registers with
`IDebug->ReadTaskContext`/`WriteTaskContext` (RTCF_* flags). The PC is `ip`. After a `trap`,
`ip` points at the trap itself.

**Breakpoints.**
- A breakpoint is `0x7FE00008` (`tw 31,0,0`, i.e. `trap`), and the hit arrives as program
  exception 0x700.
- To write one:
  1. Read the memory attributes with `IMMU->GetMemoryAttrs(addr, 0)`.
  2. If needed, `SetMemoryAttrs(addr, 4, MEMATTRF_READ_WRITE)`.
  3. Write the instruction.
  4. Restore the old attributes.
  5. `IExec->CacheClearE(addr, 4, CACRF_ClearI | CACRF_ClearD)`.
- To resume from one:
  1. Put the original instruction back.
  2. Single-step it.
  3. Re-insert the trap.
  4. Continue.

**Single-step.** In software: decode the instruction at the PC.
- `b`/`bl`/`bc`/`bclr`/`bcctr` (with LR/CTR from the context) give the branch targets; anything
  else gives PC+4.
- Put temporary traps on the successors. Only ever place traps inside the debuggee's own
  sections.
- If a successor is outside them (a call into a library), trap the return address instead:
  PC+4 for a branch-and-link, or LR for a tail branch.
- The MSR trace bit may or may not work on QEMU's 440 model; it's optional.

**Source stepping.**
- Step over: temporary traps on every line-start address of the current function except the
  current line, plus the return address. A hit only counts when SP >= the starting SP, which
  handles recursion.
- Step into: instruction steps until the line changes. Steps into code without line info turn
  into a return-address trap.
- Step out: a trap on the return address, with the same SP check.

**Return address.** Before the prologue has run, it is LR. After the prologue, it is the word
at `*(r1) + 4`, via the back chain.

**Stack.** Walk the back chain from r1. `frame[0]` is the back chain, and `frame[1]` (at +4)
is the saved LR of the caller's frame. Look up names with stabs/symtab for the program, and
`IDebug->ObtainDebugSymbol` for everything else.

**Debug info.** Read from the ELF file on disk: `.stab`/`.stabstr`, `.symtab`, and the section
headers.
- N_FUN values are absolute link addresses. N_SLINE values are relative to the function start
  (ELF stabs).
- N_PSYM/N_LSYM offsets are relative to the frame base (r1 after the prologue). N_RSYM gives a
  register number.
- N_LBRAC/N_RBRAC (function-relative) mark scopes. N_GSYM, N_STSYM and N_LCSYM are globals and
  statics.
- Map to load addresses per section: `GetSegListInfoTags(seg, GSLI_ElfHandle, &h)` then
  `IElf->GetSectionTags(h, GST_SectionIndex, i, GST_Load, FALSE)` (or GST_SectionName) gives
  each loaded section's address. Then load = link − sh_addr + section load address.

**Types.** stabs type strings cover:
- ranges, pointers (`*`), struct/union (`s`/`u`), enums (`e`), arrays (`ar`), typedefs (`t`/`T`),
  const/volatile (`k`/`B`), functions (`f`/`F`), cross-references (`x`);
- with `-gstabs+`, the C++ extensions (methods, base classes). Parse what is needed and skip
  the rest.

**Memory safety.** Validate before reading (`IMMU->GetMemoryAttrs` / `TypeOfMem`); a bad
pointer must show `<unreadable>`, not crash the IDE.

**Expressions.** Covers:
- identifiers (locals, params, then globals), `.`, `->`, `[i]`, unary `*` and `&`, casts to known
  types, integer arithmetic;
- `$r3`, `$pc`, `$lr` and similar;
- hex/decimal literals, and comparisons for conditions.

**C++.** Demangle with `abi::__cxa_demangle`.

## IDE (src/ide)

- **GUI:** ReAction (window.class, layout.gadget, clicktab, listbrowser, speedbar, scroller,
  string, chooser), with menus via window.class and the menuclass.
- **Project files:** `*.afp`, ini-style: name, sources, target, compiler flags and link flags.
- **Builds:** run per file in a separate process using `SystemTags`, so the GUI stays live.
  Output is parsed into clickable errors (`file:line:col: error|warning|note: msg`, GNU as
  `file:line: Error:`, ld `undefined reference`).
- **Native toolchain:** the SDK installed in `SDK:` (gcc 11.5 in SDK:gcc/bin). `S:User-Startup`
  assigns SDK: and runs `SDK:S/sdk-startup`.

## Features added in 0.2

The OS-independent logic of each feature lives in a model file that also compiles on the host
(`-DAF_HOST_TEST`) and has unit tests; the ReAction and exec/dos side is in a separate file.

| Feature | Model (host-tested) | OS4 side | Notes |
|---|---|---|---|
| Themes | `src/sci/Theme.*` | `SciSetup.cpp` | A theme is a table of Scintilla styles; the tests check contrast |
| Split view, minimap, multiple cursors | | `EditorFeatures.cpp` | `mainView`/`splitView`/`miniView` share one Scintilla document; `editor` is the active view |
| Snippets | `Snippets.*` | `EditorFeatures.cpp` | `${1:x}` fields are an indicator whose value is the field number |
| Outline, references, rename | `Symbols.*` | `EditorFeatures.cpp` | Text-based (comments and strings skipped), not scope-aware |
| GUI designer | `GuiModel.*` (the `.afg` format, code generator) | `Designer.cpp` | Regenerating keeps the code between `/* USER-BEGIN key */` and `/* USER-END key */`; blocks whose gadget is gone are parked at the end, not lost |
| Templates | `TemplateEngine.*` | `TemplatesUI.cpp` | `template.def` lists files, substitutions and post-create commands (e.g. `idltool`) |
| Package Release | `ReleaseGen.*` (readme, install script, flags) | `ReleaseUI.cpp` | Release build into `obj-release`, staged drawer with icons, `tar` then `xad2lha` (OS4 has no LhA that creates archives) |
| Edit and Continue | `src/dbg/hotpatch.c` | `os4_session.c` (`dbg_hotpatch`), `HotPatch.cpp` | Links the recompiled object itself against the running program's symbols (no `ld`); globals map onto the program's copies; old entries get a branch (long branch or stub when out of range); debug info is merged and the newest version wins in lookups |
| Leak tracker | `LeakModel.*`, `resnames.cpp` | `src/dbg/os4_tools.c` | `SetMethod` patches on exec/dos filtered to the debuggee's task, plus the program's own INewlib slots from `main()`; each event records LR and the back chain |
| Profiler | `ProfileModel.*` | `os4_tools.c` | A priority-10 sampler process using `ReadTaskContext`, weighted by EClock time |
| Intuition inspector | `InspectModel.*` | `InspectSnapshot.cpp` | Walks IntuitionBase's screens and windows under `LockIBase`, filtered to the debuggee's ports |
| Git panel, markers, blame | `GitCore.*` (loose objects, packs, deltas, index), `LineDiff.*` | `GitPanel.cpp`, `Integrations.cpp` | Reads the repository directly; writes go through SimpleGit, which has no blame. New Branch writes the ref itself (`sgit checkout` resets work) |
| Hex, icon and picture editors | `HexData.*` | `HexEditor.cpp`, `ResourceEditors.cpp` | Icons through icon.library, pictures through datatypes |
| ARexx | `RexxCmd.*` (command parser) | `RexxPort.cpp` | arexx.class port `AMIFORGE`; the Macros menu lists `PROGDIR:Rexx` and the project's `Rexx/` |
| MCP (assistants) | `McpCmd.*` (escaping, the paged `AF1` envelope, line edits) | `McpPort.cpp` | Machine-readable ARexx commands for the amiga-devbench `amiforge_*` MCP tools; `MCP <cmd>` sets `mcpQuiet` so `ask()` and the file requesters return their cancel value and their text becomes the error. Commands start work and return; the host polls `GETSTATE` |
| distcc | `DistccCmd.*` | `Builder.cpp` | Sets `DISTCC_HOSTS`, `HOME`/`DISTCC_DIR` and `GCC_EXEC_PREFIX`; `tools/distcc-server.sh` runs distccd with the cross-compiler in Docker |
| Explanations | `Explain.*` (rules for GCC/ld messages and crash states) | `BuildUI.cpp`, `DebugUI.cpp` | Rules are tested against real GCC 11.5 output |
| Projects folder and browser | `ProjectIndex.*` (scan two drawers deep, descriptions from `.afp`/README, recent merge, Open Project's start drawer) | `ProjectBrowser.cpp` | Settings in `ENVARC:AmiForge/ProjectsFolder` and `ShowBrowser`; the browser is a normal window handled by the event loop (`browserSignal`/`handleBrowser`) |
| Modal dialogs, Stop Program | `Robust.*` (run/stop states, what a modal dialog does with other windows' input) | `Modal.cpp`, `RunControl.cpp` | See below |
| Languages, Python projects | `sci/Languages.*`, `LangSymbols.cpp` (outlines and masking per language), `LangTools.*` (the checker and runner scripts, the traceback parser, finding the interpreter) | `PythonUI.cpp`, `sci/LexAmiga.cpp`, `SciSetup.cpp` | See below |
| Keys | `KeyBindings.*` (the table of IDE-wide keys) | `MainWindow.cpp` (`handleKey`), `DebugKeys.cpp` | Every binding is unique (a host test checks); a combination with no binding falls back to the key with fewer qualifiers |

**Modal dialogs.** Every modal window runs `beginModal(wo)`, `waitModal(wo)` instead of
`IExec->Wait(its signal)`, and `endModal()`. While one is open, the IDE's other windows (main,
Find, AutoDocs, project browser) get an empty blocking requester (no gadget, menu or key input;
refreshes still happen) and the main window the busy pointer. `waitModal` also waits on those
windows: their input is drained without acting on it, and a click, key or activation there,
or any other window of this task becoming active, brings the dialog back in front. Build and
debugger signals are not waited on, so they stay pending for the main loop (pollBuild can start a
program, a debug session or packaging, which must not happen under a dialog editing the
project). `WA_StayTop` isn't used: the dialogs' own asl/requester.class requesters would open
behind it.

**Stop Program.** Run starts the program with `SystemTags(SYS_Asynch)` and
`NP_NotifyOnDeathMessage`; DOS replies the DeathMessage to the IDE's port when the shell process
ends. Stop Program finds the process by `pr_DeathMessage` inside `ProcessScan` (the list can't
change meanwhile) and signals Ctrl-C; after 3 s (timer.device on the same port) it offers Ctrl-C
again, Ctrl-C/D/E/F, or leaving it. Nothing is ever force-killed. If AmiForge quits while a
program runs, the port is set to `PA_IGNORE` and left allocated with the DeathMessages, so the
late reply is harmless.

**Python projects.** Nothing is compiled. Build writes `LangTools`' checker to
`T:AmiForge-pycheck.py` and runs it once over all `.py` files (`Builder::startPython`): it
calls `compile()` on each and prints `file:line:col: error: SyntaxError: msg`, which the GCC
parser already reads; tracebacks are parsed too. Run writes the runner to `T:AmiForge-pyrun.py`
and starts `python -u runner STOPFILE TBFILE main.py args` through `runSystem` (so Stop Program
and the DeathMessage tracking work) with a 2 MB stack. The port doesn't turn Ctrl-C into
`KeyboardInterrupt`, so Stop Program also creates STOPFILE; the runner's replacement
`time.sleep()` sleeps in 0.25 s slices and raises `KeyboardInterrupt` when it exists. (A watcher
thread with `_thread.interrupt_main()` wedged the QEMU guest: a second thread can starve the
machine on the port.) The runner compiles and `exec`s the script itself instead of
`runpy.run_path`, which makes the path absolute, and the port can't open those paths. When the program ends, TBFILE (the uncaught exception's
traceback, with the runner's frames dropped) goes into the build log. PYTHONHOME/PYTHONPATH
are set as local variables, copied into the new process, when they're unset and the
interpreter's drawer has `lib/os.py`. Debug runs `python -m pdb -c "b file:line" ... -c
continue`.

Each area keeps its code in shared files (`App.h`, `Commands.*`, `MainWindow.cpp`) inside
`// --- <area> ---` blocks (`// --- mcp ---` for the MCP commands: App.h, RexxPort.cpp, RexxCmd.cpp,
Dialogs.cpp, DebugUI.cpp, GitPanel.cpp, main.cpp). Command IDs: lang 1700-, robust 1500-, projects 1400-, editor 1300-1359, designer 1200-, integrations
1000- (macros 1100-1139), debugtools 650-, Edit and Continue 660. The bottom tabs Windows,
Leaks and Profile are at fixed indices 7-9 (`BOTTOM_*`); Git is found by name.

