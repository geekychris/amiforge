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

## Properties every control has

| Property | Type | Design-time key | Notes |
|---|---|---|---|
| `Name` | String, R/O | `id` | |
| `Enabled` | Boolean | `disabled` (inverted) | `GA_Disabled` |
| `Visible` | Boolean | — | hides the control and relayouts the window |
| `ToolTipText` | String | `hint` | `GA_HintInfo` |
| `Tag` | String | — | free for the program |
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
| `WindowState` | Integer | — | 0 normal, 1 minimised (iconified) |
| `Tag` | String | — | |

| Method | Notes |
|---|---|
| `Show` | creates (fires `Form_Load` the first time) and opens the window |
| `Hide` | closes the window, the form and its controls stay loaded |
| `Refresh` | redraws |
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

| Event | Signature | When |
|---|---|---|
| `Click` | `<id>_Click()` | a row was selected (by the user, or code set `ListIndex`) |
| `DblClick` | `<id>_DblClick()` | a row was double-clicked (`Click` comes first) |

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

| Event | Signature |
|---|---|
| `Change` | `<id>_Change()` |

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

## Requesters

- `MsgBox(prompt [, buttons [, title]])` — an Intuition requester (requester.class) with the
  VB buttons (`vbOKOnly`, `vbOKCancel`, `vbAbortRetryIgnore`, `vbYesNoCancel`, `vbYesNo`,
  `vbRetryCancel`) and icons (`vbCritical`, `vbQuestion`, `vbExclamation`, `vbInformation`);
  returns `vbOK`, `vbCancel`, `vbYes`, ... Without a form it works in a console program too.
- `InputBox(prompt [, title [, default]])` — a requester with a string gadget; returns "" when
  cancelled.
- The title defaults to the program name (`App.Title`).

## Event order

`Form_Load` → window opens → `Form_Activate` → `Form_Resize` → (events) → close gadget →
`Form_QueryUnload` → `Form_Unload` → window closes. An event sub that raises an unhandled
runtime error ends the program with the usual `file:line: runtime error` message.
)FE";
