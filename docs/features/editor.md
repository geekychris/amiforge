# Editor features

![Dark theme, split view and minimap](../images/editor-dark-split-minimap.png)

## Themes

**View > Theme** chooses **Light** (the original colours) or **Dark**. The theme colours the
whole editor:
- the syntax highlighting of every language (C, C++, assembler, makefiles, diffs);
- the margins, fold markers and breakpoint/execution markers;
- the selection, caret and caret line;
- calltips and the completion list;
- the AutoDocs viewer.

AmiForge remembers the choice in `ENVARC:AmiForge/Theme`, which holds `Light` or `Dark`.

![The dark theme](../images/editor-dark-theme.png)

## Split view

**View > Split Editor** shows a second view of the current file beside the first. Each view
scrolls on its own, but they show the same text, so an edit in one appears in the other at once.
Click in a view to work in it: Find, Go to Definition, F1, completion and the other commands act
on the view you clicked last. The scrollers belong to the left view. Choose the menu item again
to close the second view. AmiForge remembers whether the split is on (`ENVARC:AmiForge/SplitView`).

## Minimap

**View > Minimap** shows the whole file in tiny print on the editor's right, with the part that's
in the (left) view highlighted.
- Click the minimap, or drag in it, to jump there.
- The mouse wheel over the minimap scrolls the editor.

AmiForge remembers whether it's on (`ENVARC:AmiForge/Minimap`).

## Multiple cursors

| To... | Do this |
|---|---|
| Add a cursor | **Alt-click** where you want it (Ctrl-click is Go to Definition) |
| Select the word at the cursor, then each next occurrence | **Edit > Add Next Occurrence** (**Amiga-T**), or right-click > Add Next Occurrence |
| Select every occurrence at once | **Edit > Select All Occurrences** |
| Go back to one cursor | **Esc** |

Typing, Delete/Backspace and Paste act at every cursor.

![Multiple cursors](../images/editor-multiple-cursors.png)

## Snippets

Type a snippet's name and press **Tab** to insert it. Snippets also appear in the completion
list, marked with `»` (for example `for»`); pick one there with Tab or Return. **Edit > Insert
Snippet...** (or the right-click menu) lists them all.

After a snippet goes in, its fields are outlined:
- Type over the selected field.
- **Tab** moves to the next field and **Shift-Tab** to the previous one.
- When you leave a field, the same name elsewhere in the snippet follows. For example, in
  `openlib` the library name goes into `FooBase`, `IFoo` and `struct FooIFace`.
- After the last field the cursor goes to the snippet's end point.
- **Esc** finishes early.

![A snippet's fields](../images/editor-snippet-fields.png)

| Snippet | Inserts |
|---|---|
| `main` | `main()` with the usual includes |
| `openlib` | `OpenLibrary` + `GetInterface` with an error check, and the matching `DropInterface`/`CloseLibrary` |
| `window` | `OpenWindowTags` and an IDCMP loop (close gadget, Esc) |
| `timer` | a timer.device request: port, `TimeRequest`, `OpenDevice`, `TR_ADDREQUEST`, clean-up |
| `rexx` | sending a command to an ARexx port and reading the result |
| `tags` | a `struct TagItem` array |
| `for`, `while`, `if`, `switch` | the statements |
| `struct`, `class` | a struct; a C++ class with constructor and destructor |

### Your own snippets

**Edit > Edit My Snippets** opens `ENVARC:AmiForge/Snippets`, creating it with an example if it
isn't there. Saving the file loads the snippets straight away. The format is snipMate's:

```
# a comment
snippet hello  print a line
	IDOS->Printf("${1:Hello, world}\n");${0}

snippet loop  count down
	for (int ${1:i} = ${2:10}; ${1} > 0; ${1}--) {
		${0}
	}
```

- `snippet <name>  <description>` starts a snippet.
- Each body line starts with a tab, or up to four spaces.
- In the body:
  - `${1:text}` is field 1 with default text, and `${2}` or `$2` is an empty field.
  - A field number used again copies that field.
  - `${0}` is where the cursor ends.
  - `\$`, `\}` and `\\` give a literal `$`, `}` and `\`.
- A tab at the start of a body line (after the one that marks the line) is one indent step.
- Every line gets the indentation of the line where you typed the snippet.
- A snippet with a built-in snippet's name replaces it.

## Symbol outline

The **Symbols** tab beside **Project** (or **View > Symbols**) lists what the current file
defines, in source order:
- C and C++: functions (with `Class::method` names), structs, classes, unions, enums,
  namespaces, typedefs, `#define`s and global variables;
- assembler: labels (functions marked with `.type ..., @function`), `.macro`, `.equ`/`.set`
  and `#define`.

Click an entry to go there. The list updates about a second after you stop typing.

![The symbol outline](../images/editor-symbols.png)

## Find All References

Put the cursor on a name and press **Shift-F12** (or **Edit > Find All References**, or
right-click). Every use of the name as a whole word, outside comments and strings, is listed in
the **Find** tab, across the project's files and any other open files. Click a line to go there.
The uses in open files are highlighted; **Esc** clears the highlights.

![Find All References](../images/editor-find-references.png)

## Rename Symbol

Put the cursor on a name and press **F2** (or **Edit > Rename Symbol...**, or right-click), then
type the new name.
1. A requester shows how many changes there will be in which files, and warns if the new name is
   already in use.
2. **Rename** changes every whole-word use outside comments and strings:
   - open files are changed in the editor, one Undo step per file, so you can check them before
     saving;
   - files that aren't open are written straight away.

The new name must be a valid C identifier, and not a keyword.

![Rename Symbol](../images/editor-rename.png)

## Keys

| Keys | Action |
|---|---|
| Shift-F12 | Find All References |
| F2 | Rename Symbol |
| Amiga-T | Add Next Occurrence |
| Alt-click | Add a cursor |
| Esc | One cursor again; end a snippet; clear reference highlights |
| name + Tab | Insert a snippet; Tab / Shift-Tab move between its fields |

## Limitations

- Find All References and Rename Symbol work on the text, not on what the compiler knows.
  Different things with the same name (a local `i` in two functions, a member and a global
  called `count`) are all included, so check the list or the requester before renaming.
- The outline reads declarations written in the usual way; heavy macro use can hide a function.
- The minimap follows the left view only.
- Alt-drag doesn't make a rectangular selection (Alt-click adds cursors instead).
