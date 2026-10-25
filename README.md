# AmiForge

A native IDE for **AmigaOS 4.1** (PowerPC), running on the Amiga itself:

- **Editor**: tabbed, multi-file editing on a port of [Scintilla](https://www.scintilla.org/).
  It highlights C, C++, PowerPC assembler, makefiles and diffs, and has line numbers, folding,
  brace matching, auto-indent, find/replace (with regular expressions), Find in Files,
  go to line and comment/uncomment.
- **Toolchain**: the AmigaOS 4.1 SDK's native GCC 11.5, which covers C, C++ and GNU `as` for
  `.s` files. Projects build file by file, with compiler errors you can click to jump to.
- **AutoDocs**: press F1 on an OS function (`OpenWindowTags`, `IExec->AllocVecTags`) to read its
  AutoDoc from the SDK. Help > AutoDocs Browser searches all of them, and references in an entry
  are links.
- **Git and GitHub**: clone, status, diff, log, commit, pull and push from the Git menu, using
  SimpleGit (`sgit`) from the SDK.
- **Debugger**: source-level, on the exec debug interface. It has:
  - breakpoints (with conditions), run to cursor, step over / into / out, and instruction step;
  - locals, watches, registers, call stack, memory and disassembly views;
  - crash reports that point at the source line.

```
+--------------------------------------------------------------------------+
| New Open Save | Build Run | Debug Step-Over Step-Into Step-Out Stop | Find |
+-----------+---------------------------------------------+----------------+
| Project   | [main.c x][list.c x]                        | Locals|Watches |
|  main.c   |  12  int main(void)                         |  total  15     |
|  list.c   |  13  {                                     ^|  i      6      |
|  list.h   |=>14      list = list_push(list, ...        v|  list   0x...  |
+-----------+---------------------------------------------+----------------+
| Build | Find | Call Stack | Breakpoints | Memory | Disassembly | Log     |
+--------------------------------------------------------------------------+
```

## Keys

| Key | Action |
|---|---|
| F5 | Start debugging / continue |
| Shift-F5 | Stop debugging |
| Ctrl-F5 / F6 | Build and run |
| F7 | Build |
| F9 | Toggle breakpoint (or click the margin) |
| F10 / F11 / Shift-F11 | Step over / into / out |
| Ctrl-F10 | Run to cursor |
| F8 | Step one instruction |
| F1 (or Help) | AutoDoc for the function under the cursor |
| F3 / Shift-F3 | Find next / previous |
| F4 / Shift-F4 | Next / previous error |
| Amiga-N/O/S/W | New / open / save / close file |
| Amiga-F/R/H/L | Find / replace / find in files / go to line |
| Amiga-B / Amiga-E | Build / run |

## Projects

A project is an `.afp` file, ini-style, listing the sources and the compiler settings. It also
keeps your breakpoints and watches. The `examples/` directory has three:

- `hello`: C, with a linked list to explore in the debugger.
- `shapes`: C++, with classes, virtual functions and `std::vector`.
- `asmdemo`: C calling a PowerPC assembler function.
- `bouncer`: C++ using the OS. It has RAII wrappers for an Intuition window with a GadTools
  menu, graphics.library drawing, IDCMP input, and timer.device for the frame rate.

Without a project, Build and Run work on the current file.

## Requirements (on the Amiga)

- AmigaOS 4.1 Final Edition (exec 53.70 or newer).
- The AmigaOS 4.1 SDK (54.x) with GCC 11, installed as `SDK:`. `tools/sdk/` has a script that
  installs it without the GUI installer.

## Building AmiForge (cross-compiling)

```
tools/build.sh            # Docker: walkero/amigagccondocker:os4-gcc11
```

It produces `build/AmiForge`. See [docs/DESIGN.md](docs/DESIGN.md) for how it fits together.

## Credits

- Scintilla and Lexilla © Neil Hodgson, under the Historical Permission Notice and Disclaimer
  (`third_party/*/License.txt`).
- AmigaOS 4.1 SDK and GCC (adtools) by their respective authors.
