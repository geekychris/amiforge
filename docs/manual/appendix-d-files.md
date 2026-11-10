# Appendix D: Files and settings

## In a project's drawer

| File or drawer | What it is |
|---|---|
| `name.afp` | The project (see below) |
| `obj/` | The object files of a build |
| `obj/<name>.hot.o` | A file compiled for Edit and Continue |
| `obj-release/`, `release/` | Package Release's build, and its archive and readme |
| `*.afg` | A window designed in the GUI designer |
| `<name>_gui.c`, `<name>_gui.h` | The C code the designer generated |
| `name.bbp` | A Boing BASIC project (`Name=`, `Startup=`, `Module=`, `Form=`, `Class=`, `Extension=`, `CSource=`) |
| `Rexx/` | The project's own ARexx macros (the Macros menu) |
| `__pycache__/` | Python's compiled modules; Clean deletes it |

## The project file

A `.afp` is a text file in sections:

```ini
[project]
name=hello
output=hello
cflags=-O0 -gstabs -Wall
libs=-lauto
stopatmain=1
description=C: a linked list across two files, a good first program to step through

[files]
file=main.c
file=list.c
file=list.h

[breakpoints]
bp=main.c:22:1

[watches]
watch=total
```

| Key | Section | What it holds |
|---|---|---|
| `name`, `output` | project | The project's name; the program's file name |
| `cc`, `cxx` | project | The C and C++ compilers |
| `cflags`, `cxxflags`, `asflags`, `ldflags`, `libs` | project | The flags and libraries |
| `objdir` | project | The drawer for object files (`obj`) |
| `args`, `console`, `runcmd` | project | The program's arguments, its console, and a command to run instead of it |
| `stopatmain`, `trackleaks` | project | Debugging options |
| `author`, `version`, `short`, `aminettype`, `description`, `releaseflags` | project | Package Release's details |
| `distcc`, `distcchosts` | project | Distributed builds |
| `language`, `interpreter`, `main` | project | `python` or `basic`; the interpreter or `boing` command; a Python project's main script |
| `bbp` | project | A Boing BASIC project's `.bbp` |
| `file` | files | A file of the project, relative to its drawer |
| `bp` | breakpoints | `file:line:enabled`, with a condition after it when there is one |
| `watch` | watches | An expression in the Watches tab |

## AmiForge's own settings

These are kept in `ENVARC:AmiForge/` (and copied to `ENV:AmiForge/` while the Amiga runs):

| File | What's in it |
|---|---|
| `Theme` | `Light` or `Dark` |
| `SplitView`, `Minimap` | `1` when on |
| `Snippets` | Your own snippets |
| `RecentProjects` | The Open Recent list |
| `ProjectsFolder` | The projects folder (when unset: `PROGDIR:Examples`) |
| `ShowBrowser` | `0` to not show the project browser at start-up |
| `Author` | Your name and e-mail address, for templates and releases |
| `GitName`, `GitEmail`, `GitHubUser`, `GitHubToken` | Git and GitHub |
| `DistccHosts` | The distcc hosts for every project |
| `Python` | The Python interpreter for projects that don't name one |
| `Boing` | The `boing` command for projects that don't name one |
| `BoingDocs` | A drawer with a newer `forms-events.md` |

## Variables AmiForge reads or sets

| Variable | What for |
|---|---|
| `PYTHONHOME`, `PYTHONPATH` | Read; set for the scripts AmiForge starts when they aren't set and the interpreter's drawer has the library |
| `AMIFORGE_STOPWATCH` | `1`: a Python script is also stopped in loops that never sleep |
| `GIT_SSL_NO_VERIFY` | Set for SimpleGit (see [Git and GitHub](11-git.md)) |

## Temporary files

| File | What it is |
|---|---|
| `T:AmiForge-pycheck.py`, `T:AmiForge-pyrun.py` | The syntax checker and the runner for Python projects |
| `T:AmiForge-mcp-run.txt` | A program's output, captured by `RUNPROGRAM CAPTURE` |
| `T:boing-<project>` | The interpreter made for a Boing BASIC project with extensions |
| `T:AmiForge-members.txt` | `boing members`' output, while AmiForge reads it |

## AmiForge's drawer

| File or drawer | What it is |
|---|---|
| `AmiForge` | The program |
| `Docs/` | This manual (`AmiForge-Manual.guide`, `AmiForge-Manual.txt` and the pictures in `Manual/`) |
| `Examples/` | The example projects, and the default projects folder |
| `Templates/` | The project templates |
| `Rexx/` | ARexx macros for the Macros menu |
| `Tools/QEMUCopyFix` | The fix for QEMU |
