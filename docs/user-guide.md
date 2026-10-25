# AmiForge user guide

## Installing

1. Install the AmigaOS 4.1 SDK with GCC 11 so that `SDK:` exists. Either use the SDK's own
   installer, or copy the SDK archives to the Amiga and run the script that
   `tools/sdk/make-install-script.py` generates.
2. Copy the `AmiForge` drawer anywhere, for example `Work:Development/AmiForge`.
3. Double-click AmiForge, or open a project file (`.afp`) from Workbench.

From a Shell: `AmiForge [project.afp | file.c ...]`.

## The window

- **Toolbar**: New, Open and Save; Build and Run; Debug/Continue, Step Over, Step Into,
  Step Out and Stop; Find.
- **Project**: the project's files. Click a file to open it.
- **Editor**: one tab per open file. The close gadget on a tab closes that file. The margin
  shows line numbers and markers:
  - a red circle is a breakpoint (a hollow one is disabled);
  - a yellow arrow is where the program is stopped, and a green arrow is the selected stack
    frame;
  - a red or amber line is a compiler error or warning.
- **Right panel**: Locals, Watches and Registers, while debugging.
- **Bottom panel**:
  - Build: the compiler output. Click an error to go to it.
  - Find: Find in Files results.
  - Call Stack, Breakpoints, Memory and Disassembly.
  - Debug Log.
- **Status bar**: the caret position on the left, and messages on the right.

All the panes can be resized by dragging the bars between them.

## Editing

The editor highlights C, C++, PowerPC assembler (`.s`, `.S`, `.asm`, `.i`), makefiles and
diffs. Files are Latin-1 (the Amiga character set).

| Action | Keys |
|---|---|
| Undo / redo | Amiga-Z / Amiga-Y (or Ctrl-Z / Ctrl-Y) |
| Cut / copy / paste | Amiga-X / Amiga-C / Amiga-V (the system clipboard) |
| Find, replace | Amiga-F, Amiga-R; F3 / Shift-F3 for next / previous |
| Find in Files | Amiga-H |
| Go to line | Amiga-L |
| Comment / uncomment lines | Amiga-/ |
| Indent / unindent | Amiga-] / Amiga-[ (or Tab / Shift-Tab with a selection) |
| Bigger / smaller text | Amiga-+ / Amiga-- |

Folding: click the [-] in the fold margin, or use View > Fold All / Unfold All.

## Finding your way around

**Right-click in the editor** for a menu of what applies to the word or line under the
pointer:
- Open the header of an `#include`, or go to a definition.
- Its AutoDoc, or complete the word.
- Toggle a breakpoint, run to the cursor, or add a watch.
- Cut, copy and paste.
- Keyboard Shortcuts.

| To... | Do this |
|---|---|
| Open a header named in an `#include` | Ctrl-click the line, or put the cursor on it and press F12 (or Amiga-D) |
| Go to where something is defined | Ctrl-click it, or F12 / Amiga-J |
| Read an OS function's documentation | F1, the Help key or the **Docs** button. Help > AutoDocs Browser shows them all |
| Complete a name | Type 3 letters and pick from the list, or press Ctrl-Space at any time |
| See an OS library's functions | Type `IExec->`, `IIntuition->`, `IDOS->`... |
| See a function's arguments | Type `(` after an OS function's name |
| See all the keys | Help > Keyboard Shortcuts, or right-click > Keyboard Shortcuts |

How lookups work:
- **Headers:** `"quoted"` names are looked for next to the file, then in the project. `<angle>`
  names are looked for in the SDK's include directories (OS includes, newlib, the C++ library
  and GCC's own headers). SDK headers open read-only.
- **Go to Definition** looks for the nearest declaration above the cursor (local variables and
  parameters), then for a struct, class, union, enum, `#define` or typedef in the project, then
  for a function. For an OS function it opens the AutoDoc.
- **In the completion list,** use Up/Down and Return or Tab to insert, and Esc to close it.

## Projects

A project file (`.afp`) lists the sources (relative to the project's drawer) and the build
settings. It also remembers breakpoints and watches. Use **Project > New Project**: AmiForge
adds the C, C++, assembler and header files already in that drawer, or creates a `main.c` if
there are none.

**Project > Settings** sets:

| Setting | Default |
|---|---|
| Program file | the project name |
| C compiler / C++ compiler | `gcc` / `g++` |
| C flags | `-O0 -gstabs -Wall` |
| C++ flags | `-O0 -gstabs+ -Wall` |
| Assembler flags | `-gstabs` |
| Linker flags / Libraries | (none) / `-lauto` |
| Program arguments | (none) |
| Console | a new window; or e.g. `CON:0/0/640/200/Out/CLOSE` |
| Stop at main() when debugging | on |

Keep `-gstabs` / `-gstabs+` in the flags if you want to debug at the source level. The debugger
reads stabs, the format OS4's own tools use.

Without a project, Build, Run and Debug work on the file being edited.

## Building

- **F7** (or Amiga-B) builds. Only files that changed, or whose headers changed, are
  recompiled. Rebuild All and Clean are on the Build menu, and Compile File (Amiga-K) compiles
  just the current file.
- Errors and warnings appear in the Build tab and as marked lines in the editor. **F4** and
  **Shift-F4** move between them.
- Objects go into `obj/` in the project drawer.
- C++ programs are linked with `-athread=native`, which the C++ runtime needs.
- Compilers run with a 4 MB stack.
- **Under QEMU** (the sam460ex machine), run `C:QEMUCopyFix` first, for example from `S:User-Startup`. QEMU doesn't emulate the 460EX's DMA engine, which exec uses for large memory copies, so without the fix `cc1plus` reads corrupted data and fails on C++ headers. Real hardware doesn't need it.

**Ctrl-F5** (or F6, Amiga-E) builds, then runs the program in a console window.

## Debugging

**F5** builds the program and starts it under the debugger, stopping at `main()` if that
setting is on.

| Action | Keys |
|---|---|
| Continue | F5 |
| Step over (the next line, calls run at full speed) | F10 |
| Step into (into functions that have source) | F11 |
| Step out (until the current function returns) | Shift-F11 |
| Run to the line with the caret | Ctrl-F10 |
| Step one machine instruction | F8 |
| Stop the program | Shift-F5 |
| Toggle a breakpoint | F9, or click the margin |

### Panels

- **Breakpoints**: the checkbox enables or disables a breakpoint; Delete removes it.
  A breakpoint on a line without code moves to the next line that has code.
- **Locals**: the parameters and local variables of the selected frame. Structs, arrays and
  pointers open with the arrow. Values that changed since the last stop are shown in red.
- **Watches**: expressions evaluated at every stop. Add them with the field under the list, or
  with Debug > Add Watch (the selection is used). Expressions can use:
  - variables, `.`, `->`, `[i]`, `*`, `&` and casts;
  - arithmetic and comparisons;
  - registers (`$r3`, `$pc`, `$lr`).
- **Registers**: the PowerPC registers. Changed ones are in red.
- **Call Stack**: click a frame to see its source line and locals.
- **Memory**: type an address (`0x1234ABCD`) or an expression (`p`, `&buf`, `list->next`).
  A pointer shows the memory it points to.
- **Disassembly**: the code around the PC. `=>` marks the current instruction and `*` marks a
  breakpoint.

### Crashes

If the program crashes (a DSI, alignment error, illegal instruction and so on), the debugger
stops it instead of the Grim Reaper. You see the line, the call stack and the registers; the
DAR register holds the bad address for a DSI. Stop with Shift-F5.

### Assembler

Assembler files built with `-gstabs` step line by line like C. **F8** steps single
instructions anywhere, and the Disassembly tab shows where you are.

## AutoDocs

The AmigaOS 4 SDK documents every library function in its AutoDocs
(`SDK:Documentation/AutoDocs`).

- **F1** (or the Help key) on a function name in the editor opens its entry. AmiForge accepts
  interface calls (`IIntuition->OpenWindowTags`) and the `Tags`/`TagList`/`A` variants of a name.
- **Help > AutoDocs Browser** lists every entry. Type in Find to narrow it down.
- In an entry, names of other functions (`exec.library/AllocVec`, `OpenWindow()`) are links.
  Click one to go there, and use **Back** to return. Double-click any word to look it up.
- **SDK Documentation** opens the SDK's documentation drawer, which has the SDK manual (PDF) and
  the guides for the compiler and tools.

## Git and GitHub

The Git menu works on the repository that contains the project, meaning the nearest drawer
above it with a `.git` drawer. It uses SimpleGit (`SDK:C/sgit`, installed from the SDK's
`sgit.lha`). Output appears in the Build tab.

| Command | What it does |
|---|---|
| Status, Diff, Log | Show the repository's state |
| Commit... | Saves everything, adds the project's files and the `.afp`, and commits with your message |
| Pull, Push | Pull or push the current branch from or to `origin` |
| Clone Repository... | Clones a URL into a drawer you pick, then opens its `.afp` or offers to make a project |
| GitHub Setup... | Sets your name and email for commits, and your GitHub user name and personal access token |

Push and pull use the token you set in GitHub Setup (kept in `ENVARC:AmiForge/GitHubToken`).
Without one, they open a console window where SimpleGit asks for your user name and token.
SimpleGit's built-in certificate list is older than GitHub's current certificates, so AmiForge
sets `GIT_SSL_NO_VERIFY`. The connection is still encrypted, but the certificate isn't checked.

The Amiga needs working networking with a default gateway (check with `ShowNetStatus`).

**Repositories need a disk with long file names.** Git's object and pack files have names of
38 characters or more. Classic FFS (`DOS\1`–`DOS\5`) cuts names at 30 characters, which breaks a
repository: cloning fails with "target OID for the reference doesn't exist". Use a partition
formatted as long-name FFS (`DOS\7`), SFS/02 or NGFS, or `RAM:` for a quick try. On the test
setup, the dev disk (DH1:) was converted to `DOS\7` for this.
