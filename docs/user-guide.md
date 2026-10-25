# AmiForge user guide

AmiForge is an integrated development environment for AmigaOS 4.1 on PowerPC. It runs on the
Amiga itself: you edit, build with the SDK's GCC, debug at the source level, read the AutoDocs
and use Git without leaving it.

![Debugging in AmiForge](images/debugging.png)

## Contents

1. [Requirements](#requirements)
2. [Installing](#installing)
3. [A five-minute tour](#a-five-minute-tour)
4. [The window](#the-window)
5. [Editing](#editing)
6. [Completion and finding your way around](#completion-and-finding-your-way-around)
7. [AutoDocs](#autodocs)
8. [Projects](#projects)
9. [Building and running](#building-and-running)
10. [Debugging](#debugging)
11. [Git and GitHub](#git-and-github)
12. [The examples](#the-examples)
13. [Running under QEMU](#running-under-qemu)
14. [Troubleshooting](#troubleshooting)
15. [Keyboard reference](#keyboard-reference)
16. [Files and settings](#files-and-settings)

---

## Requirements

| What | Why |
|---|---|
| AmigaOS 4.1 Final Edition (exec 53.70 or newer) | AmiForge uses ReAction, and the exec debug interface for the debugger |
| A true-colour or high-colour Workbench screen | The editor works on 8-bit screens too, but looks best in 16/32-bit |
| **The AmigaOS 4.1 SDK (54.x) with GCC 11**, installed as `SDK:` | The compilers (`gcc`, `g++`, `as`, `ld`), the headers, and the AutoDocs |
| SimpleGit (`sgit`, in the SDK's `sgit.lha`) | Only for the Git menu |
| Networking with a default gateway | Only for cloning, pulling and pushing |
| A disk with long file names (FFS `DOS\7`, SFS/02, NGFS) | Only for Git repositories (see [Git and GitHub](#git-and-github)) |

## Installing

1. **Install the SDK.** Use the SDK's own installer and include GCC 11, or see
   [Installing the SDK without its GUI](#installing-the-sdk-without-its-gui). Afterwards `SDK:` is
   assigned at boot, and `gcc -v` works in a Shell.
2. **Install AmiForge.** Unpack `AmiForge.lha` and double-click **Install-AmiForge**, or run it
   from a Shell with the destination: `Install-AmiForge Work:Development`. You can also just copy
   the `AmiForge` drawer anywhere.
3. **Under QEMU only:** copy `AmiForge/Tools/QEMUCopyFix` to `C:` and add
   `Run >NIL: C:QEMUCopyFix` to `S:User-Startup` (see [Running under QEMU](#running-under-qemu)).
4. **Start AmiForge**: double-click its icon, or a project's `.afp` icon, or from a Shell run
   `AmiForge [project.afp | file.c ...]`. Started without arguments, it reopens the last project.

### Installing the SDK without its GUI

`tools/sdk/make-install-script.py` (in the source repository) reads the SDK's `package.xml`.
It writes an AmigaDOS script that unpacks base, GCC 11, the binutils, newlib, pthreads and the
Unix tools, and runs their post-install steps, as the SDK installer would:

```
tools/sdk/make-install-script.py SDK_Install/package.xml DH1:SDK-Install DH0:SDK > install-sdk
```

Copy the SDK's `.lha` files and `install-sdk` to `DH1:SDK-Install` on the Amiga, then:

```
execute DH1:SDK-Install/install-sdk
```

Add these lines to `S:User-Startup`:

```
assign SDK: DH0:SDK
execute SDK:S/sdk-startup
```

## A five-minute tour

1. Open **Examples/hello/hello.afp** (Project > Open Project, or double-click it).
2. Press **F7**. The Build tab shows the compile and link commands and then "Build succeeded".
3. Press **Ctrl-F5**. The program runs in a console window.
4. Click in the margin next to line 22 (the `list = list_push(...)` line) to set a breakpoint
   (a red circle).
5. Press **F5**. The program starts under the debugger and stops at `main()`. Press **F5** again
   to run on to the breakpoint. The yellow arrow shows where it stopped.
6. Look at **Locals** on the right: `total`, `i`, and the `names` array (click its arrow).
   Press **F10** to step over a line and **F11** to step into `square()`. Changed values turn
   red.
7. Put the cursor on `printf` and press **F1** to read its AutoDoc. Close the AutoDocs window.
8. On an empty line type `list_` and pick from the list that pops up.
9. Press **Shift-F5** to stop.

## The window

- **Toolbar**: New, Open and Save; Build and Run; Debug/Continue, Step Over, Step Into, Step Out
  and Stop; Find; Docs.
- **Project**: the project's files, sources first. Click one to open it.
- **Editor**: one tab per file. A tab's close gadget closes that file, and a `*` means unsaved
  changes. The margins hold:
  - line numbers;
  - markers: a red circle for a breakpoint (hollow if disabled), a yellow arrow and highlight
    where the program is stopped, a green arrow for the selected stack frame, and a red or amber
    background on lines with compiler errors or warnings;
  - fold boxes, which collapse and expand blocks.
- **Right panel**: **Locals**, **Watches** and **Registers**, filled in while debugging.
- **Bottom panel**:
  - **Build**: compiler and Git output. Click an error to go to it.
  - **Find**: the results of Find in Files.
  - **Call Stack**, **Breakpoints**, **Memory** and **Disassembly**.
  - **Debug Log**: what the debugger did and why it stopped.
- **Status bar**: the cursor position on the left, and messages on the right.

Drag the bars between the panes to resize them.

## Editing

The editor is a port of [Scintilla](https://www.scintilla.org/). It highlights C, C++,
PowerPC assembler (`.s`, `.S`, `.asm`, `.i`; GNU `as` syntax with registers, directives and
labels), makefiles and diffs. It also matches braces and auto-indents (after `{`, and a `}`
lines up with its opening line). Files are Latin-1, the Amiga's character set.

| Action | Keys |
|---|---|
| Undo / redo | Amiga-Z / Amiga-Y (or Ctrl-Z / Ctrl-Y) |
| Cut / copy / paste (system clipboard) | Amiga-X / Amiga-C / Amiga-V |
| Select all | Edit > Select All |
| Find / replace | Amiga-F / Amiga-R |
| Find next / previous | F3 / Shift-F3 |
| Find in Files (the whole project) | Amiga-H |
| Go to line | Amiga-L |
| Comment or uncomment the selected lines | Amiga-/ |
| Indent / unindent | Amiga-] / Amiga-[, or Tab / Shift-Tab with a selection |
| Bigger / smaller / normal text | Amiga-+ / Amiga-- / Amiga-0 |
| Wrap long lines, show whitespace, fold all | View menu |

The Find window has Match case, Whole word and Regular expression options, plus Replace and
Replace All. A Replace All can be undone in one step.

## Completion and finding your way around

### Completion

Completion pops up by itself as you type:

| You type | You get |
|---|---|
| 3 letters of a name | Matching words from the open files and project files, C/C++ keywords, and every OS function in the AutoDocs |
| `IExec->`, `IIntuition->`, `IDOS->`... | That library's functions |
| `os::`, `MyClass::`, `std::` | What's declared inside that namespace or class |
| `win.` or `win->` | The members of `win`'s type: your own classes, OS structs (`struct Window`), and `std::vector`, `std::string` and `std::map` |
| `(` after an OS function | A calltip with its prototype |
| **Ctrl-Space** | The list at any point, even with no letters typed |

In the list, use Up and Down, then Return or Tab to insert, or Esc to close it.

![Members of a struct from the SDK headers](images/completion-struct.png)

Completion reads the current file, the project's files and the headers they `#include`
(following them into the SDK) without compiling anything. It works for declarations written in
the usual way. It doesn't follow `auto`, typedef chains or expressions such as `a.get()->b`;
Ctrl-Space still gives the general list there.

### Headers and definitions

| To... | Do this |
|---|---|
| Open a header named in an `#include` | **Ctrl-click** the line, or put the cursor on it and press **F12** (or Amiga-D) |
| Go to where something is defined | **Ctrl-click** it, or **F12** (or Amiga-J) |

- **Headers:** `"quoted"` headers are looked for next to the file, then in the project.
  `<angled>` headers are looked for in the SDK's include drawers: the OS includes, newlib, the
  C++ library and GCC's own headers. SDK headers open read-only.
- **Definitions:** Go to Definition looks for, in order:
  1. the nearest declaration above the cursor (local variables and parameters);
  2. a struct, class, union, enum, `#define` or typedef in the project;
  3. a function in the project;
  4. for an OS function, its AutoDoc.

### The context menu

Click the **right mouse button** in the editor for a menu of what applies to the word or line
under the pointer:
- open the `#include`d header, go to the definition, its AutoDoc, or complete the word;
- toggle a breakpoint, run to the cursor, or add a watch for the word;
- cut, copy and paste;
- Keyboard Shortcuts.

![The editor's context menu](images/context-menu.png)

## AutoDocs

The SDK documents every library function in its AutoDocs (`SDK:Documentation/AutoDocs`).
AmiForge reads them directly:

- **F1** (or the Help key, or the **Docs** button) on a function name opens its entry. Interface
  calls such as `IIntuition->OpenWindowTags` work, and so do the `Tags`/`TagList`/`A` variants of
  a name.
- **Help > AutoDocs Browser** lists every entry. Type in **Find** to narrow the list.
- **Links:** names of other functions in an entry (`exec.library/AllocVec`, `OpenWindow()`) are
  links. Click one to follow it, **Back** returns, and double-clicking any word looks it up.
- **SDK Documentation** opens the SDK's documentation drawer, with the SDK manual (PDF) and the
  guides for the compiler and tools.

![The AutoDocs browser](images/autodocs.png)

## Projects

A project is an `.afp` file in the project's drawer. It lists the sources (relative to that
drawer) and the build settings, and remembers breakpoints and watches.

- **Project > New Project** asks where to save the `.afp`. It adds the C, C++, assembler and
  header files already in that drawer, or writes a starter `main.c` if there are none. Saved
  projects get a Workbench icon that opens them in AmiForge.
- **Project > Open Project**, or **Open Recent**: the last ten projects.
- **Add Files** and **Remove File** change the file list. Removing doesn't delete the file.
- **Project > Settings**:

| Setting | Default |
|---|---|
| Program file | the project's name |
| C compiler / C++ compiler | `gcc` / `g++` (from `SDK:gcc/bin` if they aren't in the path) |
| C flags | `-O0 -gstabs -Wall` |
| C++ flags | `-O0 -gstabs+ -Wall` |
| Assembler flags | `-gstabs` |
| Linker flags / libraries | (none) / `-lauto` |
| Program arguments | (none) |
| Console | a new console window; or a file or console spec such as `CON:0/0/640/200/Out/CLOSE` |
| Stop at main() when debugging | on |

Keep `-gstabs` (C) and `-gstabs+` (C++) in the flags to debug at the source level. The debugger
reads stabs, the debug format OS4's own tools use.

Without a project, Build, Run and Debug work on the file being edited, as a one-file project.

## Building and running

- **F7** (or Amiga-B) builds. Only the sources that changed, or whose headers changed, are
  compiled again. **Rebuild All** and **Clean** are on the Build menu, and **Compile File**
  (Amiga-K) compiles just the current file.
- Errors and warnings are listed in the Build tab and marked in the editor. Click one, or press
  **F4** / **Shift-F4**, to go to it.
- Object files go into `obj/` in the project drawer.
- C++ programs are linked with `-athread=native`, which the C++ runtime needs.
- The compilers run with a 4 MB stack.
- The build runs in the background, so you can keep editing. Build > Stop Build ends it.

**Ctrl-F5** (or F6, Amiga-E) builds if needed, then runs the program in a console window in the
project's drawer, with the program arguments from the project settings.

## Debugging

**F5** builds if needed and starts the program under the debugger. If "Stop at main()" is on,
it stops at the start of `main`.

| Action | Keys |
|---|---|
| Start / continue | F5 |
| Stop | Shift-F5 |
| Step over (the next line; calls run at full speed) | F10 |
| Step into (into functions that have source) | F11 |
| Step out (until the current function returns) | Shift-F11 |
| Run to the line with the cursor | Ctrl-F10, or right-click > Run to Cursor |
| Step one machine instruction | F8 |
| Toggle a breakpoint | F9, or click the margin |

![Stopped at a breakpoint](images/breakpoint.png)

### The panels

- **Locals**: the parameters and local variables of the selected frame. Structs, arrays and
  pointers open with their arrow. Values that changed since the last stop are red.
- **Watches**: expressions evaluated at every stop. Add one with the field under the list, from
  Debug > Add Watch (which uses the selection), or with right-click > Add Watch. Expressions can
  use:
  - variables, `.`, `->`, `[i]`, `*`, `&` and casts;
  - arithmetic and comparisons;
  - registers (`$r3`, `$pc`, `$lr`).
- **Registers**: the PowerPC registers. Changed ones are red.
- **Call Stack**: click a frame to see its source line (green arrow), locals and watches.
- **Breakpoints**: the checkbox enables or disables a breakpoint, Delete removes it, and a click
  goes to it. A breakpoint on a line without code moves to the next line that has some. A
  breakpoint can carry a condition; it then stops only when the condition is non-zero.
- **Memory**: type an address (`0x1234ABCD`) or an expression (`p`, `&buf`, `list->next`). A
  pointer shows the memory it points to.
- **Disassembly**: the code around the PC, with the source lines in between. `=>` marks the
  current instruction and `*` marks a breakpoint.

![Registers and disassembly](images/registers-disassembly.png)

### Crashes

If the program crashes (a DSI, an alignment error, an illegal instruction...), the debugger
stops it instead of the Grim Reaper. You see the source line, the call stack and the registers.
For a DSI, the DAR register holds the bad address. Shift-F5 ends the program.

### Assembler

Assembler sources built with `-gstabs` step line by line like C. **F8** steps single
instructions anywhere, and the Disassembly tab shows where you are.

### How it works

The debugger is AmiForge's own; it doesn't use GDB:
- It runs the program in a process that has an exec debug hook.
- It plants `trap` instructions for breakpoints and steps in software, by decoding the branch at
  the PC.
- It reads the program's stabs for lines, functions, types and variables.
- Traps only ever go into the program's own code, never into the OS.

## Git and GitHub

The **Git** menu works on the repository that holds the project, meaning the nearest drawer
above it with a `.git` drawer. It uses SimpleGit (`SDK:C/sgit`), and its output appears in the
Build tab.

| Command | What it does |
|---|---|
| Status, Diff, Log | Show the repository's state |
| Commit... | Saves everything, adds the project's files and the `.afp`, and commits with your message |
| Pull, Push | Pull or push the current branch from or to `origin` |
| Clone Repository... | Clones a URL into a drawer you choose, then opens its `.afp` or offers to make a project |
| GitHub Setup... | Your name and email for commits, and your GitHub user name and personal access token |

Getting set up:

1. **Git > GitHub Setup**: enter your name and email. For pushing, also enter your GitHub user
   name and a personal access token (github.com > Settings > Developer settings > Personal access
   tokens, with repository access). They're kept in `ENVARC:AmiForge/`.
2. **Git > Clone Repository**: paste the URL (`https://github.com/user/project`) and choose a
   drawer on a disk with long file names.

Notes:
- Without a token, Push and Pull open a console window where SimpleGit asks for your user name
  and token.
- **Certificates:** SimpleGit's built-in certificate list is older than GitHub's current
  certificates, so AmiForge sets `GIT_SSL_NO_VERIFY`. The connection is still encrypted, but the
  server's certificate isn't checked.
- **Long file names:** git's object and pack files have names of 38 characters or more. Classic
  FFS (`DOS\1`–`DOS\5`) cuts names at 30 characters, which breaks a repository: cloning fails
  with "target OID for the reference doesn't exist". Keep repositories on long-name FFS
  (`DOS\7`), SFS/02 or NGFS. `RAM:` is fine for a quick try.
- **Networking:** check with `ShowNetStatus` that there is a default gateway.

## The examples

| Example | What it shows |
|---|---|
| `hello` | C. A linked list across two files, a good first program to step through |
| `shapes` | C++. Classes, virtual functions, `std::vector`, `std::unique_ptr` |
| `asmdemo` | C calling a PowerPC assembler function (`fastsum.s`); step into it with F11 and F8 |
| `bouncer` | C++ using the OS. RAII wrappers for an Intuition window with a GadTools menu, graphics.library drawing, IDCMP input and timer.device pacing. Click in its window to add balls |

![The bouncer example](images/bouncer.png)

## Running under QEMU

AmiForge works on QEMU's `sam460ex` machine with AmigaOS 4.1 FE, with one fix needed:

- **QEMUCopyFix**: QEMU doesn't emulate the 460EX's DMA engine, which exec uses for large memory
  copies, so copies of 256 KB or more silently lose data. GCC's C++ compiler then reads
  corrupted headers and fails with nonsense errors. Run `C:QEMUCopyFix` at boot (from
  `S:User-Startup`). It makes `CopyMem`/`CopyMemQuick` copy with the CPU. Real hardware doesn't
  need it.
- **Speed:** compiles are slow under emulation. A small C file takes a few seconds; a C++ file
  that includes `<iostream>` takes about two minutes. Under the debugger, each breakpoint hit or
  step costs 10–30 ms.

## Troubleshooting

| Problem | What to do |
|---|---|
| "Couldn't run the command (is the SDK installed?)" | Check that `SDK:` is assigned and `SDK:gcc/bin/gcc` exists |
| "as: Unknown command" or "A build tool wasn't found" | The SDK install is incomplete: `SDK:gcc/bin` should hold `as` and `ld` (AmiForge adds `SDK:gcc/bin` and `SDK:C` to the build's path itself) |
| C++ builds fail with strange errors in standard headers (QEMU) | Run `C:QEMUCopyFix` (see above) |
| A breakpoint shows a hollow circle | It's disabled, or the line has no code; check the Breakpoints tab |
| Locals are empty or say `<unreadable>` | Build with `-gstabs` / `-gstabs+` and without optimisation (`-O0`) |
| The debugger stopped "in Kickstart" | You paused in OS code; use Step Out or Continue |
| F1 says the SDK isn't installed | The AutoDocs come from `SDK:Documentation/AutoDocs` |
| Clone fails with "target OID ... doesn't exist" | The drawer is on a disk with 30-character names; use one with long names |
| Clone fails with "failed to connect" | No route to the internet: check `ShowNetStatus` for a default gateway |

## Keyboard reference

| Keys | Action |
|---|---|
| F1 / Help | AutoDoc for the word at the cursor |
| Ctrl-Space | Complete the word |
| Ctrl-click / F12 | Open an `#include` / go to a definition |
| Right mouse button | Context menu |
| F3 / Shift-F3 | Find next / previous |
| F4 / Shift-F4 | Next / previous error |
| F5 | Debug / continue |
| Shift-F5 | Stop debugging |
| Ctrl-F5 / F6 | Build and run |
| F7 | Build |
| F8 | Step one instruction |
| F9 | Toggle breakpoint |
| F10 / F11 / Shift-F11 | Step over / into / out |
| Ctrl-F10 | Run to cursor |
| Amiga-N / O / S / A / W | New / open / save / save as / close file |
| Amiga-P | Open project |
| Amiga-F / R / H / L | Find / replace / find in files / go to line |
| Amiga-D / J | Open include / go to definition |
| Amiga-/ | Comment / uncomment |
| Amiga-B / K / E | Build / compile file / run |
| Amiga-Q | Quit |

Help > Keyboard Shortcuts shows this list in AmiForge.

## Files and settings

| File | What's in it |
|---|---|
| `project.afp` | A project: an ini-style text file with `[project]` settings, `[files]`, `[breakpoints]` and `[watches]` |
| `obj/` | Object files, in the project's drawer |
| `ENVARC:AmiForge/RecentProjects` | The Open Recent list |
| `ENVARC:AmiForge/GitName`, `GitEmail`, `GitHubUser`, `GitHubToken` | Git settings |

An example `.afp`:

```ini
[project]
name=hello
output=hello
cflags=-O0 -gstabs -Wall
libs=-lauto
stopatmain=1

[files]
file=main.c
file=list.c
file=list.h

[breakpoints]
bp=main.c:22:1

[watches]
watch=total
```
