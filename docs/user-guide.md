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
