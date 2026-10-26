# Building AmiForge from source

AmiForge is cross-compiled on macOS or Linux with the adtools GCC 11 toolchain for AmigaOS 4,
in Docker.

## Requirements

- Docker (Docker Desktop, Rancher Desktop or plain Docker on Linux).
- The `walkero/amigagccondocker:os4-gcc11` image (`docker pull walkero/amigagccondocker:os4-gcc11`).
- For the host unit tests: a C/C++ compiler (clang or gcc).
- For packaging: Python 3 with Pillow (the icons), and `lha` to inspect archives.

## Build

```
tools/build.sh                      # everything: build/AmiForge, test programs, debuggee targets
tools/build.sh build/AmiForge       # just the IDE
tools/build.sh DEBUG=1              # with stabs and no optimisation
make -C tests/host                  # host unit tests of the debugger core (ELF, stabs, eval, PPC)
tools/make-icons.py                 # Workbench icons into dist/icons/
tools/make-dist.sh                  # dist/AmiForge.lha
```

Build notes:
- C++17 with newlib (`-mcrt=newlib`) and `-mcpu=440fp`.
- No `-D__USE_INLINE__`: OS functions are called through their interfaces
  (`IExec->AllocVecTags`), because the inline macros define names like `Insert`, `Remove` and
  `Text` that clash with C++ code.
- C++ programs link with `-athread=native`.

## Layout

| Directory | Contents |
|---|---|
| `src/ide/` | The application: ReAction GUI, documents, projects, builds, debugger UI, AutoDocs, completion, navigation, Git |
| `src/sci/` | The Scintilla platform layer for AmigaOS 4 (`SciView`), and the PowerPC assembler lexer |
| `src/dbg/` | The debugger core (`dbg.h`): ELF/stabs reader, types and expressions, PPC decoder, OS4 runtime |
| `third_party/` | Scintilla 5.5.7 and Lexilla 5.4.5, unmodified |
| `examples/` | The example projects |
| `tests/` | Host unit tests, OS4 test programs (`dbgtest`, `scitest`, `memtest`) and debuggee programs |
| `tools/` | Build, packaging, SDK install and test scripts; `tools/os4/QEMUCopyFix.c` |
| `docs/` | This guide, the user guide, the design notes and the debugger notes |

[DESIGN.md](DESIGN.md) describes the architecture, and [debugger-notes.md](debugger-notes.md)
records what was learned about the OS4 debug interface (including several kernel and DOS traps
the debugger works around).

## Testing on an OS4 machine

The `tools/m1*` scripts drive an AmigaOS 4.1 guest in QEMU over SSH. They use QEMU's monitor
and an [amiga_mcp](https://github.com/geekychris/amiga_mcp) devbench with its bridge running in
the guest. Set `M1=user@host` for a remote Mac or Linux box, or `M1=local` for this machine.

| Script | What it does |
|---|---|
| `m1deploy.sh FILE...` | Writes files into the dev disk with QEMU stopped, then restarts it (fast and reliable for big binaries) |
| `m1push.sh FILE AMIGA_PATH` | Copies a file in through the bridge while the guest runs |
| `m1dos.sh 'command'` | Runs an AmigaDOS command and prints its output |
| `m1shot.sh out.png` | Screenshot |
| `m1click.sh X Y`, `m1keys.sh keys...`, `m1type.py 'text'` | Mouse and keyboard |

Run the debugger's tests on the guest with
`dbgtest DH1:AmiForge all memtrace DH1:AmiForge/dbgtest.log`. It needs the `tests/targets`
programs in `DH1:AmiForge` and their sources in `DH1:AmiForge/src`. Its `oscall` section
(`tests/targets/pausetest.c`) pauses a program that prints to a console window and one that
sits in `Wait()`, and checks that every stop is in the program's code and that `LockIBase`
still returns. `oskill` stops (kills) the same programs inside those calls and checks that
nothing is left behind.

## Testing the features

`make -C tests/host ide` builds one test runner per feature area from `tests/host/ide/*.mk`
(the shared harness is `tests/host/ide/aftest.h`). The areas also have tests that run on AmigaOS 4:

### Edit and Continue

- `make -C tests/host ide` (area `hotpatch`): the linker against objects and programs built
  by the cross-compiler: data kept at the program's addresses, long-branch stubs, refusals
  (new static, resized variable, function not in the program, wrong file), debug-info merge,
  successive patches, C++ vtables, plain `-gstabs` function ranges.
- `hotpatchtest DIR` on AmigaOS 4 (with `hptarget`, `hpcpp` and `hotpatch/*/compute.o`,
  `hotpatch/cpp2/shape.o` in DIR): runs the programs under the debugger and patches them
  twice, checking output, kept data, moved breakpoints, stepping into a new function and a
  refused change. `setenv AFHOTPATCH_FAR 1` makes it take the long-branch and stub paths
  even when the patch is within branch range.

### Debugger tools

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
  toolstest DH1:AF-debugtools all RAM:toolstest.log [trace]
  ```

  The sections are `leaks`, `profile`, `inspect` and `baseline` (CPU-bound `hot` under the
  debugger with no tools: it must still end).

`tests/templates/check.sh` cross-compiles every project template (and a sample GUI with each gadget kind) with `-Werror`.
`tests/examples/check.sh` does the same for every example in `examples/`, with the flags from its `.afp`.
Python templates and examples (`language=python`) are checked for syntax with the host's `python3` instead.

`make -C tests/host ide` also runs the `lang` area: language detection, outlines per language,
the ARexx/AmigaDOS/Python lexers on a test document, the key table (no key bound twice), and
the Python checker and runner scripts, run with the host's `python3`.
