# AmiForge user manual

AmiForge is an integrated development environment for AmigaOS 4.1: an editor, a builder and a
source-level debugger in one window on the Amiga, for C, C++, PowerPC assembler, Python and
Boing BASIC. This manual describes everything it does, and its exercises build complete
projects step by step.

## Contents

**Getting started**

1. [Introduction and installing](01-introduction.md): what AmiForge does, what it needs, installing it
2. [A guided tour](02-tour.md): build, run and debug an example in ten minutes

**The editor**

3. [The window and editing](03-editing.md): the panels, editing, the other languages, split view, themes, multiple cursors, snippets, the hex and icon editors
4. [Completion and navigation](04-navigation.md): completion, call tips, the AutoDocs, definitions, the context menu, the outline, Find All References, Rename

**Projects and building**

5. [Projects and templates](05-projects.md): projects, the project browser, settings, the templates, Package Release
6. [Building and running](06-building.md): C, C++ and assembler, Python, Boing BASIC and its extensions, distcc, the explanations
7. [The GUI designer and the Menu Editor](07-designer.md): ReAction windows with generated C code, Boing BASIC forms and their menus

**Debugging**

8. [Debugging](08-debugging.md): breakpoints, stepping, the variables, crashes, assembler and Boing BASIC
9. [Edit and Continue](09-edit-and-continue.md): changing a program while it's stopped
10. [The debugger tools](10-debugger-tools.md): the Windows inspector, the leak tracker, the profiler

**Tools and integration**

11. [Git and GitHub](11-git.md): change markers, the Git panel, blame, commits, cloning and pushing
12. [The Markdown preview](12-markdown.md)
13. [ARexx and MCP control](13-arexx-mcp.md): the ARexx port, macros, commands for scripts, AI assistants
14. [Running under QEMU](14-qemu.md)
15. [Customising and settings](15-settings.md)
16. [Troubleshooting](16-troubleshooting.md)

**[Exercises](exercises/README.md)**: tutorials that build complete projects, with their
finished versions in AmiForge's `Examples/Manual` drawer

**Appendices**

- [A. The keys](appendix-a-keys.md)
- [B. ARexx and MCP commands](appendix-b-commands.md)
- [C. The menus](appendix-c-menus.md)
- [D. Files and settings](appendix-d-files.md)

## Other forms of this manual

- On the Amiga: **Help > User Manual** in AmiForge, or `Docs/AmiForge-Manual.guide` in
  AmiForge's drawer (AmigaGuide), and `Docs/AmiForge-Manual.txt`.
- As a PDF book: `make manual-pdf` in AmiForge's source makes `build/AmiForge-Manual.pdf`.
- This Markdown version is the source of the others; it reads best on GitHub.

## Conventions

- `Code like this` is something you type: C, a Shell command, a file or drawer name.
- **Menu > Item** is a menu item; **F7** and **Amiga-B** are keys. Amiga is the right Amiga
  key.
- The manual describes AmiForge 0.3.1.
