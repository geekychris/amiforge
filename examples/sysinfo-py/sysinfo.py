"""sysinfo: what the Amiga is doing, from Python.

Uses the AmigaOS 4 Python port's built-in _amiga module (exec and dos calls) to list
memory, the busiest tasks, the libraries in use and a volume's free space. On another
system it says what it would show.

Try: Build (F7) for a syntax check, Run (Ctrl-F5), and F9 on a line then F5 for pdb.
"""
import sys

try:
    import _amiga
except ImportError:
    _amiga = None


def kib(n):
    return "%d KB" % (n // 1024)


def show_memory():
    mem = _amiga.avail_mem_summary()
    print("Memory")
    for kind in ("any", "largest"):
        if kind in mem:
            print("  %-8s %10s" % (kind, kib(mem[kind])))


def show_tasks(limit=8):
    tasks = sorted(_amiga.list_tasks(), key=lambda t: -t[1])
    print("Tasks (%d, highest priority first)" % len(tasks))
    for name, pri, state in tasks[:limit]:
        print("  %4d  %-6s %s" % (pri, state, name))


def show_libraries(limit=8):
    libs = sorted(_amiga.list_libraries(), key=lambda l: -l[3])
    print("Libraries in use (%d)" % len(libs))
    for name, version, revision, opens in libs[:limit]:
        print("  %-28s %3d.%-4d opened %d times" % (name, version, revision, opens))


def show_volume(volume):
    info = _amiga.volume_info(volume)
    if not info:
        print("No volume %s" % volume)
        return
    total = info["num_blocks"] * info["bytes_per_block"]
    used = info["num_blocks_used"] * info["bytes_per_block"]
    print("%s %s used of %s (%d%%)" % (volume, kib(used), kib(total), 100 * used // max(total, 1)))


def main(argv):
    volume = argv[1] if len(argv) > 1 else "SYS:"
    if _amiga is None:
        print("This needs the AmigaOS 4 Python port (its _amiga module).")
        return 0
    print(" ".join(_amiga.version()))
    show_memory()
    show_tasks()
    show_libraries()
    show_volume(volume)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
