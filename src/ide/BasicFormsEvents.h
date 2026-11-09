// --- basic --- A copy of Boing BASIC's docs/forms-events.md (github.com/geekychris/boing-basic),
// the contract between its forms runtime and the designer: AmiForge's built-in control table
// is read from it, and a newer forms-events.md found on the Amiga replaces it (BasicDesign.cpp).
// Refresh it with a copy of the file when the runtime's controls change.
#pragma once

static const char kFormsEventsMd[] = R"FE(# Forms: controls, properties, methods and events

This file is the contract between the forms runtime (`src/runtime/os4/forms.c`) and the IDE
(AmiForge's designer in a Boing BASIC project). For every object kind of the `.afg` format
(AmiForge `src/ide/GuiModel.h`) it lists what a BASIC program can use. The designer's
**Events** list offers exactly the events below, and creates the sub with exactly the
signature given.

Conventions:

- A control is reached by its `.afg` `id` (`ok`, `name`). Objects without an `id` can't be
  used from BASIC. Names are case-insensitive, as everything in BASIC.
- The form is reached by the window's `name` (`main`), or as `Me` in its own code-behind.
- Event subs are `Sub <id>_<Event>(<args>)` in the form's code-behind; for the form itself it
  is always `Form_<Event>`, whatever the window's name (as in VB6).
- `Boolean` properties are `True`/`False` (`-1`/`0`). Indexes (`ListIndex`, `List(i)`) count
  from 0, and `ListIndex = -1` means nothing is selected — VB6's rules.
- **Design-time key**: the `.afg` property that sets the initial value. A property without
  one can only be set at run time.
- R/O = read only at run time.
- Setting a property from code doesn't fire the control's `Change`/`Click` event, except
  where noted (VB6 fires `Change` when code sets `Text`; we do the same for `Text`).

**On the host** (macOS/Linux) there is no GUI. A form program fails with a message, unless
`BOING_HEADLESS=1` is set: then the forms run without a display (`src/runtime/posix/forms.c`).
Controls keep their properties, events fire as on AmigaOS (`Form_Load` while loading,
`Activate` and `Resize` on `Show`, `Click`/`Change` when code sets `ListIndex`/`Value`/`Text`,
`QueryUnload`/`Unload`), timers fire in real time, canvases draw as always, and the event loop
ends when the last form is unloaded or after `BOING_HEADLESS_SECS` seconds (default 10). A
program's own test mode can so run in `make test`, under sanitizers too (see
`examples/tincture/tests/host-test.sh`).

## Properties every control has

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Name` | String, R/O | `id` | |
| `Enabled` | Boolean | `disabled` (inverted) | `GA_Disabled` |
| `Visible` | Boolean | — | hides the control and relayouts the window |
| `ToolTipText` | String | `hint` | `GA_HintInfo` |
| `Tag` | String | — | free for the program |
| `ContextMenu` | String | `contextmenu` | the hidden top-level `menu` the right mouse button opens over the control |
| `Left`, `Top`, `Width`, `Height` | Long, R/O | — | pixels, inside the window; set by the layout |

Methods every gadget has: `SetFocus` (activates a string/integer/texteditor gadget; does
nothing for the others), `Refresh` (redraws).

Labels, spaces and fuel gauges (not gadgets) have `Name`, `Visible`, `Tag` and the geometry,
but no `Enabled`, `ToolTipText` or `SetFocus`.

## Form (`window`)

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Name` | String, R/O | `name` | |
| `Caption` | String | `title` | the window title |
| `Width`, `Height` | Long | `width`, `height` | inner size in pixels; setting it resizes the window |
| `Left`, `Top` | Long | — | window position on the screen |
| `Visible` | Boolean | — | `Show`/`Hide` |
| `Enabled` | Boolean | — | `False` = busy pointer and no input |
| `WindowState` | Integer | — | 0 normal, 1 iconified (needs `iconify=1`) |
| | | `remember` | the window box is saved in ENV:/ENVARC: and used next time |
| | | `iconify` | an iconify gadget |
| | | `toolwindow` | no size or depth gadget |
| | | `dropfiles` | an AppWindow (see Drag and drop) |
| `Tag` | String | — | |
| `ContextMenu` | String | `contextmenu` | the window's context menu (over no control that has one) |
| `ContextControl` | String, R/O | — | the control the last context menu was opened over |

| Method | Notes |
|---|---|
| `Show [vbModal]` | creates (fires `Form_Load` the first time) and opens the window; with `vbModal` (1) it returns when the form is hidden or unloaded, the other forms take no input meanwhile |
| `PopupMenu menu [, x, y]` | opens a hidden top-level menu at the pointer (or x, y in the window) |
| `AddMenu id, caption [, parent [, key]]` | a menu added at run time (no parent = a new title) |
| `RemoveMenu id` | |
| `Controls(name)` | a control by name |
| `Hide` | closes the window, the form and its controls stay loaded |
| `Refresh` | redraws |
| `SetFocus` | brings the window to the front and activates it |
| `Move left [, top [, width [, height]]]` | |

`Load main` loads a form without showing it; `Unload main` (or `Unload Me`) fires
`Form_Unload` and frees it. The program ends when the last loaded form is unloaded, or at `End`.
Closing the window with its close gadget is `Unload Me` (the program can refuse with `Cancel`).

| Event | Signature | When |
|---|---|---|
| `Load` | `Form_Load()` | the form is loaded, before the window opens |
| `Activate` | `Form_Activate()` | the window becomes the active one |
| `Deactivate` | `Form_Deactivate()` | another window becomes active |
| `Resize` | `Form_Resize()` | after the window opened and every time its size changes |
| `KeyDown` | `Form_KeyDown(KeyCode As Integer, Shift As Integer)` | a key is pressed while the window is active and no text gadget has the input |
| `KeyUp` | `Form_KeyUp(KeyCode As Integer, Shift As Integer)` | the key is released |
| `KeyPress` | `Form_KeyPress(KeyAscii As Integer)` | a key with a character (Latin-1) is typed |
| `QueryUnload` | `Form_QueryUnload(Cancel As Integer, UnloadMode As Integer)` | before `Unload`; UnloadMode 0 = close gadget, 1 = code |
| `Unload` | `Form_Unload(Cancel As Integer)` | the form is being unloaded; set `Cancel = True` to keep it |

`KeyCode` uses VB's `vbKey*` values for letters, digits, F1-F10, cursor keys, Return (13),
Esc (27), Tab (9), Backspace (8), Delete (46), Help (`vbKeyHelp` = 47). `Shift` is a bit set:
1 = Shift, 2 = Ctrl, 4 = Alt, 8 = Amiga (either).

## Button (`button`) — VB `CommandButton`

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Caption` | String | `text` | `_` marks the keyboard shortcut |
| `Text` | String | `text` | same as `Caption` |

| Event | Signature |
|---|---|
| `Click` | `<id>_Click()` |

## String (`string`) — VB `TextBox`

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Text` | String | `text` | setting it fires `Change` |
| `MaxLength` | Long | `maxchars` | |
| `Locked` | Boolean | — | read-only text |
| `SelStart`, `SelLength` | Long | — | cursor position / selection length |

| Event | Signature | When |
|---|---|---|
| `Change` | `<id>_Change()` | the text changed (every edit) |
| `KeyPress` | `<id>_KeyPress(KeyAscii As Integer)` | Return (13) and Tab (9) only: the user finished the entry |

## Integer (`integer`) — a number field with arrows

| Property | Type | Design-time key |
|---|---|---|
| `Value` | Long | `value` |
| `Min` | Long | `min` |
| `Max` | Long | `max` |
| `Text` | String | `value` (as text) |

| Event | Signature | When |
|---|---|---|
| `Change` | `<id>_Change()` | the number changed (typed + Return/Tab, or an arrow clicked) |

## Check box (`checkbox`) — VB `CheckBox`

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Caption` / `Text` | String | `text` | |
| `Value` | Integer | `checked` | 0 = `vbUnchecked`, 1 = `vbChecked` (VB6 values) |

| Event | Signature |
|---|---|
| `Click` | `<id>_Click()` — also when code changes `Value` (as VB6) |

## Chooser (`chooser`) — VB `ComboBox` with Style = 2 (drop-down list)

| Property / method | Type | Design-time key |
|---|---|---|
| `ListIndex` | Long | `selected` |
| `ListCount` | Long, R/O | number of `items` |
| `List(i)` | String (get and set) | `items` |
| `Text` | String, R/O | the selected item |
| `AddItem item [, index]` | method | |
| `RemoveItem index` | method | |
| `Clear` | method | |

| Event | Signature | When |
|---|---|---|
| `Click` | `<id>_Click()` | the user chose an item (also when code sets `ListIndex`, as VB6) |

## Radio buttons (`radio`) — one control for the whole group (VB's array of `OptionButton`s)

| Property | Type | Design-time key |
|---|---|---|
| `ListIndex` / `Value` | Long | `selected` — which button is on |
| `ListCount` | Long, R/O | number of `items` |
| `List(i)` | String, R/O | `items` |
| `Text` | String, R/O | the selected button's label |

| Event | Signature |
|---|---|
| `Click` | `<id>_Click()` |

## Slider (`slider`) — VB `Slider`

| Property | Type | Design-time key |
|---|---|---|
| `Value` | Long | `level` |
| `Min` | Long | `min` |
| `Max` | Long | `max` |

| Event | Signature | When |
|---|---|---|
| `Change` | `<id>_Change()` | the knob was released at a new value (or code set `Value`) |
| `Scroll` | `<id>_Scroll()` | while the knob is being dragged |

## Scroller (`scroller`) — VB `HScrollBar` / `VScrollBar`

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Value` | Long | `top` | |
| `Max` | Long | `total` | the range is 0 .. Max - LargeChange |
| `LargeChange` | Long | `visible` | the knob size |

| Event | Signature | When |
|---|---|---|
| `Change` | `<id>_Change()` | released at a new value |
| `Scroll` | `<id>_Scroll()` | while dragging |

## List (`listbrowser`) — VB `ListBox`

| Property / method | Type | Design-time key |
|---|---|---|
| `ListIndex` | Long | — (-1 = none) |
| `ListCount` | Long, R/O | |
| `List(i)` | String (get and set) | `items` |
| `Text` | String, R/O | the selected row |
| `TopIndex` | Long | first visible row |
| `AddItem item [, index]` | method | |
| `RemoveItem index` | method | |
| `Clear` | method | |

| `ColumnCount` | Long, R/O | `columns` (`"Name\|Size\|Date"`; rows are tab-separated: `AddItem "a" & vbTab & "1"`) |
| `ColumnTitle(i)` | String | `columns` |
| `Cell(row, col)` | String (get and set) | |
| `Selected(i)` | Boolean | `multiselect` |
| `SelCount` | Long, R/O | |
| `SortColumn`, `SortOrder` | Long | `sortable` (title clicks sort); 0 ascending, 1 descending |
| `ItemData(i)` | Long | — (moves with its row) |
| `Sort [col [, order]]` | method | numbers sort as numbers |
| | | `widths` (column weights) |

| Event | Signature | When |
|---|---|---|
| `Click` | `<id>_Click()` | a row was selected (by the user, or code set `ListIndex`) |
| `DblClick` | `<id>_DblClick()` | a row was double-clicked (`Click` comes first) |
| `ColumnClick` | `<id>_ColumnClick(Column As Integer)` | a column title was clicked (with `sortable=1` the list is sorted first) |

## Text label (`label`) — VB `Label`

| Property | Type | Design-time key |
|---|---|---|
| `Caption` / `Text` | String | `text` |

No events.

## Space (`space`)

No properties beyond the common ones, no events.

## Fuel gauge (`fuelgauge`) — VB `ProgressBar`

| Property | Type | Design-time key |
|---|---|---|
| `Value` | Long | `level` |
| `Min` | Long | `min` |
| `Max` | Long | `max` |

No events.

## File chooser (`getfile`) — a file name field with a button that opens the ASL requester

| Property / method | Type | Design-time key |
|---|---|---|
| `FileName` | String | — (full path, as typed or chosen) |
| `Text` | String | same as `FileName` |
| `DialogTitle` | String | `title` |
| `Pattern` | String | `pattern` (AmigaDOS pattern, `#?.bas`) |
| `SaveMode` | Boolean | `save` |
| `ShowOpen` | method | opens the requester from code; `FileName` is "" if cancelled |

| Event | Signature | When |
|---|---|---|
| `Change` | `<id>_Change()` | a file was chosen in the requester (not when cancelled) |

## Text editor (`texteditor`) — VB multi-line `TextBox`

| Property / method | Type | Design-time key |
|---|---|---|
| `Text` | String | `text` (lines separated by `vbLf`) |
| `Locked` | Boolean | — |
| `SelText` | String | the selected text; setting it replaces the selection |
| `LineCount` | Long, R/O | |
| `SelStart`, `SelLength` | Long | — (characters from the start) |
| `CurrentLine`, `CurrentColumn` | Long | — (0-based cursor) |
| `Modified` | Boolean | — |
| `FixedFont` | Boolean | `fixedfont` |
| `TabSize` | Long | — |
| `Bold`, `Italic`, `Underline` | Boolean | — (the selection / at the cursor) |
| `CanUndo`, `CanRedo` | Boolean, R/O | — |
| `Cut`, `Copy`, `Paste`, `Undo`, `Redo`, `Clear`, `SelectAll` | method | |
| `InsertText s` | method | at the cursor |
| `Find(text [, matchCase [, fromTop]])` | method | selects the match; True if found |
| `Replace(find, with [, all [, matchCase]])` | method | returns how many |
| `LoadFile path`, `SaveFile path` | method | |

| Event | Signature |
|---|---|
| `Change` | `<id>_Change()` (also while typing) |
| `CursorMove` | `<id>_CursorMove()` — the cursor moved |

## Timer (`timer`) — VB `Timer` — **new .afg kind**

Not a gadget: it takes no space in the window and the runtime skips it while building the
layout. It can sit anywhere in the layout tree (the designer should show it in the tree,
with no preview).

```
timer id=tick interval=1000 enabled=1
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Interval` | Long | `interval` (default 0) | milliseconds; 0 = never fires |
| `Enabled` | Boolean | `enabled` (default 1) | |

| Event | Signature | When |
|---|---|---|
| `Timer` | `<id>_Timer()` | every `Interval` ms while `Enabled` and the form is loaded |

Timers run on timer.device. Events never overlap: a `Timer` event that is still running when
the next one is due skips that tick.

## Canvas (`canvas`) — a drawing surface (VB `PictureBox`, for graphics and games) — **new .afg kind**

```
canvas id=view width=400 height=300 contextmenu=mnuCanvas mousemove=0
```

The layout gives it at least `width` x `height` pixels. Programs draw into an off-screen
picture and show it with `Refresh`. The full reference is [graphics.md](graphics.md).

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Width`, `Height` | Long, R/O | `width`, `height` | the picture size |
| `ForeColor`, `FillColor`, `BackColor` | Long | — | VB colours (`RGB()`) |
| `CurrentX`, `CurrentY` | Long | — | |
| `Frames` | Long, R/O | — | |

Methods: `Cls`, `PSet`, `Line` (`B`/`BF`), `Box`, `Rect`, `Circle`, `Polygon`, `PSetMany`,
`Point`, `DrawText`, `Print`, `TextWidth`, `TextHeight`, `Refresh` (`Flip`, `EndDraw`),
`BeginDraw`, `KeyDown`, `CreateImage`, `LoadImage`, `SetTarget`, `DrawImage`,
`DrawImagePart`, `ImageWidth`, `ImageHeight`, `FreeImage`, `SaveImage`, `Hash`, `Resize`
(see graphics.md for their arguments).

Keys arrive as the form's `KeyDown`/`KeyUp`, and `view.KeyDown(k)` gives a key's state at any
time. Animate it from a `Timer`. Mouse events are in canvas pixels (the picture is centred in
the control and scaled by `scale`; while dragging, X and Y may be outside the picture):

| Event | Signature | When |
|---|---|---|
| `MouseDown` | `<id>_MouseDown(Button As Integer, Shift As Integer, X As Long, Y As Long)` | a button went down over the canvas (Button: 1 left, 2 right, 4 middle) |
| `MouseUp` | `<id>_MouseUp(Button As Integer, Shift As Integer, X As Long, Y As Long)` | it was released (also outside the canvas, after a press on it) |
| `MouseMove` | `<id>_MouseMove(Button As Integer, Shift As Integer, X As Long, Y As Long)` | the pointer moved while a button is down (Button = the buttons held); with `mousemove=1` every move over the canvas |
| `DblClick` | `<id>_DblClick()` | a double click (after the second `MouseDown`) |
| `MouseWheel` | `<id>_MouseWheel(Delta As Integer)` | the wheel turned over the canvas (positive = away from the user) |

The right button opens the menus (or the canvas's `contextmenu=`) when the form has any, as on
every Amiga window, so `Button = 2` only arrives in forms without menus.

The form gets the same events (`Form_MouseDown(Button, Shift, X, Y)` ...) for clicks outside
any canvas and gadget, X and Y inside the window's border.

## Menu (`menu`) — VB `Menu` — **new .afg kind**

The menu bar: `menu` trees at the top level of the file, after the root layout. A top-level
`menu` is a menu title, its children the items, theirs sub-items. A top-level menu with
`hidden=1` is a context / pop-up menu (see `contextmenu` and `PopupMenu`). Details and an
example: [ui.md](ui.md#1-menus).

```
menu id=mnuFile text="Project"
  menu id=mnuOpen text="Open..." key=O
  menu text=-
  menu id=mnuQuit text="Quit" key=Q
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Caption` | String | `text` | `-` = a separator bar |
| `Shortcut` | String | `key` | one character: Amiga + key; a longer text is only shown |
| `Checked` | Boolean | `checked` | |
| `Checkable` | Boolean | `checkable` | a toggle item with a checkmark |
| `Enabled` | Boolean | `disabled` (inverted) | |
| `Visible` | Boolean | `hidden` (inverted) | |
| | | `radio` | mutually exclusive with the other `radio` items of its panel |

| Event | Signature | When |
|---|---|---|
| `Click` | `<id>_Click()` | the item was chosen (mouse, shortcut, context or pop-up menu); `Checked` is already the new state |

The form's methods and properties for menus: `Me.PopupMenu menu [, x, y]`,
`Me.AddMenu id, caption [, parent [, key]]`, `Me.RemoveMenu id`, `Me.Controls(name)`;
`ContextMenu` (String, design-time key `contextmenu`) on every control and the form names the
hidden menu the right mouse button opens over it; `Me.ContextControl` (String, R/O) is the
control the last context menu was opened over.

## Toolbar (`toolbar`) — VB `Toolbar` — **new .afg kind**

A row of buttons (speedbar.gadget) holding `tool` objects; it reports no events itself (each
tool has its own `Click`). [ui.md](ui.md#2-toolbars-and-status-bars)

```
toolbar id=tb
  tool id=tbOpen text="Open" image="TBImages:open" hint="Open a file" menu=mnuOpen
  tool separator=1
  tool id=tbGrid text="Grid" toggle=1 checked=1
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `ButtonCount` | Long, R/O | — | tools, separators included |
| | | `textonly` | only the texts |
| | | `imagesonly` | only the pictures |
| | | `vertical` | a vertical bar |

## Tool (`tool`) — VB `Button` of a Toolbar — **new .afg kind**

Only inside a `toolbar`.

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Caption` / `Text` | String | `text` | |
| `Picture` | String | `image` | a picture file (datatypes); `selimage` = the pressed picture |
| `ToolTipText` | String | `hint` | |
| `Enabled` | Boolean | `disabled` (inverted) | follows the `menu=` item's `Enabled` |
| `Value` | Integer | `checked` | toggle tools (`toggle=1`): 0 / 1 |
| | | `separator` | a separator instead of a button |
| | | `menu` | the menu item it stands for: its `Click` also calls `<menu>_Click` |

| Event | Signature |
|---|---|
| `Click` | `<id>_Click()` |

## Status bar (`statusbar`) — VB `StatusBar` — **new .afg kind**

```
statusbar id=sb panels="Ready|Ln 1|100%" widths="0|60|50"
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Text` / `SimpleText` | String | first of `panels` | panel 0 |
| `Panel(i)` | String | `panels` | panel i |
| `PanelCount` | Long, R/O | | |
| | | `widths` | pixel widths, 0 = takes the rest |

No events.

## Image (`image`) — VB `Image` — **new .afg kind**

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Picture` | String | `file` | a picture file (datatypes) |
| | | `frame` | a recessed frame around it |

No events.

## Tabs (`tabs`) — VB `TabStrip` — **new .afg kind**

A clicktab whose children (layouts) are the pages; each page's `title` is its tab.

```
tabs id=pages
  vlayout title="General"
    ...
  vlayout title="Advanced"
    ...
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Tab` | Long | `selected` | the page shown, 0-based (also `Value`) |
| `TabCount` | Long, R/O | number of pages | |
| `TabCaption(i)` | String | the pages' `title` | |

| Event | Signature | When |
|---|---|---|
| `Click` | `<id>_Click()` | another tab was chosen (also when code sets `Tab`) |

## Tree view (`treeview`) — VB `TreeView` — **new .afg kind**

A hierarchical list; nodes have a key (unique) and a parent key.

| Property / method | Type | Notes |
|---|---|---|
| `AddNode key, parent, text` | method | parent `""` = a root node; error 457 for a key in use |
| `RemoveNode key` | method | with its children |
| `Clear` | method | |
| `Expand key`, `Collapse key` | method | |
| `NodeText(key)` | method / `NodeText key, text` | |
| `NodeParent(key)` | method | the parent's key, "" for a root |
| `NodeExpanded(key)` | method | |
| `NodeCount` | Long, R/O | |
| `SelectedKey` | String | setting it opens the node's parents and selects it |
| `EventKey` | String, R/O | the node of the last event |

| Event | Signature | When |
|---|---|---|
| `NodeClick` | `<id>_NodeClick()` | a node was selected (`SelectedKey`) |
| `DblClick` | `<id>_DblClick()` | |
| `Expand` | `<id>_Expand()` | the user opened a node (`EventKey`) |
| `Collapse` | `<id>_Collapse()` | the user closed a node |

## Grid (`grid`) — VB `MSFlexGrid` — **new .afg kind**

An editable cell grid drawn by the runtime, with scrollers. [ui.md](ui.md#grid-grid--an-editable-cell-grid-vb-msflexgrid-for-spreadsheets)

```
grid id=sheet rows=100 cols=27 fixedrows=1 fixedcols=1 colwidth=64 editable=1 headers=1
```

| Property / method | Type | Design-time key | Notes |
|---|---|---|---|
| `Rows`, `Cols` | Long | `rows`, `cols` | |
| `FixedRows`, `FixedCols` | Long | `fixedrows`, `fixedcols` | title rows / columns |
| | | `headers` | titles A, B, C... and 1, 2, 3... in them |
| `Row`, `Col` | Long | | the current cell |
| `Text` | String | | the current cell; setting it sets every selected cell |
| `TextMatrix(row, col)` | String | | any cell (get and set) |
| `ColWidth(col)` | Long | `colwidth` | pixels; `ColWidth = n` sets every column |
| `RowHeight` | Long | | |
| `ColAlignment(col)` | Long | | 0 left, 1 right, 2 centre (default: numbers right) |
| `CellBold(row, col)`, `CellForeColor(row, col)`, `CellBackColor(row, col)` | | | also without indexes for the current cell |
| `TopRow`, `LeftCol` | Long | | scrolling |
| `SelStartRow`, `SelStartCol`, `SelEndRow`, `SelEndCol` | Long | | the selected range |
| `Editable` | Boolean | `editable` | typing starts editing a cell |
| `Editing` | Boolean, R/O | | |
| `Redraw` | Boolean | | False while filling many cells |
| `AddItem text [, row]` | method | | a row; cells separated by `vbTab` |
| `RemoveItem row` | method | | |
| `Clear` | method | | the cells (not the titles) |
| `EditCell` | method | | start editing the current cell |

| Event | Signature | When |
|---|---|---|
| `EnterCell` | `<id>_EnterCell()` | the current cell changed (after) |
| `LeaveCell` | `<id>_LeaveCell()` | it is about to change |
| `BeforeEdit` | `<id>_BeforeEdit(Row As Long, Col As Long, Cancel As Integer)` | editing of that cell is about to start (VSFlexGrid's signature) |
| `AfterEdit` | `<id>_AfterEdit(Row As Long, Col As Long)` | an edit of that cell was committed (Return, Tab, cursor keys, a click elsewhere) or Del cleared cells |
| `Click` | `<id>_Click()` | |
| `DblClick` | `<id>_DblClick()` | (starts editing when `Editable`) |
| `KeyDown` | `<id>_KeyDown(KeyCode As Integer, Shift As Integer)` | a key while the grid has the focus; set KeyCode = 0 to swallow it |
| `Scroll` | `<id>_Scroll()` | |

Keys: cursor keys (with Shift: extend the selection), Tab, Return, Page Up/Down, Home/End,
F2 (edit), Del (clear the selection), Esc (cancel an edit).

## Layouts: splitter, picture buttons

- `hlayout splitter=1` / `vlayout splitter=1`: draggable bars between the children.
- `button image="file" [selimage="file"] [text="..."]`: a button with a picture (and its text
  beside it).

## Common dialog (`commondialog`) — VB `CommonDialog` — **new .afg kind**

Not a gadget: takes no space (like `timer`). Also `CreateObject("Boing.CommonDialog")`.
[ui.md](ui.md#3-standard-dialogs-commondialog-vb-commondialog)

```
commondialog id=dlg title="Open a picture" filter="Pictures|#?.(png|iff|jpg)"
```

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `DialogTitle` | String | `title` | |
| `Filter` | String | `filter` | AmigaDOS pattern, or VB's `"Text\|*.txt;*.doc"` |
| `InitDir` | String | `initdir` | |
| `Flags` | Long | `flags` | `cdlOFNAllowMultiselect` &H200, `cdlOFNOverwritePrompt` &H2, `cdlOFNFileMustExist` &H1000 |
| `FileName` | String | — | full path |
| `FileTitle` | String, R/O | — | |
| `FileCount` | Long, R/O | — | |
| `Files(i)` | String, R/O | — | multi-select |
| `CancelError` | Boolean | — | cancelling raises error 32755 |
| `Cancelled` | Boolean, R/O | — | |
| `FontName`, `FontSize`, `FontBold`, `FontItalic`, `FontUnderline` | | — | ShowFont |
| `Color` | Long | — | ShowColor |
| `ScreenModeID`, `ScreenWidth`, `ScreenHeight`, `ScreenDepth` | Long | — | ShowScreenMode |
| `ShowOpen` | method | | |
| `ShowSave` | method | | |
| `ShowFont` | method | | |
| `ShowColor` | method | | |
| `ShowScreenMode` | method | | |
| `ShowMessage(prompt, buttons [, title [, icon]])` | method | | returns the button number, 1 = leftmost |

No events.

## Requesters

- `MsgBox(prompt [, buttons [, title]])` — an Intuition requester (requester.class) with the
  VB buttons (`vbOKOnly`, `vbOKCancel`, `vbAbortRetryIgnore`, `vbYesNoCancel`, `vbYesNo`,
  `vbRetryCancel`) and icons (`vbCritical`, `vbQuestion`, `vbExclamation`, `vbInformation`);
  returns `vbOK`, `vbCancel`, `vbYes`, ... Without a form it works in a console program too.
- `InputBox(prompt [, title [, default]])` — a requester with a string gadget; returns "" when
  cancelled.
- The title defaults to the program name (`App.Title`).
- `App.Request(prompt, "_Save|_Discard|_Cancel" [, title [, icon]])` — your own buttons;
  returns 1 for the leftmost ... n for the last (Esc). See [ui.md](ui.md) for the rest of the
  App object (ToolType, ARexxPort, SaveIcon, ShowHelp...).

## Drag and drop from Workbench

A form with `dropfiles=1` (or `Me.OLEDropMode = 1`) is an AppWindow. Files dropped on it fire
`Form_OLEDragDrop(Count As Integer, X As Integer, Y As Integer)`, or over a control with
`dropfiles=1` `<id>_OLEDragDrop(Count As Integer, X As Integer, Y As Integer)` (X, Y inside the
control); the files are `App.DropFile(0)` .. `App.DropFile(Count - 1)`.

| Event | Signature | When |
|---|---|---|
| `OLEDragDrop` | `<id>_OLEDragDrop(Count As Integer, X As Integer, Y As Integer)` / `Form_OLEDragDrop(...)` | icons dropped from Workbench |

## Event order

`Form_Load` → window opens → `Form_Activate` → `Form_Resize` → (events) → close gadget →
`Form_QueryUnload` → `Form_Unload` → window closes. An event sub that raises an unhandled
runtime error ends the program with the usual `file:line: runtime error` message.
)FE";
