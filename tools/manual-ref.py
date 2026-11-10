#!/usr/bin/env python3
"""manual-ref.py [--check] - the manual's reference appendices, generated from AmiForge's source
so they can't drift from it:

    docs/manual/appendix-a-keys.md      the key table (src/ide/KeyBindings.cpp) and the menus'
                                        Amiga-key shortcuts (src/ide/MainWindow.cpp)
    docs/manual/appendix-b-commands.md  the ARexx commands (src/ide/RexxCmd.cpp), with the ones
                                        the MCP tools use (src/ide/McpPort.cpp) marked
    docs/manual/appendix-c-menus.md     the menus (src/ide/MainWindow.cpp)

Each file has a hand-written introduction above the line "<!-- generated below -->"; only what
follows it is generated. --check: exit 1 if a file isn't up to date (make manual-test).

As a module it also gives the tables to tools/manual-check.py: menu_items(), key_table(),
rexx_commands(), mcp_commands().
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "src", "ide")
MAN = os.path.join(ROOT, "docs", "manual")
MARK = "<!-- generated below by tools/manual-ref.py: don't edit what follows -->"

RAW = {"RAW_F%d" % i: "F%d" % i for i in range(1, 13)}
RAW.update({"RAW_HELP": "Help", "RAW_BREAK": "Break"})
MODS = {"SHIFT": "Shift", "CTRL": "Ctrl", "ALT": "Alt"}


def read(name):
    return open(os.path.join(SRC, name), encoding="utf-8").read()


def key_table():
    """[(keys, description)] from KeyBindings.cpp's table, in its order."""
    out = []
    body = read("KeyBindings.cpp")
    body = body[body.index("static const std::vector<Binding> table"):]
    body = body[:body.index("};")]
    for m in re.finditer(r'\{\s*(RAW_\w+),\s*([A-Z| 0]+?),\s*CMD_\w+,\s*"([^"]*)"\s*\}', body):
        mods = [MODS[x.strip()] for x in m.group(2).split("|") if x.strip() in MODS]
        out.append(("-".join(mods + [RAW[m.group(1)]]), m.group(3)))
    return out


def menu_items():
    """[(title, item, sub, key)] of the menus[] table (sub is "" for an item, key "" for none).
    Submenus filled in at run time (Open Recent, Theme) and the Macros menu (before Help) are
    noted by menu_notes()."""
    body = read("MainWindow.cpp")
    body = body[body.index("static struct NewMenu menus[]"):]
    body = body[:body.index("NM_END")]
    out, title, item = [], "", ""
    for line in body.split("\n"):
        line = re.sub(r"//.*", "", line)
        m = re.search(r'TITLE\("([^"]*)"\)', line)
        if m:
            title = m.group(1)
            continue
        m = re.search(r'(ITEM|SUB)\("([^"]*)",\s*(?:"([^"]*)"|0),', line)
        if not m:   # a NewMenu written out: { NM_ITEM, (STRPTR)"Track Leaks", 0 or "K", ... }
            m = re.search(r'\{\s*NM_(ITEM|SUB),\s*\(STRPTR\)"([^"]*)",\s*(?:\(STRPTR\))?(?:"([^"]*)"|0),', line)
        if m:
            text, key = m.group(2), m.group(3) or ""
            if m.group(1) == "ITEM":
                item = text
                out.append((title, text, "", key))
            else:
                out.append((title, item, text, key))
    return out


def rexx_commands():
    """[(name, template, description, section)] from RexxCmd.cpp's table."""
    out, section = [], ""
    body = read("RexxCmd.cpp")
    for line in body.split("\n"):
        m = re.match(r"\s*// --- (end )?(\w+)", line)
        if m:
            section = "" if m.group(1) else m.group(2)
            continue
        m = re.match(r'\s*\{\s*"([A-Z0-9]+)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\},', line)
        if m:
            unesc = lambda s: s.replace('\\"', '"').replace("\\\\", "\\")
            out.append((m.group(1), unesc(m.group(2)), unesc(m.group(3)), section))
    return out


def mcp_commands():
    body = read("McpPort.cpp")
    body = body[body.index("kCommands[] = {"):]
    body = body[:body.index("};")]
    body = re.sub(r"//.*", "", body)
    return re.findall(r'"([A-Z0-9]+)"', body)


def md_cell(s):
    return s.replace("|", "\\|")


def keys_md():
    L = ["| Keys | What they do |", "|---|---|"]
    for k, d in key_table():
        L.append("| %s | %s |" % (k, md_cell(d[0].upper() + d[1:])))
    L += ["", "## Menu shortcuts", "",
          "The right Amiga key with a letter chooses a menu item. Some letters do more than one",
          "thing: Intuition takes the first item with that letter.", "",
          "| Keys | Menu item |", "|---|---|"]
    for title, item, sub, key in menu_items():
        if key:
            L.append("| Amiga-%s | %s > %s |" % (md_cell(key), title, md_cell(sub or item)))
    return "\n".join(L) + "\n"


def menus_md():
    L = []
    titles = []
    for t, _, _, _ in menu_items():
        if t not in titles:
            titles.append(t)
    for t in titles:
        L += ["## %s" % t, "", "| Item | Keys |", "|---|---|"]
        for title, item, sub, key in menu_items():
            if title != t:
                continue
            name = ("&nbsp;&nbsp;&nbsp;&nbsp;" + sub) if sub else item
            L.append("| %s | %s |" % (md_cell(name), "Amiga-" + md_cell(key) if key else ""))
        L.append("")
    L += ["Filled in while AmiForge runs: **Project > Open Recent** (the last projects), **View >",
          "Theme** (the colour themes), and the **Macros** menu before Help (the ARexx macros in",
          "the macros drawer; see [ARexx and MCP control](13-arexx-mcp.md))."]
    return "\n".join(L) + "\n"


def commands_md():
    mcp = set(mcp_commands())
    titles = {"": "Classic commands", "mcp": "Commands for scripts and assistants",
              "markdown": "The Markdown preview", "basic": "Boing BASIC projects and forms"}
    L, last = [], None
    for name, tpl, desc, sec in rexx_commands():
        if sec != last:
            if last is not None:
                L.append("")
            L += ["## %s" % titles.get(sec, sec.capitalize()), "", "| Command | Template | What it does |",
                  "|---|---|---|"]
            last = sec
        mark = " (MCP)" if name in mcp and sec == "" else ""
        L.append("| `%s`%s | %s | %s |" % (name, mark, "`%s`" % md_cell(tpl) if tpl else "",
                                            md_cell(desc)))
    return "\n".join(L) + "\n"


FILES = [("appendix-a-keys.md", keys_md), ("appendix-b-commands.md", commands_md),
         ("appendix-c-menus.md", menus_md)]


def main():
    check = "--check" in sys.argv
    fails = 0
    for name, fn in FILES:
        path = os.path.join(MAN, name)
        text = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
        if MARK not in text:
            print("FAIL manual-ref: %s has no line %s" % (name, MARK))
            fails += 1
            continue
        new = text[:text.index(MARK) + len(MARK)] + "\n\n" + fn()
        if new != text:
            if check:
                print("FAIL manual-ref: docs/manual/%s is out of date (run tools/manual-ref.py)" % name)
                fails += 1
            else:
                open(path, "w", encoding="utf-8").write(new)
                print("manual-ref: wrote docs/manual/%s" % name)
    # every MCP command is in the ARexx table (its documentation)
    names = {n for n, _, _, _ in rexx_commands()}
    for c in mcp_commands():
        if c not in names:
            print("FAIL manual-ref: the MCP command %s isn't in RexxCmd.cpp's table" % c)
            fails += 1
    if check:
        print("manual-ref: %d keys, %d menu items, %d ARexx commands%s" % (
            len(key_table()), len(menu_items()), len(names), "" if not fails else ", %d failures" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
