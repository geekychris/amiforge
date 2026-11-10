# Running under QEMU

AmiForge works on QEMU's `sam460ex` machine with AmigaOS 4.1 Final Edition, which is how many
people run AmigaOS 4 today. One fix is needed, builds are slower than on real hardware, and a
few things about QEMU's networking and keyboard are worth knowing.

## QEMUCopyFix

QEMU doesn't emulate the 460EX's DMA engine, which exec uses for big memory copies, so copies of
256 KB or more silently lose data. GCC's C++ compiler then reads corrupted headers and fails
with errors that make no sense.

`QEMUCopyFix` (in AmiForge's `Tools` drawer) makes `CopyMem()` and `CopyMemQuick()` copy with
the CPU. Copy it to `C:` and start it at boot, in `S:User-Startup`:

```amigados
Run >NIL: C:QEMUCopyFix
```

Real hardware doesn't need it.

## Speed

- Compiling is slow under emulation: a small C file takes a few seconds, a C++ file that
  includes `<iostream>` about two minutes. [Distributed builds](06-building.md#distributed-builds-distcc)
  hand the compiling to the computer that runs QEMU.
- Under the debugger, each breakpoint hit or step costs 10 to 30 milliseconds.
- Python takes about 20 seconds to start.
- The profiler's samples come less often (every 15 to 25 ms while the CPU is busy), but its
  times still add up.

## Networking

QEMU's user networking makes the computer that runs QEMU reachable from the Amiga at the
guest's gateway address: `10.0.2.2` by default, or for example `192.168.101.2` when QEMU is
started with `net=192.168.101.0/24`. That's the address to use for a distcc server on the host.
For Git and GitHub the Amiga needs a default gateway (check with `ShowNetStatus`).

## The keyboard

F11, F12 and Break are on PC keyboards, and QEMU passes them on, so the debugger's keys work as
described. Pause/Break on a PC keyboard is the Amiga's Break, which pauses the program being
debugged.

## Sharing files with the host

`deploy`-style tools write into the guest's hard disk image (an `.hdf` file) from the host,
for example with amitools' `xdftool`. Do that only while QEMU isn't running: AmigaOS and the
host would each have their own idea of the disk, and the disk would be damaged. Copying over
the network (an FTP or SMB share) or through an MCP bridge is safe while the Amiga runs.
