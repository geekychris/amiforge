# Troubleshooting

## Building

| Problem | What to do |
|---|---|
| "Couldn't run the command (is the SDK installed?)" | Check that `SDK:` is assigned and that `SDK:gcc/bin/gcc` exists |
| "as: Unknown command", or "A build tool wasn't found" | The SDK isn't completely installed: `SDK:gcc/bin` should hold `as` and `ld` (AmiForge puts `SDK:gcc/bin` and `SDK:C` in the build's path itself) |
| C++ builds fail with strange errors in the standard headers (QEMU) | Start `C:QEMUCopyFix` at boot: see [Running under QEMU](14-qemu.md) |
| The IDE seems frozen: the busy pointer, clicks do nothing, "A build is already running" | A dialog (Project Settings, New Project, a requester) is waiting for an answer, probably hidden behind the main window. Clicking or typing in the main window brings it back to the front; answer or cancel it, and the IDE carries on |
| A Python project's build fails in `T:AmiForge-pycheck.py` with "will become a reserved keyword in Python 2.6" | The interpreter is Python 2 (AmigaOS 4.1's `C:python`): set a Python 3 one; see [Which Python](06-building.md#which-python) |
| "AmiForge can't find the Boing BASIC compiler (boing)" | Set **Project > Settings > Boing BASIC command**, or `SetEnv SAVE AmiForge/Boing <path of boing>` |
| A Boing BASIC program stops with "... is not linked into this interpreter" | The project uses an extension: give it its `Extension=` line in **Project > Settings** so Run makes an interpreter with it |

## Running

| Problem | What to do |
|---|---|
| The program you ran hangs | **Build > Stop Program** (Shift-Ctrl-F5) sends it Ctrl-C; see [Running a program](06-building.md#running-a-program). The IDE keeps working |
| The program's drawer can't be deleted after stopping a crashed program | Stop ends the program without its C library clean-up, so a lock it held (such as its current drawer) stays until you reboot |
| A Python script fails with `FileNotFoundError: [Errno 2] N: 'Work:...'` | Older builds of the Python port can't open paths that start with a volume name; use relative paths or device names in your scripts, or update the port |
| An installer from Package Release says "not executable" in a Shell | Archives don't keep the script bit: use `Execute Install <drawer>`, or double-click its icon |

## Debugging

| Problem | What to do |
|---|---|
| A breakpoint is a hollow circle | It's disabled, or the line has no code; look in the Breakpoints tab |
| Locals are empty or say `<unreadable>` | Build with `-gstabs` (C) or `-gstabs+` (C++), and without optimising (`-O0`) |
| The debugger stopped "in Kickstart" | The program crashed in the OS, usually from a bad pointer it passed in; the call stack shows the program's frame that made the call |
| "Pausing... (in AmigaOS call)" doesn't end | The program is waiting in the OS (for an event on its window, a signal, a timer). Give it what it's waiting for, or press Pause again to cancel |
| Debug Log: "The program couldn't be stopped" | Stop waits a second and a half for the program to come back from an AmigaOS call; one that never does is left suspended, with its code in memory, rather than risking a crash. It does no harm, but don't start it again by other means; reboot when it suits you |
| Edit and Continue says it can't apply a change | The Build tab says why: a new or resized variable, a function the program doesn't link, assembler. Restart the program; see [What you can change](09-edit-and-continue.md#what-you-can-change) |

## The editor and help

| Problem | What to do |
|---|---|
| F1 says the SDK isn't installed | The AutoDocs come from `SDK:Documentation/AutoDocs` |
| The editor shows two or three columns of code | The split view or the minimap is on: click the pressed **Split** or **Map** button |
| The right mouse button shows the menu bar over the editor | That's what it does: hold **Shift** (or **Ctrl**) for the editor's context menu |
| **Amiga-D** opens an include file instead of starting the debugger | Both **Edit > Open Include File** and **Debug > Start / Continue** have the key D, and Intuition takes the first: use **F5** to debug |

## Git

| Problem | What to do |
|---|---|
| Clone fails with "target OID ... doesn't exist" | The drawer is on a disk that cuts names at 30 characters; use one with long names |
| Clone fails with "failed to connect" | No route to the internet: check `ShowNetStatus` for a default gateway |

## ARexx and assistants

| Problem | What to do |
|---|---|
| "the devbench isn't connected to the Amiga bridge" | Start `amiga-bridge` on the Amiga; the devbench's page must show green |
| "AmiForge isn't running: there is no ARexx port AMIFORGE" | Start AmiForge (or use `amiforge_launch`). A second copy is `AMIFORGE.1`: pass `port` |
| "AmiForge didn't answer within 10 s" | AmiForge shows a window or requester of its own: look with `amiforge_screenshot` and close it |
| A build "failed" with no diagnostics | The end of the build log comes back as `log_tail`: a missing SDK tool, or the link step |
| `amiforge_run` returns no output | The program writes to its own window, or is still running: ask `amiforge_run_output` later |
