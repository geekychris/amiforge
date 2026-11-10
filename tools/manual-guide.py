#!/usr/bin/env python3
"""manual-guide.py [OUTDIR] - the user manual for reading on the Amiga: an AmigaGuide database
(one node per chapter, exercise and appendix, links between them, "Show the picture" links to
the screenshots and diagrams) and a plain Latin-1 text version.

    tools/manual-guide.py              build/manual/AmiForge-Manual.guide and .txt, with the
                                       pictures in build/manual/Manual/images/ (the guide's
                                       links point there, as AmiForge's Docs/ drawer has them)

md2txt() and md2guide() are Boing BASIC's (boing-basic tools/kit/kit.py, by bb-docs), kept the
same apart from taking the reading order from tools/manual_config.py and linking screenshots.
"""
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MAN = os.path.join(ROOT, "docs", "manual")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import manual_config as CFG

SUBST = {"—": "--", "–": "-", "‘": "'", "’": "'", "“": '"', "”": '"',
         "…": "...", "→": "->", "←": "<-", "↔": "<->", "•": "*", " ": " ",
         "×": "x", "≥": ">=", "≤": "<=", "≠": "!="}


def latin1(text):
    for k, v in SUBST.items():
        text = text.replace(k, v)
    return text.encode("latin-1", errors="replace")


def diagram_name(src, n):
    """The name tools/manual-diagrams.py gives the n-th Mermaid diagram of a manual file."""
    parts = os.path.normpath(src).split(os.sep)
    stem = os.path.splitext(parts[-1])[0]
    return ("ex-" + stem if len(parts) > 1 and parts[-2] == "exercises" else stem) + "-%d" % n


def manual_files():
    """The manual's files in reading order: (path, node name)."""
    out = [(os.path.join(MAN, "README.md"), "MAIN")]
    for _, chapters in CFG.PARTS:
        out += [(os.path.join(MAN, c + ".md"), c) for c in chapters]
    out += [(os.path.join(MAN, e + ".md"), e.replace("exercises/README", "exercises").replace("/", "-"))
            for e in CFG.EXERCISES]
    out += [(os.path.join(MAN, a + ".md"), a) for a in CFG.APPENDICES]
    return out


def md2txt(src):
    out, fence, mermaid, ndiag = [], False, False, 0
    for line in open(src, encoding="utf-8").read().splitlines():
        if line.lstrip().startswith("```"):
            if not fence and line.strip() == "```mermaid":         # a diagram: point to its picture
                ndiag += 1
                mermaid = True
                out.append("  [Diagram: Manual/images/diagrams/%s.png]" % diagram_name(src, ndiag))
            elif fence:
                mermaid = False
            fence = not fence
            continue
        if mermaid:
            continue
        if fence:
            out.append("    " + line)
            continue
        if line.lstrip().startswith("<!--"):     # the manual's test directives
            continue
        m = re.match(r"^\s*!\[([^\]]*)\]\(([^)]+)\)", line)
        if m:                                     # pictures: named, to look at on the Amiga
            out.append("  [Picture: Manual/%s]" % os.path.normpath(
                os.path.relpath(os.path.join(os.path.dirname(src), m.group(2)), MAN)))
            continue
        m = re.match(r"^(#{1,6})\s+(.*)", line)
        if m:
            title = re.sub(r"[`*]", "", m.group(2))
            if out and out[-1] != "":
                out.append("")
            out.append(title.upper() if len(m.group(1)) == 1 else title)
            if len(m.group(1)) <= 2:
                out.append(("=" if len(m.group(1)) == 1 else "-") * len(title))
            continue
        if re.match(r"^\|[-| :]+\|$", line.strip()):
            continue
        if line.strip().startswith("|"):
            line = "  " + "  ".join(c.strip() for c in line.strip().strip("|").split("|"))
        line = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
                      lambda mm: mm.group(1) if mm.group(1) == mm.group(2) else mm.group(1), line)
        line = re.sub(r"\*\*(.+?)\*\*", r"\1", line)
        line = re.sub(r"\*(.+?)\*", r"\1", line)
        line = line.replace("`", "")
        out.append(line)
    return "\n".join(out).rstrip() + "\n"


def _guide_esc(s):
    return s.replace("\\", "\\\\").replace("@", "\\@")


def md2guide(files, title):
    """One AmigaGuide database of markdown files: a node per file, links between them."""
    nodes = {os.path.normpath(p): n for p, n in files}
    L = ["@database " + CFG.GUIDE_NAME, "@author " + CFG.AUTHOR, '@title "%s"' % title, "@wordwrap", ""]
    for path, node in files:
        here = os.path.dirname(path)
        text = open(path, encoding="utf-8").read().splitlines()
        first = next((re.sub(r"^#+\s*", "", l) for l in text if l.startswith("#")), node)
        L.append('@node %s "%s"' % (node, _guide_esc(re.sub(r"[`*]", "", first)).replace('"', "'")))
        fence = mermaid = False
        ndiag = 0
        for line in text:
            if line.lstrip().startswith("```"):
                if not fence and line.strip() == "```mermaid":     # a diagram: a link that shows its picture
                    ndiag += 1
                    mermaid = True
                    L.append('  @{" Show the diagram " link "Manual/images/diagrams/%s.png/main"}'
                             % diagram_name(path, ndiag))
                elif fence:
                    mermaid = False
                fence = not fence
                continue
            if mermaid:
                continue
            if line.lstrip().startswith("<!--"):
                continue
            if fence:
                L.append("    " + _guide_esc(line))
                continue
            m = re.match(r"^\s*!\[([^\]]*)\]\(([^)]+)\)", line)
            if m:                                 # a screenshot: a link that shows it
                rel = os.path.normpath(os.path.relpath(os.path.join(here, m.group(2)), MAN))
                L.append('  @{" Show the picture " link "Manual/%s/main"} %s' % (rel, _guide_esc(m.group(1))))
                continue
            m = re.match(r"^(#{1,6})\s+(.*)", line)
            if m:
                t = _guide_esc(re.sub(r"[`*]", "", m.group(2)))
                L.append("")
                L.append("@{b}%s@{ub}" % (t.upper() if len(m.group(1)) == 1 else t))
                continue
            if re.match(r"^\|[-| :]+\|$", line.strip()):
                continue
            if line.strip().startswith("|"):
                line = "  " + "  ".join(c.strip() for c in line.strip().strip("|").split("|"))
            line = _guide_esc(line)

            def link(mm):
                label, target = mm.group(1), mm.group(2).split("#")[0]
                tgt = os.path.normpath(os.path.join(here, target)) if target else os.path.normpath(path)
                n = nodes.get(tgt)
                return '@{"%s" link %s}' % (label.replace('"', "'"), n) if n else label
            line = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, line)
            line = re.sub(r"\*\*(.+?)\*\*", r"@{b}\1@{ub}", line)
            line = line.replace("`", "")
            L.append(line)
        L.append("@endnode")
        L.append("")
    return latin1("\n".join(L) + "\n")


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "manual")
    os.makedirs(out, exist_ok=True)
    files = manual_files()
    missing = [p for p, _ in files if not os.path.exists(p)]
    if missing:
        print("manual-guide: missing", ", ".join(os.path.relpath(p, ROOT) for p in missing))
        return 1
    open(os.path.join(out, CFG.GUIDE_NAME), "wb").write(md2guide(files, CFG.TITLE))
    txt = "\n\n".join(md2txt(p) for p, _ in files)
    open(os.path.join(out, CFG.TEXT_NAME), "wb").write(latin1(txt))
    pics = os.path.join(out, "Manual", "images")
    shutil.rmtree(pics, ignore_errors=True)
    shutil.copytree(os.path.join(MAN, "images"), pics, ignore=shutil.ignore_patterns("manifest.txt", "*.svg"))
    print("manual-guide: wrote %s, %s and Manual/images/ in %s" % (CFG.GUIDE_NAME, CFG.TEXT_NAME, os.path.relpath(out, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
