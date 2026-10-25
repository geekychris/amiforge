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
programs in `DH1:AmiForge` and their sources in `DH1:AmiForge/src`.
