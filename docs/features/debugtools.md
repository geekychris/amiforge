# Debugger tools: Windows inspector, leak tracker, profiler

Three tools build on AmiForge's debugger. Each has a tab in the bottom panel: **Windows**,
**Leaks** and **Profile**.

## Windows: the live Intuition inspector

**Debug > Inspect Windows** shows the program's screens, windows and gadgets as a tree. The
**Windows** tab refreshes itself every time the program stops. While it runs, press **Refresh**.

![The Windows tab](../images/debugtools-windows.png)

- **Task**: the program's state (running, waiting...) and its signals, by name:
  - **Waiting for**: what it is blocked on;
  - **Received**: what has arrived;
  - **Allocated**: the signals it owns.

  A window's IDCMP port appears as `IDCMP 'title'`, so you can see straight away that a program
  is waiting for input on its window, or for CTRL-C.
- **Screens**: those the program's windows are on, and any opened since debugging started
  (likely the program's own). Each shows its size, depth and public name.
- **Windows**: every window whose IDCMP port signals the program's task. Each shows:
  - position and size;
  - IDCMP flags and window flags, decoded (`CLOSEWINDOW|GADGETUP`, `SMART_REFRESH|DRAGBAR|...`);
  - the port's signal bit.
- **Gadgets**: each one with its ID, type (`BOOLGADGET`, `SYS:CLOSE`, or the BOOPSI class, such as
  `layout.gadget`), position, size, flags and activation flags. A gadget's text or label is shown
  too.

**Click a window or gadget** (or select it and press **Highlight**) to bring the window to the
front and flash a frame around it on the screen. That's useful when a program has several
windows, or a gadget is hard to find. The frame is drawn twice per flash, so it leaves nothing
behind.

Notes:
- ReAction windows show their root `layout.gadget`. Gadgets nested inside layouts are not listed
  (layout.gadget has no public way to list its children).
- Windows opened by other tasks the program starts are not shown: only windows whose port
  signals the program's own task.

## Leaks: what the program never released

Turn on **Debug > Track Leaks**, or the **Track leaks when debugging** box in Project > Settings.
The project remembers the setting. Then debug as usual (F5). When the program ends, the **Leaks**
tab lists everything it acquired and didn't release, grouped by the line that acquired it.

![The Leaks tab](../images/debugtools-leaks.png)

| Tracked | Released by |
|---|---|
| `AllocVec`, `AllocVecTagList`, `AllocVecTags` | `FreeVec` |
| `AllocMem` | `FreeMem` (the size is checked) |
| `AllocSysObject`, `AllocSysObjectTags` | `FreeSysObject` |
| `CreatePool` | `DeletePool` |
| `OpenLibrary` | `CloseLibrary` (opens are counted) |
| `GetInterface` | `DropInterface` |
| `Open`, `FOpen`, `OpenFromLock` | `Close` |
| `Lock`, `DupLock`, `ParentDir`, `CreateDir`, `DupLockFromFH`, `ParentOfFH` | `UnLock` (`OpenFromLock` takes the lock over) |
| `malloc`, `calloc`, `realloc` (and so C++ `new`) | `free` |

- Each group shows how many items and how many bytes, and the source line and function that
  acquired them. **Click it** to go to that line.
- Under each group are the items themselves: address and size, plus for files and locks the
  file's name, for libraries the name and version, and for system objects the `ASOT_` type.
- **Problems** are listed at the top:
  - memory freed twice;
  - memory freed with the wrong function (`FreeVec()` of memory from `malloc()`);
  - `FreeMem()` with a different size from the `AllocMem()`.
- Only the program's own calls count. Allocations an OS function makes for itself while the
  program calls it are not recorded (for example, the memory `Open()` uses internally). When a
  library function allocates on the program's behalf, the leak is shown at the program's line
  that called it.
- The summary line counts how many calls were tracked. If more than 8192 arrive between two
  polls (10 ms), some are lost, and the summary says so.

How it works: while the program runs, exec's and dos's functions are patched with `SetMethod`, as
`QEMUCopyFix` does. Each patch checks first whether the caller is the program's task; every other
task goes straight through. The patch records the call and its return addresses: the LR plus
the stack's back chain. `malloc` and `free` go through the program's own newlib interface, which
is patched when the program reaches `main()`. All patches are removed as soon as the program
ends, and when AmiForge stops debugging or quits.

Limitations:
- `malloc` calls made before `main()` (C++ static constructors) are not tracked.
- Resources acquired by other tasks the program starts are not tracked.
- The leaked resources are still leaked afterwards: the tracker reports them, it doesn't free them.

## Profile: where the time goes

**Debug > Profile** builds the program if needed and runs it under the profiler. It doesn't stop
at `main()` or at breakpoints. A sampler process at priority 10 reads the program's registers
every millisecond. When the program ends (or you press Stop), the **Profile** tab lists its
functions, and the editor marks its hot lines.

![The Profile tab and line heat](../images/debugtools-profile.png)

| Column | Meaning |
|---|---|
| **Self** | Samples with the program counter inside the function. |
| **In OS** | Samples in the OS (a library, the kernel, or waiting) when this function was the nearest program function on the call chain: it called the OS and the OS was busy, or the task was waiting. `w` is the waiting share. |
| **Total** | Samples with the function anywhere on the call chain (itself or its callees). |
| **Hottest line** | Click the row to go there. |

- Rows starting **[OS]** are the OS functions themselves (`newlib.library: memcpy`,
  `kernel: ...`), named through the system's debug symbols.
- Lines with samples get an end-of-line note in the editor:
  - `<< 23.4% (117)` is time spent on that line;
  - `<< 30.1% in OS (151)` is time spent in calls made from that line.
- **Clear** (or Debug > Clear Profile) removes the notes.
- The header shows the number of samples and how much of the time the program was waiting.
- Under QEMU, timer resolution and emulation speed make 1 ms sampling coarse. Run the program
  long enough to collect a few hundred samples.

## Testing

- **Host unit tests:** `make -C tests/host ide` runs `tests/host/ide/debugtools_*_test.cpp`. They
  cover:
  - leak matching and grouping, double and mismatched frees, `FreeMem` size checks;
  - profile attribution (self, in OS, total, hottest lines, annotations);
  - the inspector's flag decoders and panel rows.
- **On OS4:** `tests/os4/toolstest` runs the real thing against three programs in `tests/targets`:
  - `leaky` leaks one resource of each kind;
  - `hot` has a known hot loop and a 0.4 s wait;
  - `windows` opens a window with two gadgets.

  Copy `build/tests/toolstest` and `build/targets/{leaky,hot,windows}` to a drawer, and the target
  sources to its `src/` drawer, then run:

  ```
  toolstest DH1:AF-debugtools all RAM:toolstest.log
  ```
