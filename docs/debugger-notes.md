# Debugger notes: the exec debug interface on the QEMU sam460ex guest

Measured on AmigaOS 4.1 FE in QEMU sam460ex (m1.local), October 2026. The runtime that
relies on these results is `src/dbg/os4_session.c`.

The experiments live in `tests/os4/dbgprobe.c`, a probe with numbered stages and variant bits;
see the comments in it. The probe logs to `DH1:AmiForge/probe.log` and closes the file after
every line, so the log survives a hang.

**Read this before experimenting.** One process stuck in DOS's exit path makes every later
process exit hang too, and eventually the bridge stops answering. After any failure, reboot
the guest before you trust the next result: only the first failure after a clean boot means
anything. Many early conclusions here turned out wrong for exactly this reason.

## Results

| Question | Answer |
|---|---|
| Are code pages writable? | No. Code pages report `0x2C0` (`EXECUTE \| SUPER_RO_USER_RO`). |
| Does the IMMU dance work? | Only if it keeps the other bits. `SetMemoryAttrs(a, 4, MEMATTRF_READ_WRITE)`, as adtools GDB does it, drops `MEMATTRF_EXECUTE` for the whole page and froze the guest. What works: under `SuperState()`, set `(old & ~MEMATTRF_RW_MASK) \| MEMATTRF_READ_WRITE`, store, put the old attributes back, `UserState()`, then `CacheClearE(a, 4, CACRF_ClearI \| CACRF_ClearD)`. |
| Does the trap reach the hook? | Yes. `tw 31,0,0` (0x7FE00008) arrives as `DBHMT_EXCEPTION` with `Traptype` 0x700 and `ip` at the trap. The hook's context has `Flags` = 3 (`ECF_FULL_GPRS \| ECF_FPU`), so it is a complete register set. The pointer is a fixed kernel buffer (0x02168230 every time), so copy it. |
| What state is the task in after the hook returns 1? | `TS_SUSPENDED`. `RestartTask` resumes it at the trap address (put the original instruction back first). `WriteTaskContext` changes are honoured: `ip := main+4` resumed there. |
| How do we get the exit code? | `NP_NotifyOnDeathMessage`: `dm_ReturnCode` is the program's return value. `DBHMT_REMTASK` arrives too. |
| How do we kill? | There is no DOS `Exit()`. The entry point (`_start`) is trapped once and its context kept. To kill, write it back with `ip = lr` (the return into RunCommand) and `r3 = 20`, remove all traps, and `RestartTask`. The program returns, RunCommand returns 20, and the launcher exits normally. |
| Does the MSR trace bit work? | Not tried: stepping is done in software. |
| Can kernel code be read for disassembly? | `GetMemoryAttrs` reports kernel pages as not user-readable, so `dbg_read_memory` refuses them. `IDebug->DisassembleNative` and direct reads of them do work. |

## The traps (each of these hung a program or the whole guest)

1. **Don't start the program as an `NP_Seglist` process.** If one parent creates a newlib
   program with `CreateNewProc(NP_Seglist, NP_Cli, ...)`, the second such process hangs in
   DOS's process cleanup when it ends. It spins at kernel +0x127F0 (an `sc` in a loop) called
   from dos.library. This happens with no debugging at all: no hook, no traps, a fresh
   LoadSeg, and either kind of death notification. Trivial `NP_Entry` children are fine.
   **Fix:** the debuggee is a small launcher process (`NP_Entry`, `NP_Cli`, with the session
   in `NP_UserData`) that runs the program with `IDOS->RunCommand()`, as a shell does.
   RunCommand also returns the exit code directly.
2. **Don't wake on the DeathMessage.** The message is replied "at the last possible moment"
   of the process's life, from DOS's cleanup, which seems to run at a lowered priority. If
   the IDE is woken by it, it starts the next run straight away, and that run's exit
   livelocks in the same DOS spin. `dbg_signal_mask()` therefore contains only the poll
   timer: the exit is noticed on the next tick (10 ms). As a margin, `dbg_run` also waits
   50 ms after a previous exit.
3. **Don't restart within milliseconds of a hook suspension.** `tc_State` reads
   `TS_SUSPENDED` before the kernel is done with the task. A `RestartTask` in that window
   is lost: the task shows `TS_READY` but never runs again (it stays at the trap address).
   After seeing the suspension, the runtime sleeps `SETTLE_MS` (10 ms) before reading or
   restarting the task. The standalone probe never saw this because it always logged to
   disk between the hit and the restart.
4. **Don't `Signal()` the IDE from the hook.** The debuggee then hung at its next DOS call.
   This is most likely trap 3 in another form (the IDE woke instantly). Polling a counter
   that the hook increments is what's verified. The hook only copies the context, bumps
   the counter and returns 1.
5. **Use a clean hook per run.** The runtime clears the `struct Hook` (including
   `h_MinNode`) before every `AddDebugHook`.
6. **Watch out for the bridge `push_file` bug.** A file whose last chunk is tiny (for example
   182284 bytes = 178 KB + 12) wedges the bridge. `tools/m1push.sh` pads ELF files to a 1 KB
   multiple.
7. **Use `run >NIL: prog >log args`.** `run >log prog` only redirects run's "[CLI n]" banner.
   dbgtest writes its own line-by-line log anyway
   (`dbgtest DIR all - DH1:AmiForge/dbgtest.log`).

## Stabs as produced by adtools GCC 11 (-gstabs+)

- There is one `.stab` section with a single header entry (`N_UNDF`), and the values are final
  link addresses. The executable keeps `.rela.stab`, but nothing needs applying.
- `N_FUN` holds absolute addresses. An empty `N_FUN` ends a function, and its value is the
  size. `N_SLINE` is function-relative inside a function and absolute outside one.
  Assembler files (`.S` through gcc) produce `N_SO` for the temporary `.s` file, `N_SOL`
  for the real one, and absolute `N_SLINE` without `N_FUN`, so `.symtab` supplies their bounds.
- Locals come before the `N_LBRAC` of their block. `N_LBRAC`/`N_RBRAC` are function-relative.
  Function statics (`V`) are repeated after the end marker.
- There are no `N_BINCL`/`N_EXCL` entries. Types are numbered `(0,n)` per unit, and an xref
  (`(0,18)=xsnode:`) is often redefined in place by a later `node:T(0,18)=s28...`.
- C++ `N_FUN` names are mangled. Classes are `Name:Tt(0,n)=s<size>` with fields
  `name:/<vis><type>,<bitpos>,<bits>;`, static members `name:/2<type>:<physname>;`, methods
  `name::<type>:<physname>;<vis><qual><kind>;` and bases `!<n>,<virt><vis><offset>,<type>;`.
- At -O0 the frame base for `N_PSYM`/`N_LSYM` offsets is r1 after the prologue (r31 = r1). A
  function's first line entry is at its start and the second one starts the body, which is
  where breakpoints go.
