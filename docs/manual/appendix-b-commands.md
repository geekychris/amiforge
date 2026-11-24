# Appendix B: ARexx and MCP commands

Every command of AmiForge's ARexx port, with its template (in the form of AmigaDOS's
`ReadArgs()`: `/A` must be given, `/N` is a number, `/S` a switch, `/F` takes the rest of the
line) and what it does. The table is made from AmiForge's source. Commands marked (MCP) are
also used by the MCP tools; the commands after the first table are meant for scripts and
assistants, and are sent wrapped as `MCP <command>` (see [ARexx and MCP
control](13-arexx-mcp.md#the-format)). In AmiForge, **Macros > ARexx Commands...** and
`HELP COMMAND` give the same information.

<!-- generated below by tools/manual-ref.py: don't edit what follows -->

## Classic commands

| Command | Template | What it does |
|---|---|---|
| `OPEN` | `FILE/A/F` | Open a file in the editor |
| `SAVE` |  | Save the current file |
| `SAVEAS` | `FILE/A/F` | Save the current file under a new name |
| `CLOSE` | `FORCE/S` | Close the current file (FORCE: without asking about changes) |
| `GOTO` | `LINE/N/A,COL/N` | Move the cursor to a line (and column) |
| `GETLINE` |  | RESULT = the cursor's line number |
| `GETCOL` |  | RESULT = the cursor's column |
| `GETFILE` |  | RESULT = the current file's path |
| `GETTEXT` | `ALL/S` | RESULT = the selection ("" if none); with ALL the whole file |
| `GETLINETEXT` | `LINE/N` | RESULT = the text of a line (default: the cursor's) |
| `GETWORD` |  | RESULT = the word at the cursor |
| `INSERT` | `TEXT/A/F` | Insert text at the cursor (\n for a new line) |
| `REPLACE` | `TEXT/F` | Replace the selection with text |
| `SELECT` | `FROMLINE/N/A,TOLINE/N/A` | Select whole lines |
| `FIND` | `TEXT/A/F` | Find text from the cursor; RC 5 if not found |
| `BUILD` |  | Build the project |
| `RUN` |  | Build and run the project |
| `DEBUG` |  | Start debugging (or continue) |
| `STOP` |  | Stop debugging |
| `PROJECT` | `FILE/A/F` | Open a project (.afp) |
| `GETPROJECT` |  | RESULT = the project file |
| `STATUS` | `TEXT/F` | Show text in the status bar |
| `REQUEST` | `TEXT/A/F` | Show a message; RESULT = 1 for OK |
| `QUIT` | `FORCE/S` | Quit AmiForge |
| `HELP` | `COMMAND` | RESULT = the list of commands, or one command's template |

## Commands for scripts and assistants

| Command | Template | What it does |
|---|---|---|
| `MCP` | `COMMAND/A/F` | Run a command; RESULT = "AF1 rc more length payload" (always RC 0) |
| `MCPMORE` |  | The next part of the last MCP result |
| `MCPBUF` | `SLOT/N/A,CLEAR/S,TEXT` | Append text to slot 1-9 (refer to it as \@1); CLEAR empties it first |

## The Markdown preview

| Command | Template | What it does |
|---|---|---|
| `PREVIEWMARKDOWN` | `FILE` | Show FILE (or the current .md file) in the Markdown preview (after the reply: MDSTATE says when it is there); RESULT = the file |
| `MDBLOCKS` | `FILE` | The Markdown block tree of FILE (or the preview's file), one block per line, then its links and images |
| `MDLINKS` |  | The links visible in the preview: index<tab>href<tab>screen x<tab>y<tab>w<tab>h<tab>text |
| `MDSTATE` |  | The preview's state: file, blocks, links, images, pictures drawn/loaded/failed, size, scroll |
| `MDCLICK` | `LINK/A` | Follow link number LINK as a click on it would |
| `MDSCROLL` | `Y/A` | Scroll the preview to Y pixels from the top |
| `GETSTATE` |  | RESULT = key=value lines: project, file, build and debugger state |
| `DOCUMENTS` |  | RESULT = the open files: path, modified, current, lines |
| `PROJECTINFO` |  | RESULT = key=value lines of the project's settings |
| `PROJECTFILES` |  | RESULT = the project's files: relative path, full path |
| `PROJECTS` | `DEPTH/N` | RESULT = the projects in the projects folder: name, .afp, description |
| `TEMPLATES` |  | RESULT = the project templates: id, name, description |
| `OPENPROJECT` | `FILE/A` | Open a project (.afp); fails (and says why) if it didn't open |
| `ACTIVATE` | `FILE/A,LINE/N` | Open a file (relative: in the project's drawer) and go to a line |
| `NEWPROJECT` | `TEMPLATE/A,NAME/A,DRAWER/A` | Make a project from a template and open it; RESULT = the .afp |
| `READ` | `FILE,FROMLINE/N,TOLINE/N` | RESULT = "lines<tab>from<tab>to<tab>path" and the text (the open buffer if open) |
| `EDITLINES` | `FILE,FROMLINE/N/A,TOLINE/N/A,TEXT` | Replace lines (TOLINE = FROMLINE-1 inserts); RESULT = the new line count |
| `SUBST` | `FILE,OLD/A,NEW,ALL/S` | Replace exact text (must be unique unless ALL); RESULT = count<tab>line |
| `SAVEALL` |  | Save every modified file |
| `REVERT` | `FILE` | Drop unsaved changes: reload the file from disk |
| `STARTBUILD` | `REBUILD/S,CLEAN/S,CHECK/S` | Start a build now (no requesters); poll GETSTATE for build=idle; CHECK: Boing BASIC's boing check only |
| `STOPBUILD` |  | Stop the running build |
| `ERRORS` | `ALL/S` | RESULT = the last build's messages: kind, file, line, col, text, explanation |
| `BUILDLOG` |  | RESULT = the build log as shown |
| `RUNPROGRAM` | `CAPTURE/S` | Run the built program (no build); CAPTURE: output to a file for RUNOUTPUT |
| `RUNOUTPUT` |  | RESULT = "running=0\|1" and the captured output of RUNPROGRAM CAPTURE |
| `STOPPROGRAM` |  | Send Ctrl-C to the program started with RUNPROGRAM CAPTURE |
| `DEBUGSTART` | `MAIN/S,NOMAIN/S` | Start debugging the built program (no build); MAIN/NOMAIN: stop at main or not |
| `DEBUGSTOP` |  | Stop debugging (kills the program) |
| `CONTINUE` |  | Continue the stopped program |
| `STEP` |  | Step into (one source line) |
| `NEXT` |  | Step over (one source line) |
| `STEPOUT` |  | Run until the current function returns |
| `PAUSE` |  | Stop the running program where it is |
| `RUNTO` | `FILE/A,LINE/N/A` | Continue until a line |
| `BREAK` | `FILE/A,LINE/N/A,COND` | Set a breakpoint (COND: stop only when it's non-zero) |
| `UNBREAK` | `FILE,LINE/N,ALL/S` | Clear a breakpoint, or ALL of them |
| `BREAKS` |  | RESULT = the breakpoints: file, line, enabled, condition, hits, address |
| `LOCALS` |  | RESULT = the selected frame's parameters and locals: name, type, value, children |
| `STACK` |  | RESULT = the call stack: level, function, file, line, pc, module |
| `FRAME` | `LEVEL/N/A` | Select a stack frame for LOCALS and EVAL |
| `EVAL` | `CHILDREN/S,EXPR/A/F` | RESULT = name, type, value of an expression (CHILDREN: and its members) |
| `REGS` |  | RESULT = the registers: name, value |
| `CRASH` |  | RESULT = the explanation of the last crash |
| `SYMBOLS` | `FILE` | RESULT = the outline of a file: kind, name, line, detail |
| `REFERENCES` | `NAME/A` | RESULT = every use of an identifier in the project: file, line, col, text |
| `GITSTATUS` |  | RESULT = "repo<tab>branch", then changed files: XY, path |

## Boing BASIC projects and forms

| Command | Template | What it does |
|---|---|---|
| `DESIGN` | `FILE` | Open the GUI designer on a .afg (none: the project's form); RESULT = file<tab>basic\|c |
| `DESIGNSELECT` | `ID` | Select an object in the designer (the window: its name); RESULT = kind<tab>id |
| `DESIGNEVENTS` | `ID` | RESULT = the selected (or ID's) events: name<tab>code\|empty\|none<tab>params[<tab>default] |
| `GOTOEVENT` | `ID,EVENT` | Open the code-behind at Sub <id>_<event>, made if missing (EVENT: default event); RESULT = file<tab>line<tab>sub |
| `DESIGNSET` | `ID,KEY/A,VALUE/A/F` | Set a property of the selected (or ID's) object; a new id renames its event subs |
| `DESIGNSAVE` |  | Save the designer's .afg |
| `DESIGNADD` | `ID,KIND/A` | Add an object (button, string, timer...) after the selected (or ID's) one, or into the selected group; RESULT = kind<tab>id |
| `BASICMEMBERS` | `NAME/A,FILE` | What completion offers after NAME. in FILE (a control's or a Boing class's members); RESULT = what, then one per line |
| `DESIGNMENUS` |  | Open the designer's Menu Editor (Boing BASIC forms; after the reply, the dialog is modal) |
| `VIEWTOGGLE` |  | View Form / View Code (Alt-F7); RESULT = current file<tab>designer's file |
| `BASICSUBS` | `FILE` | RESULT = the procedures of a Boing BASIC module (or a form's code): kind, name, line, end, code\|empty, params |
| `BASICCONTROLS` |  | RESULT = source=..., then per control kind: kind, BASIC type, default event, events |
