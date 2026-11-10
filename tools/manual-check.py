#!/usr/bin/env python3
"""manual-check.py [--verbose] [--no-docker] - check the user manual against AmiForge
(make manual-test).

(The block parser and the directives are Boing BASIC's tools/manual-check.py, by bb-docs; the
checks are AmiForge's.)

1. Code samples compile:
   - every ```c and ```cpp block that is a whole program is compiled with the SDK's GCC (the
     walkero Docker image that tools/build.sh uses; one container for all of them), and so is
     every C project under examples/manual/ (its .afp's files, plus any other .c in the drawer);
   - every ```basic block is checked with `boing check`, and every Boing BASIC project under
     examples/manual/ too; one with an expected.out is linked with `boing vm --project` and run,
     and its output compared (BOING=path of a host boing; skipped with a note if there is none).
   The line before a block can say:
     <!-- fragment -->                 not a whole program: skipped
     <!-- project: examples/manual/x -->  an excerpt of that project, which is checked instead
2. ARexx: every command a ```rexx block sends ('NAME ...' or 'MCP NAME ...'), and every
   `NAME` or `NAME args` code span in the text whose first word is in capitals, is a command of
   AmiForge's ARexx port (src/ide/RexxCmd.cpp), or one of the known words that aren't commands.
3. Keys: every "**Menu > Item** (**Amiga-K**)" names the item's real shortcut, every Amiga-key
   in the text is a menu shortcut, and every function-key combination (Shift-F5, Ctrl-F10...)
   is in the key table (src/ide/KeyBindings.cpp); the generated appendices are up to date
   (tools/manual-ref.py --check).
4. Links and pictures: every link to a file of the manual or the repository exists, every
   #anchor names a heading of its file, and every picture exists.
"""
import glob
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MAN = os.path.join(ROOT, "docs", "manual")
EXAMPLES = os.path.join(ROOT, "examples", "manual")
VERBOSE = "--verbose" in sys.argv
DOCKER = shutil.which("docker") and "--no-docker" not in sys.argv
IMAGE = os.environ.get("PPC_IMAGE") or ("walkero/amigagccondocker:os4-gcc11-arm64"
                                        if os.uname().machine in ("arm64", "aarch64")
                                        else "walkero/amigagccondocker:os4-gcc11-amd64")
BOING = os.environ.get("BOING") or next(
    (p for p in [os.path.join(ROOT, "..", d, "build", "boing") for d in ("boing_basic", "boing-basic")]
     if os.path.exists(p)), "")
BOING = os.path.abspath(BOING) if BOING else ""

DIRECTIVE = re.compile(r"<!--\s*(fragment|project)\b:?\s*(.*?)\s*-->")
FENCE = re.compile(r"^```(\w*)\s*$")

# capitalised words in code spans that aren't ARexx commands of AmiForge's
NOT_COMMANDS = {
    "RESULT", "RC", "SIGL", "AMIFORGE", "REXX", "OPTIONS", "RESULTS", "ADDRESS", "PARSE", "VAR",
    "DO", "END", "IF", "THEN", "ELSE", "SAY", "EXIT", "PROCEDURE", "LEAVE", "WHEN", "OTHERWISE",
    "DAR", "TODO", "WORLD", "TEST", "LONGEST", "README", "FORM", "FALSE", "TRUE", "APTR", "STRPTR",
    "COMMAND", "BOOLGADGET", "IDCMP", "FILE", "AF1", "ENV", "ENVARC", "NIL", "SDK", "PROGDIR",
    "LIBS", "AUTO", "WAIT", "CLOSE", "SHOUT", "TIMES", "NAME", "HEAD", "PYTHONHOME", "PYTHONPATH",
}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


REF = load("manual_ref", os.path.join(ROOT, "tools", "manual-ref.py"))


def manual_files():
    return sorted(glob.glob(os.path.join(MAN, "*.md")) + glob.glob(os.path.join(MAN, "exercises", "*.md")))


def blocks(path):
    """[(line, lang, code, directive, arg)] of a Markdown file's fenced blocks."""
    out = []
    lines = open(path, encoding="utf-8").read().split("\n")
    i = 0
    while i < len(lines):
        m = FENCE.match(lines[i])
        if not m:
            i += 1
            continue
        lang, start = m.group(1), i
        j = i + 1
        while j < len(lines) and not lines[j].startswith("```"):
            j += 1
        prev = lines[start - 1] if start else ""
        d = DIRECTIVE.search(prev)
        out.append((start + 1, lang, "\n".join(lines[start + 1:j]) + "\n", d.group(1) if d else "",
                    d.group(2) if d else ""))
        i = j + 1
    return out


def rel(p):
    return os.path.relpath(p, ROOT)


def afp_files(afp):
    files, sec = [], ""
    for line in open(afp, encoding="latin-1"):
        line = line.strip()
        if line.startswith("["):
            sec = line
        elif sec == "[files]" and line.startswith("file="):
            files.append(line[5:])
    return files


# --- 1. code -------------------------------------------------------------------------------

def check_c(fails):
    """Compile the C samples and the C projects of examples/manual in one Docker container."""
    jobs = []       # (name, [sources], include dir)
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build") if os.path.isdir(os.path.join(ROOT, "build")) else None)
    n = 0
    for md in manual_files():
        for line, lang, code, d, arg in blocks(md):
            if lang not in ("c", "cpp") or d in ("fragment", "project"):
                continue
            n += 1
            src = os.path.join(tmp, "sample%d.%s" % (n, lang))
            open(src, "w").write(code)
            jobs.append(("%s:%d" % (rel(md), line), [src], lang))
    for afp in sorted(glob.glob(os.path.join(EXAMPLES, "*", "*.afp"))):
        d = os.path.dirname(afp)
        if glob.glob(os.path.join(d, "*.bbp")):
            continue
        srcs = [os.path.join(d, f) for f in afp_files(afp) if f.endswith((".c", ".cpp"))]
        if not srcs:
            continue
        jobs.append((rel(afp), srcs, "cpp" if any(s.endswith(".cpp") for s in srcs) else "c"))
        for extra in sorted(glob.glob(os.path.join(d, "*.c"))):   # another version of the program
            if extra not in srcs:
                jobs.append((rel(extra), [extra], "c"))
    if not DOCKER:
        print("manual-check: no docker: the C samples aren't compiled (%d)" % len(jobs))
        return len(jobs)
    script = ["set +e"]
    for i, (name, srcs, lang) in enumerate(jobs):
        cc = "ppc-amigaos-g++ -athread=native" if lang == "cpp" else "ppc-amigaos-gcc"
        files = " ".join("'/src/%s'" % rel(s) for s in srcs)
        script.append("%s -mcrt=newlib -mcpu=powerpc -O0 -Wall -Werror=implicit-function-declaration "
                      "-o /tmp/p%d %s -lauto >/tmp/o%d 2>&1; echo \"RC %d $?\"; cat /tmp/o%d" % (cc, i, files, i, i, i))
    open(os.path.join(tmp, "build.sh"), "w").write("\n".join(script) + "\n")
    r = subprocess.run(["docker", "run", "--rm", "-v", ROOT + ":/src", "-w", "/src", IMAGE, "sh",
                        "/src/" + rel(os.path.join(tmp, "build.sh"))], capture_output=True, text=True)
    shutil.rmtree(tmp, ignore_errors=True)
    out = r.stdout
    for i, (name, srcs, lang) in enumerate(jobs):
        m = re.search(r"^RC %d (\d+)\n(.*?)(?=^RC \d+ |\Z)" % i, out, re.M | re.S)
        if not m:
            fails.append("%s: the compiler didn't run (%s)" % (name, (r.stderr or out)[-300:]))
        elif m.group(1) != "0":
            fails.append("%s doesn't compile:\n%s" % (name, m.group(2).strip()[:1500]))
        elif "warning:" in m.group(2):
            fails.append("%s compiles with warnings:\n%s" % (name, m.group(2).strip()[:1500]))
        elif VERBOSE:
            print("ok", name)
    return len(jobs)


def boing(args, cwd):
    return subprocess.run([BOING] + args, cwd=cwd, capture_output=True, text=True)


def check_basic(fails):
    if not BOING or not os.path.exists(BOING):
        print("manual-check: no boing (set BOING=path): the Boing BASIC samples aren't checked")
        return 0
    n = 0
    tmp = tempfile.mkdtemp()
    for md in manual_files():
        for line, lang, code, d, arg in blocks(md):
            if lang != "basic" or d == "fragment":
                continue
            n += 1
            if d == "project":
                target = os.path.join(ROOT, arg)
                r = boing(["check", os.path.basename(target)], os.path.dirname(target))
            else:
                src = os.path.join(tmp, "sample%d.bas" % n)
                open(src, "w").write(code)
                r = boing(["check", src], tmp)
            if r.returncode:
                fails.append("%s:%d: boing check: %s" % (rel(md), line, (r.stdout + r.stderr).strip()[:600]))
            elif VERBOSE:
                print("ok %s:%d" % (rel(md), line))
    for bbp in sorted(glob.glob(os.path.join(EXAMPLES, "*", "*.bbp"))):
        n += 1
        d = os.path.dirname(bbp)
        r = boing(["check", os.path.basename(bbp)], d)
        if r.returncode:
            fails.append("%s: boing check: %s" % (rel(bbp), (r.stdout + r.stderr).strip()[:600]))
            continue
        exp = os.path.join(d, "expected.out")
        if os.path.exists(exp) and shutil.which("cc"):
            vm = os.path.join(tmp, "vm-" + os.path.basename(d))
            r = boing(["vm", "--project", os.path.basename(bbp), "-o", vm], d)
            if r.returncode:
                fails.append("%s: boing vm: %s" % (rel(bbp), (r.stdout + r.stderr).strip()[:600]))
                continue
            r = subprocess.run([vm, "run", os.path.basename(bbp)], cwd=d, capture_output=True, text=True, timeout=60)
            if r.stdout != open(exp).read():
                fails.append("%s: the output differs from expected.out:\n%s" % (rel(bbp), r.stdout[:600]))
            elif VERBOSE:
                print("ok", rel(bbp), "(run)")
    shutil.rmtree(tmp, ignore_errors=True)
    return n


# --- 2. ARexx commands ---------------------------------------------------------------------

def check_rexx(fails):
    commands = {c for c, _, _, _ in REF.rexx_commands()}
    n = 0

    def known(word, where):
        nonlocal n
        n += 1
        if word in commands or word in NOT_COMMANDS:
            return
        fails.append("%s: `%s` isn't an ARexx command of AmiForge's (add it to NOT_COMMANDS in "
                     "tools/manual-check.py if it isn't meant as one)" % (where, word))

    for md in manual_files():
        text = open(md, encoding="utf-8").read()
        for line, lang, code, d, arg in blocks(md):
            if lang != "rexx":
                continue
            for m in re.finditer(r"^\s*(?:ELSE\s+|THEN\s+|IF .*? THEN\s+)?'([A-Z]+)(?: ([A-Z]+))?\b", code, re.M):
                cmd = m.group(2) if m.group(1) == "MCP" and m.group(2) else m.group(1)
                known(cmd, "%s:%d" % (rel(md), line))
        if os.path.basename(md) == "appendix-b-commands.md":
            continue                         # made from the command table itself
        prose = re.sub(r"^```.*?^```", "", text, flags=re.M | re.S)
        for m in re.finditer(r"`([A-Z][A-Z]+)(\.\.\.)?( [^`]*)?`", prose):
            word = m.group(1)
            if word == "MCP" and m.group(3):
                nxt = re.match(r" ([A-Z]{3,})\b", m.group(3))
                if nxt:
                    word = nxt.group(1)
            if m.group(2):                   # DESIGN... : a family of commands
                if not any(c.startswith(word) for c in commands):
                    fails.append("%s: no command starts with %s" % (rel(md), word))
                continue
            known(word, rel(md))
    return n


# --- 3. keys -------------------------------------------------------------------------------

def check_keys(fails):
    items = REF.menu_items()
    keyed = {}
    for title, item, sub, key in items:
        if key:
            keyed.setdefault(key.upper(), []).append((title, sub or item))
    fkeys = {k for k, _ in REF.key_table()}
    n = 0
    for md in manual_files():
        if os.path.basename(md).startswith("appendix-"):
            continue
        text = open(md, encoding="utf-8").read()
        for m in re.finditer(r"\*\*([A-Z][\w ]*?) > ([^*]+?)\*\*\s*\((?:[^)]*?)\*\*Amiga-(\S)\*\*", text):
            n += 1
            title, item, key = m.group(1), m.group(2).strip(), m.group(3).upper()
            ok = any(t == title and (i == item or i.startswith(item + " ") or i.startswith(item.rstrip(".") + "..."))
                     for t, i in keyed.get(key, []))
            if not ok:
                fails.append("%s: **%s > %s** isn't on Amiga-%s (the menus say: %s)" % (
                    rel(md), title, item, key, ", ".join("%s > %s" % x for x in keyed.get(key, [])) or "nothing"))
        for m in re.finditer(r"\bAmiga-([^\s*`|,)/]+)", text):
            k = m.group(1).rstrip(".")
            if k in ("key",) or len(k) != 1:
                continue
            n += 1
            if k.upper() not in keyed:
                fails.append("%s: Amiga-%s isn't a menu shortcut" % (rel(md), k))
        for m in re.finditer(r"\b((?:(?:Shift|Ctrl|Alt)-)+F\d+)\b", text):
            n += 1
            combo = m.group(1)
            parts = combo.split("-")
            norm = "-".join(sorted(parts[:-1], key=["Shift", "Ctrl", "Alt"].index) + parts[-1:])
            if norm not in fkeys:
                fails.append("%s: %s isn't in the key table" % (rel(md), combo))
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "manual-ref.py"), "--check"],
                       capture_output=True, text=True)
    if r.returncode:
        fails.append("the generated appendices: " + r.stdout.strip())
    return n


# --- 4. links and pictures -----------------------------------------------------------------

def gh_slug(title):
    t = re.sub(r"[`*]", "", title.strip().lower())
    t = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", t)
    t = re.sub(r"[^\w\- ]", "", t)
    return t.replace(" ", "-")


def anchors(path):
    out, seen, fence = set(), {}, False
    for line in open(path, encoding="utf-8"):
        if line.startswith("```"):
            fence = not fence
            continue
        m = None if fence else re.match(r"^#{1,6}\s+(.*)", line)
        if m:
            s = gh_slug(m.group(1))
            k = seen.get(s, 0)
            seen[s] = k + 1
            out.add(s if not k else "%s-%d" % (s, k))
    return out


def check_links(fails):
    n = 0
    for md in manual_files() + [os.path.join(EXAMPLES, "README.md")]:
        text = re.sub(r"^```.*?^```", "", open(md, encoding="utf-8").read(), flags=re.M | re.S)
        text = re.sub(r"`[^`\n]*`", "", text)
        here = os.path.dirname(md)
        for m in re.finditer(r"(!?)\[[^\]]*\]\(([^)\s]+)\)", text):
            target = m.group(2)
            if re.match(r"^[a-z]+://|^mailto:", target):
                continue
            n += 1
            path, _, anchor = target.partition("#")
            full = os.path.normpath(os.path.join(here, path)) if path else md
            if not os.path.exists(full):
                fails.append("%s: %s %s doesn't exist" % (rel(md), "picture" if m.group(1) else "link", target))
                continue
            if anchor and full.endswith(".md") and anchor not in anchors(full):
                fails.append("%s: link %s: no heading with the anchor #%s" % (rel(md), target, anchor))
    return n


def main():
    fails = []
    counts = {}
    counts["C"] = check_c(fails)
    counts["BASIC"] = check_basic(fails)
    counts["ARexx"] = check_rexx(fails)
    counts["keys"] = check_keys(fails)
    counts["links"] = check_links(fails)
    for f in fails:
        print("FAIL manual-check:", f)
    print("manual-check: %s%s" % (", ".join("%d %s" % (v, k) for k, v in counts.items()),
                                  "" if not fails else "; %d failures" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
