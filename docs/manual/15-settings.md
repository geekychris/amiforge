# Customising and settings

AmiForge keeps most of its settings for you: the project's in the project, your own in
`ENVARC:AmiForge/`. This chapter lists what you can change and where it's kept;
[Files and settings](appendix-d-files.md) lists every file.

## Settings of the project

**Project > Settings...** holds the compilers, flags, libraries, program arguments, console,
run command and debugging options of the project, and for Python and Boing BASIC projects
their interpreter or `boing` command; see [Projects and templates](05-projects.md#project-settings).
They're saved in the `.afp`, so each project has its own.

**Build > Distributed Builds (distcc)...** saves the distcc hosts in the project, or for every
project. **Build > Package Release...** saves the release details in the project.

## Your own settings

| What | How to change it | Kept in |
|---|---|---|
| The colour theme | **View > Theme** | `ENVARC:AmiForge/Theme` |
| The split view and the minimap | **View > Split Editor**, **View > Minimap**, or the toolbar's **Split** and **Map** | `ENVARC:AmiForge/SplitView`, `Minimap` |
| Your snippets | **Edit > Edit My Snippets** | `ENVARC:AmiForge/Snippets` |
| The projects folder | **Project > Set Projects Folder...** | `ENVARC:AmiForge/ProjectsFolder` |
| The project browser at start-up | its **Show at startup** box | `ENVARC:AmiForge/ShowBrowser` |
| The last projects | opening projects; **Project > Open Recent** | `ENVARC:AmiForge/RecentProjects` |
| Your name for templates and releases | the template chooser and Package Release | `ENVARC:AmiForge/Author` |
| Git and GitHub | **Git > GitHub Setup...** | `ENVARC:AmiForge/GitName`, `GitEmail`, `GitHubUser`, `GitHubToken` |
| The distcc hosts for every project | **Build > Distributed Builds (distcc)...** | `ENVARC:AmiForge/DistccHosts` |
| The Python interpreter for every project | `SetEnv SAVE AmiForge/Python DH1:python-os4` | `ENVARC:AmiForge/Python` |
| The `boing` command for every project | `SetEnv SAVE AmiForge/Boing Work:BoingBASIC/boing` | `ENVARC:AmiForge/Boing` |
| Where Boing BASIC's `forms-events.md` is | `SetEnv SAVE AmiForge/BoingDocs Work:BoingBASIC/docs` | `ENVARC:AmiForge/BoingDocs` |

The text size (**View > Bigger Text**, **Smaller Text**, **Normal Text Size**) is for the
session.

## Macros and templates

- Put your own ARexx macros in AmiForge's `Rexx` drawer, or in a project's `Rexx/` drawer for
  that project only; they appear in the Macros menu. See [ARexx and MCP
  control](13-arexx-mcp.md#macros).
- Put your own project templates in `AmiForge/Templates`. See [Making your own
  templates](05-projects.md#making-your-own-templates).

## Starting AmiForge

From a Shell:

```amigados
AmiForge [REXXPORT=name] [project.afp | project.bbp | file ...]
```

- A project (`.afp` or `.bbp`) opens as the project; other files open in the editor; a `.afg`
  opens in the GUI designer.
- `REXXPORT=name` gives the ARexx port a name of your own (see [ARexx and MCP
  control](13-arexx-mcp.md)).
- With nothing to open, AmiForge reopens the last project, or shows the project browser.

The same works from Workbench: double-click a project's icon, or select files and
double-click AmiForge's icon with Shift held.
