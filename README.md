# AmiForge

**A native IDE for AmigaOS 4.1 (PowerPC).** It runs on the Amiga itself, with a modern editor,
the SDK's GCC, a source-level debugger, the AutoDocs one key away, and Git.

![AmiForge debugging a C program](docs/images/debugging.png)

## Features

- **Editor**: a port of [Scintilla](https://www.scintilla.org/) to AmigaOS 4.
  - Tabbed, multi-file editing with syntax highlighting for C, C++, PowerPC assembler (GNU `as`),
    makefiles and diffs, and for Python, ARexx, AmigaDOS scripts, Lua, Perl, shell scripts, JSON,
    YAML and Markdown, each with its comment toggle, auto-indent and symbol outline.
  - Line numbers, folding, brace matching and auto-indent.
  - Find/replace with regular expressions, Find in Files, go to line, and comment/uncomment.
  - Light and dark themes, a split view and a minimap (toolbar buttons), multiple cursors and
    snippets with fields.
  - A symbol outline, Find All References and Rename Symbol across the project.
- **Code completion**:
  - pops up as you type;
  - `IExec->` lists exec.library's functions;
  - `os::` lists a namespace's contents;
  - `win->` lists the members of `win`'s type, including OS structs from the SDK headers;
  - calltips show the parameters of OS functions, your functions, constructors and methods, with
    the current argument highlighted.
- **Python 3** (with the [AmigaOS 4 port](https://github.com/geekychris/python-amigaos4)):
  Python projects where Build checks the syntax (errors clickable in the build log), Run runs
  the main script and puts a traceback's lines in the build log, Stop Program stops it, and F5
  offers pdb with the project's breakpoints. Completion of keywords and built-ins, Python
  snippets, a project template and an example.
- **Navigation**: Ctrl-click (or F12) opens an `#include`d header or goes to a definition. A
  context menu on the right mouse button has everything that applies to the word under the
  pointer.
- **AutoDocs**: F1 on an OS function opens its AutoDoc. The browser searches all of them, and
  references between entries are links.
- **Building**: the AmigaOS 4.1 SDK's native GCC 11.5 (C, C++, assembler).
  - Projects build incrementally, in the background.
  - Errors and warnings are marked in the editor and clickable in the build log.
  - Ctrl-F5 builds and runs.
  - Common compiler errors get a plain-language explanation of the fix.
  - Optional distributed builds with distcc (to a cross-compiler on another machine).
  - **Package Release** builds an optimised version and makes an Aminet-ready `.lha` with icons,
    a readme and an install script.
- **Debugging**: AmiForge has its own debugger, built on the exec debug interface; it doesn't
  use GDB.
  - Breakpoints, including conditional ones; step over, into and out; run to cursor or to a
    function; set next statement; pause and restart; instruction stepping. The keys are the ones
    modern IDEs use (F5, F9, F10, F11, Shift-F11, Ctrl-F10, Shift-F9 quick watch...).
  - Locals and watches with expandable structs, arrays and pointers, and changed values in red.
  - Registers, call stack, memory and disassembly views.
  - Crashes are caught and shown at the source line, with an explanation of the likely cause.
  - **Edit and Continue**: change a function while stopped, press Ctrl-F9 and the program carries
    on with the new code, keeping its data.
  - A live **Intuition inspector** of the program's screens, windows and gadgets.
  - A **leak tracker** that lists memory, files, locks, libraries and objects never released,
    at the line that acquired them.
  - A sampling **profiler** that annotates the hottest lines in the editor.
- **Git and GitHub**: clone, status, diff, log, commit, pull and push from the Git menu, using
  SimpleGit from the SDK.
  - A Git panel to stage, unstage, discard, commit and switch or create branches.
  - Change markers in the editor's margin, and blame for each line.
- **GUI designer**: lay out a ReAction window with a live preview and generate its C code;
  hand-written code is kept when you regenerate.
- **Templates**: new projects from ten templates (console C and C++, ReAction app, Intuition
  window with menus, commodity, screen blanker, shared library, device, AHI player, Python script).
- **Hex, icon and picture editors**: binaries open in a hex editor; `.info` icons and IFF/PNG
  pictures open in their own viewers.
- **ARexx**: an `AMIFORGE` port to script the IDE, and a Macros menu of your own `.rexx` macros.
- **AI assistants (MCP)**: Claude Code or another MCP client on the host can open projects,
  read and edit files, build and read the errors, run, set breakpoints and debug, through the
  amiga-devbench's `amiforge_*` tools ([user guide](docs/user-guide.md#controlling-amiforge-from-an-ai-assistant-mcp)).
- **Projects**: `.afp` files with Workbench icons, recent projects, and breakpoints and watches
  that are saved with the project. A projects folder (the examples by default) and a project
  browser that lists every project in it, with descriptions.

| | |
|---|---|
| ![AutoDocs](docs/images/autodocs.png) | ![Completion of SDK struct members](docs/images/completion-struct.png) |
| *F1 opens the AutoDoc; references are links* | *`win->` lists the fields of `struct Window` from the SDK headers* |
| ![Context menu](docs/images/context-menu.png) | ![Registers and disassembly](docs/images/registers-disassembly.png) |
| *Right-click: headers, definitions, docs, debugging* | *Registers, and disassembly with source lines* |
| ![Dark theme, split view and minimap](docs/images/editor-dark-split-minimap.png) | ![The GUI designer](docs/images/designer-window.png) |
| *Dark theme, split view and minimap* | *The GUI designer with its live preview* |
| ![Edit and Continue](docs/images/hotpatch-applied.png) | ![The leak tracker](docs/images/debugtools-leaks.png) |
| *Edit and Continue: Ctrl-F9 while stopped* | *Leaks, at the line that acquired them* |
| ![The Git panel](docs/images/integrations-git-panel.png) | ![The profiler](docs/images/debugtools-profile.png) |
| *The Git panel, with change markers in the editor* | *The profiler, with the hottest lines annotated* |

## Getting started

1. Install the **AmigaOS 4.1 SDK** with GCC 11 (from Hyperion), so that `SDK:` is assigned.
2. Unpack **AmiForge.lha** and run **Install-AmiForge**, or copy the `AmiForge` drawer anywhere.
3. Start AmiForge: the project browser lists the examples. Double-click `hello`, then press **F7** to build, **Ctrl-F5** to run and **F5** to
   debug.

Under QEMU's `sam460ex`, also run `Tools/QEMUCopyFix` at boot. QEMU doesn't emulate the DMA
engine that exec uses for large memory copies, and without it C++ compiles fail.

The **[user guide](docs/user-guide.md)** covers everything: a five-minute tour, editing,
completion and navigation, AutoDocs, projects, building, debugging, Git and GitHub, the
examples, QEMU, troubleshooting, and every key.

## Examples

| Example | What it shows |
|---|---|
| `hello` | C: a linked list across two files, to step through in the debugger |
| `shapes` | C++: classes, virtual functions, `std::vector`, `std::unique_ptr` |
| `asmdemo` | C calling a PowerPC assembler function |
| `bouncer` | C++ using the OS: an Intuition window with a GadTools menu, graphics.library drawing, IDCMP input and timer.device |
| `clock` | C++ using the OS: an analog and digital clock; timer.device ticks, `DateStamp()`, flicker-free drawing through an off-screen friend bitmap, a resizable window |
| `todo` | C++ with ReAction: window.class, layout, a listbrowser with checkboxes, a string gadget and buttons, one class per gadget group; the list is kept in `ENVARC:TodoExample` |
| `dirsize` | C++ Shell tool: `ReadArgs()`, recursive `ExamineDir()`, totals by file type in a `std::map`, sorted output, Ctrl-C |
| `workers` | C++ and exec multitasking: worker processes from `CreateNewProcTags()` report through message ports; Ctrl-C stops them all |
| `sketch` | C++ drawing program: an Intuition window with GadTools menus for pen and brush, mouse drawing, an ASL save requester, IFF ILBM written with iffparse.library |
| `sysinfo-py` | Python: memory, tasks, libraries and free space through the Python port's `_amiga` module |

The examples drawer is the default projects folder: **Project > Browse Projects** (Amiga-U)
lists every project in it with its description.

## Requirements

- AmigaOS 4.1 Final Edition (exec 53.70 or newer).
- The AmigaOS 4.1 SDK (54.x) with GCC 11, as `SDK:`.
- For Git: SimpleGit (`sgit.lha` from the SDK), networking, and a disk with long file names
  (FFS `DOS\7`, SFS/02 or NGFS).

## Building AmiForge

AmiForge is cross-compiled with the adtools GCC 11 toolchain in Docker:

```
tools/build.sh            # build/AmiForge
tools/make-dist.sh        # dist/AmiForge.lha
```

See **[docs/building.md](docs/building.md)** for the details, the source layout and how to test
on an OS4 machine. [docs/DESIGN.md](docs/DESIGN.md) describes the architecture, and
[docs/debugger-notes.md](docs/debugger-notes.md) records what was learned about debugging on
AmigaOS 4.

## Licence

AmiForge is under the MIT licence (see [LICENSE](LICENSE)). It includes Scintilla and Lexilla by
Neil Hodgson, under their own licence (`third_party/*/License.txt`). The AmigaOS 4.1 SDK and GCC
are not included; they come from Hyperion and the adtools project.
