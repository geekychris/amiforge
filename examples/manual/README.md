# The manual's exercises, finished

The finished projects of the [AmiForge manual's exercises](../../docs/manual/exercises/README.md).
Open a project's `.afp` in AmiForge to build, run or debug it.

| Drawer | Exercise |
|---|---|
| `ex01-wordcount/` | 1. A first program from a template |
| `ex02-converter/` | 2. A window made with the GUI designer |
| `ex03-inventory/` | 3. Finding a crash (`inventory-crash.c` has the bug, `inventory.c` is fixed) |
| `ex04-compute/` | 4. Edit and Continue (the program after the change) |
| `ex05-dice/` | 5. Boing BASIC with an extension in C (`expected.out` is its output) |
| `ex06-git/` | 6. A round trip to GitHub (the `.gitignore` and README) |
| `ex07-arexx/` | 7. Driving AmiForge from ARexx (`TodoCount.rexx`, `BuildReport.rexx`) |

`tools/manual-check.py` (make manual-test) compiles every C project here with the SDK's GCC and
checks the Boing BASIC one with `boing check`.
