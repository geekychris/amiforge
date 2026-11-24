#!/usr/bin/env python3
"""manual-diagrams.py [--check] - the user manual's Mermaid diagrams as pictures.

Every ```mermaid block in docs/manual (in reading order) is a diagram named <file>-<n>
(exercises/03-classes.md's first one: ex-03-classes-1). This renders each with the Mermaid
CLI (mmdc, theme tools/manual-mermaid.json) to docs/manual/images/diagrams/<name>.png, the
picture the AmigaGuide and the plain-text manual point to, and records the source's hash in
docs/manual/images/diagrams/manifest.txt.

    tools/manual-diagrams.py           render the diagrams whose source changed
    tools/manual-diagrams.py --check   render every diagram again (in a temporary drawer) and
                                       fail on any mmdc error, a "Syntax error" picture, a
                                       missing picture or one older than its source
The PDF (tools/manual-pdf.py) renders its own copies at a higher resolution with render().
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MAN = os.path.join(ROOT, "docs", "manual")
DIR = os.path.join(MAN, "images", "diagrams")
CONFIG = os.path.join(ROOT, "tools", "manual-mermaid.json")
MMDC = shutil.which("mmdc") or "/opt/homebrew/bin/mmdc"


def name_of(path):
    rel = os.path.relpath(path, MAN)[:-3]
    return ("ex-" + rel.split("/", 1)[1]) if rel.startswith("exercises/") else rel


def diagrams():
    """[(name, file, code)] for every mermaid block of the manual."""
    out = []
    files = sorted(glob.glob(os.path.join(MAN, "*.md"))) + sorted(glob.glob(os.path.join(MAN, "exercises", "*.md")))
    for f in files:
        text = open(f, encoding="utf-8").read()
        for n, m in enumerate(re.finditer(r"^```mermaid\n(.*?)^```", text, re.M | re.S), 1):
            out.append(("%s-%d" % (name_of(f), n), f, m.group(1)))
    return out


def render(code, png, scale=1, width=900):
    """Render one diagram; raises RuntimeError with mmdc's message on failure."""
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "d.mmd")
        open(src, "w", encoding="utf-8").write(code)
        r = subprocess.run([MMDC, "-i", src, "-o", png, "-c", CONFIG, "-b", "white", "-s", str(scale),
                            "-w", str(width), "-q"], capture_output=True, text=True, timeout=180)
        if r.returncode or not os.path.exists(png):
            msg = (r.stderr or r.stdout or "mmdc failed").strip().split("\n")
            keep = []
            for line in msg:                       # the parse error, not the stack trace after it
                if line.lstrip().startswith(("at ", "Parser.")):
                    break
                keep.append(line)
            raise RuntimeError(" / ".join(l for l in keep if l.strip())[:400])
    svg_like = open(png, "rb").read()
    return len(svg_like)


def digest(code):
    return hashlib.sha1((code + open(CONFIG).read()).encode()).hexdigest()[:12]


def read_manifest():
    m = {}
    p = os.path.join(DIR, "manifest.txt")
    if os.path.exists(p):
        for line in open(p):
            if line.strip() and not line.startswith("#"):
                k, v = line.split()
                m[k] = v
    return m


def main():
    check = "--check" in sys.argv
    ds = diagrams()
    manifest = read_manifest()
    fails = []
    os.makedirs(DIR, exist_ok=True)
    tmpdir = tempfile.mkdtemp()
    for name, f, code in ds:
        png = os.path.join(DIR, name + ".png")
        h = digest(code)
        if check:
            try:
                render(code, os.path.join(tmpdir, name + ".png"))
            except RuntimeError as e:
                fails.append("%s (%s): mmdc: %s" % (name, os.path.relpath(f, ROOT), e))
            if "Syntax error" in code or not os.path.exists(png) or manifest.get(name) != h:
                fails.append("%s: docs/manual/images/diagrams/%s.png is missing or out of date "
                             "(run tools/manual-diagrams.py)" % (name, name))
        elif manifest.get(name) != h or not os.path.exists(png):
            render(code, png)
            manifest[name] = h
            print("rendered", os.path.relpath(png, ROOT))
    shutil.rmtree(tmpdir, ignore_errors=True)
    names = {n for n, _, _ in ds}
    if check:
        extra = [p for p in glob.glob(os.path.join(DIR, "*.png")) if os.path.basename(p)[:-4] not in names]
        fails += ["%s: no diagram has this name any more" % os.path.relpath(p, ROOT) for p in extra]
        for x in fails:
            print("FAIL manual-diagrams:", x)
        print("manual-diagrams: %d diagrams rendered%s" % (len(ds), "" if not fails else ", %d failures" % len(fails)))
        return 1 if fails else 0
    for p in glob.glob(os.path.join(DIR, "*.png")):
        if os.path.basename(p)[:-4] not in names:
            os.unlink(p)
    with open(os.path.join(DIR, "manifest.txt"), "w") as fh:
        fh.write("# tools/manual-diagrams.py: diagram name and the hash of its source\n")
        for n in sorted(names):
            fh.write("%s %s\n" % (n, manifest[n]))
    print("manual-diagrams: %d diagrams" % len(ds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
