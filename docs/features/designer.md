# GUI designer, project templates and release packaging

## The GUI designer

**Tools > GUI Designer** edits a window built from ReAction classes, and writes the C code
that makes it. The design is kept in a `.afg` file in the project.

![The GUI designer with its preview](../images/designer-window.png)

Opening the designer:
- If the project has one `.afg` file, the designer opens it.
- Otherwise a file requester asks which one to open. Type a new name to start a new design.
  A new design starts with a vertical group holding one button.

The designer window has three columns:

| Column | What it does |
|---|---|
| **Palette** | The objects you can add: vertical and horizontal groups, Button, String, Integer, Check Box, Chooser, Radio Buttons, Slider, Scroller, List, Text Label, Space, Fuel Gauge, File Chooser and Text Editor. Select one and click **Add**, or double-click it. |
| **Layout** | The window and its objects as a tree. A new object goes into the selected group, or after the selected object. **Up** and **Down** move it among its neighbours. **In** moves it into the group above it, **Out** moves it out of its group, and **Remove** deletes it. |
| **Properties** | The selected object's properties. Click one, type the new value in the field underneath and press Return. Yes/no and choice properties (such as Frame) change each time you click them again. |

Properties every object has:
- **ID name**: the C name in the generated code. `ok` becomes `MAIN_OK`, and the object is
  `gui->obj[MAIN_OK]`.
- **Label (left)**: a label shown to the left of the object. Put `_` before a letter to make it the
  keyboard shortcut.
- **Weight**: its share of the spare space. 0 means its minimum size.
- **Minimum width** and **Minimum height**.

Gadgets also have **Disabled** and **Help hint**: the hint is a bubble shown when the mouse
rests on the gadget.

Groups have:
- **Frame**: none, group, button or field. A frame title makes a group frame.
- **Frame title**.
- **Even sizes**: all children the same size.

Each kind also has its own properties: a button's text, a chooser's items (separated by `|`),
a slider's range, and so on.

The **window** is the first row of the tree. Its properties are:
- **Code name**: the generated functions are called `<name>_gui_...`.
- **Title**.
- **Size**: 0 means as small as the contents allow.
- **Resizable**.

The **preview** window is built from the real ReAction classes and is rebuilt after every
change. Click a gadget in the preview to select it in the tree. **Preview** brings the window
back if you closed it.

### Generate Code

**Generate Code** saves the `.afg` file and writes `<name>_gui.h` and `<name>_gui.c` next to it.
It also adds them, and the `.afg`, to the project.

The header declares:
- an `enum` of IDs;
- `struct <name>_gui`, holding the window object, the open window, `obj[]` with every object, and
  a `quit` flag;
- the functions: `_create`, `_open`, `_close`, `_dispose`, `_signal` (the signal to wait for) and
  `_handle`. `_handle` processes the window's input, and returns FALSE when the program should
  end.

A program's main loop is then:

```c
static struct main_gui gui;
if (main_gui_create(&gui) && main_gui_open(&gui)) {
    uint32 sig = main_gui_signal(&gui);
    while (!(IExec->Wait(sig | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C))
        if (!main_gui_handle(&gui)) break;
}
main_gui_dispose(&gui);
```

**Your code goes in `<name>_gui.c`**, between the `/* USER-BEGIN ... */` and `/* USER-END ... */`
lines:
- There is one section for each gadget, inside a `switch` on the gadget's ID. Each starts with a
  comment saying what `code` holds for that kind of gadget.
- There's one for includes and globals at the top.
- There's one for the close gadget. By default it sets `gui->quit`.

Generating the code again keeps everything between the markers, and replaces the rest. A
section whose gadget you deleted is kept at the end of the file, in an `#if 0` block, so nothing
you wrote is lost. If you add the gadget back, its code goes back in its place.

The generated code uses the interfaces explicitly (`IIntuition->NewObject(...)`), with no
`__USE_INLINE__`. It builds with the project's default flags and `-lauto`.

### The .afg file

The `.afg` file is plain text, one object per line, indented two spaces per level of nesting:

```
# AmiForge GUI (edit with Tools > GUI Designer)
window name=main title="My Program" width=320
vlayout id=root
  hlayout id=opts title=Options
    checkbox id=fast text=_Fast checked=1
    chooser id=mode items="Low|Medium|High" label=_Mode
  button id=ok text=_OK weight=0
```

You can edit it by hand. Values with spaces go in quotes, and inside quotes `\"`, `\\` and `\n`
work as in C.

## New Project from Template

**Project > New Project from Template** lists the templates in `AmiForge/Templates`:

1. Choose a template.
2. Enter the project's name (letters, digits, `-` and `_`) and the drawer to make it in.
3. Enter the author: it goes into the comments and the version string, and is remembered.

AmiForge makes a drawer with the project's name, writes the files and the `.afp`, and opens it.
Every template builds with **F7**. **Ctrl-F5** runs it.

![Choosing a template](../images/designer-templates.png)

| Template | What it makes |
|---|---|
| Console C program | A Shell program that reads its arguments with `ReadArgs()` |
| Console C++ program | A Shell program with a class, `std::vector`, `std::string` and algorithms |
| ReAction application | A window designed in a `.afg` file, with its code generated and the buttons already doing something |
| Intuition window + GadTools menu | A classic Intuition window with a GadTools menu, drawing with graphics.library |
| Commodity | A commodity with a hot key, controlled from Exchange; shows a window when the key is pressed |
| Screen blanker | A commodity that blanks the screen with a starfield after a time without input, or on a hot key. `TEST` blanks it for three seconds |
| AHI sound player | Synthesises notes and plays them through `ahi.device` |
| Shared library | A library described in IDL XML, with its headers and function table made by the SDK's `idltool`, and a test program |
| Device driver skeleton | An exec device (Open, Close, BeginIO, AbortIO) that keeps a small buffer, and a test program |

### The library and device templates

The library and device projects build `<name>.library` or `<name>.device`, linked with
`-nostartfiles`.

Their **Run** command (Project > Settings > Run command) is `Execute run-test`. That script:
1. flushes the old copy from memory;
2. compiles `test_<name>.c`;
3. runs it against the library or device just built.

The library's functions are described in `<name>.xml`. To add one:
1. Describe it in the XML.
2. Run `SDK:C/idltool -i -p -n -c <name>.xml` to remake `include/` and `<name>_vectors.c/.h`.
3. Write the function in `<name>.c`.

### Making your own templates

A template is a drawer in `AmiForge/Templates` with a `template.def`:

```ini
[template]
name=My template
description=What it makes, in one line
order=90                          ; where it comes in the list
[project]                         ; .afp settings for the new project
output=${LOWER}
libs=-lauto
[files]
file=main.c -> ${LOWER}.c         ; copied, and added to the project
extra=ReadMe                      ; copied, but not part of the build
generated=include/x.h             ; made by a command below; added to the project
open=${LOWER}.c                   ; opened afterwards
[create]
command=SDK:C/idltool -i x.xml    ; run in the new drawer after copying
gui=${LOWER}.afg                  ; generate GUI code from this file
```

Substitutions work in file contents and in the destination names:

| Substitution | Value |
|---|---|
| `${NAME}` | the name as typed |
| `${IDENT}` | the name as a C identifier |
| `${UPPER}` | `${IDENT}` in upper case |
| `${LOWER}` | `${IDENT}` in lower case |
| `${AUTHOR}` | the author |
| `${DATE}` | the date, as d.m.yyyy |
| `${YEAR}` | the year |

- Add `:upper`, `:lower` or `:ident` to any of them, as in `${NAME:upper}`.
- `$${` gives a literal `${`.
- For a `gui=` file, a file in the template with the same name as the generated `.c` can supply
  user sections. That's how the ReAction template's buttons come with working code.

## Package Release

**Build > Package Release** makes an archive ready to upload to Aminet.

1. **The release details.** A dialog asks for them; they're saved in the project:
   - version;
   - author, as `Name <email>` (Aminet needs the email);
   - a short description (Aminet allows 40 characters);
   - the Aminet category;
   - a longer description;
   - the release compiler flags. By default these are the debug flags with `-g`/`-gstabs`
     removed, and `-O0` becomes `-O2`.

   Problems that would get the upload rejected are listed before anything is built.
2. **The release build.** The project is built from scratch with the release flags, into
   `obj-release/`, so the debug build in `obj/` is untouched.
3. **The archive.** In the project's `release/` drawer, AmiForge makes:
   - `stage/<Name>/`, holding the stripped program, its icon and `<Name>.readme`;
   - a drawer icon;
   - an `Install` script with an icon. It copies the drawer into a drawer chosen in a requester,
     or into the drawer given on the command line (`Install Work:Tools`).

   It packs these into `release/<Name><version>.lha`, and writes the Aminet readme beside it as
   `release/<Name><version>.readme`.

The Build tab shows each step, ending with a listing of the archive.

AmigaOS 4 has no LhA that can create archives, but it has `xad2lha`, which converts any archive
XAD can read into LhA. So AmiForge packs the drawer with the SDK's `tar`, then converts it.
