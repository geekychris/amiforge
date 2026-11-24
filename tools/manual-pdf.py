#!/usr/bin/env python3
"""manual-pdf.py [--letter] [--out FILE] - the user manual as a PDF book.

(A copy of Boing BASIC's tools/manual-pdf.py by bb-docs; this manual's title, parts, lexers
and names are in tools/manual_config.py.)

The source stays docs/manual/*.md. Each file is converted with pandoc (gfm -> asciidoc) into
build/manual-adoc/, fixed up for a book (chapter ids, internal links, listing titles, BASIC
highlighting, figures with captions), and a master file book.adoc includes them in parts. Then
asciidoctor-pdf renders build/<PDF_NAME>.pdf with tools/manual-pdf-theme.yml; any
asciidoctor warning (an unresolved link, a missing image) fails the build.

    tools/manual-pdf.py              A4
    tools/manual-pdf.py --letter     US Letter
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MAN = os.path.join(ROOT, "docs", "manual")
OUT = os.path.join(ROOT, "build", "manual-adoc")
PANDOC = shutil.which("pandoc") or "/opt/homebrew/bin/pandoc"
ASCIIDOCTOR_PDF = shutil.which("asciidoctor-pdf") or "/opt/homebrew/bin/asciidoctor-pdf"
sys.path.insert(0, os.path.join(ROOT, "tools"))
import manual_config as CFG
REPO_URL = CFG.REPO_URL
import importlib.util as _ilu
_spec = _ilu.spec_from_file_location("manual_diagrams", os.path.join(ROOT, "tools", "manual-diagrams.py"))
DIAG = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(DIAG)

PARTS, EXERCISES, APPENDICES = CFG.PARTS, CFG.EXERCISES, CFG.APPENDICES


def stem_id(stem):
    """'exercises/04-first-form' -> 'ex-04-first-form', '12-forms' -> 'ch-12-forms'."""
    if stem.startswith("exercises/"):
        return "ex-" + stem.split("/", 1)[1].lower()
    if stem.startswith("appendix"):
        return stem.lower()
    return "ch-" + stem.lower()


def gh_slug(title):
    """The anchor GitHub gives a heading (as the manual's links use them)."""
    t = re.sub(r"[`*]", "", title.strip().lower())
    t = re.sub(r"[^\w\- ]", "", t)
    return t.replace(" ", "-")


def all_stems():
    out = [s for _, ch in PARTS for s in ch] + EXERCISES + APPENDICES
    return out


CODE_TOKEN = "BBCODESPAN%dX"


def code_spans(md):
    """The inline code spans of a Markdown text (outside fenced blocks), as written: a list of
    (start, end, text) with the text as the reader sees it (table-cell \\| unescaped)."""
    out, fence, pos = [], False, 0
    for line in md.split("\n"):
        if line.lstrip().startswith("```"):
            fence = not fence
        elif not fence:
            for m in re.finditer(r"(`+)(.+?)(?<!`)\1(?!`)", line):
                text = m.group(2)
                if text.startswith(" ") and text.endswith(" ") and text.strip():
                    text = text[1:-1]
                if line.lstrip().startswith("|"):
                    text = text.replace("\\|", "|")
                out.append((pos + m.start(), pos + m.end(), text))
        pos += len(line) + 1
    return out


def protect_code_spans(md):
    """Replace inline code with tokens pandoc leaves alone: AsciiDoc would read #, ^, ~, *, _,
    + and {name} inside backticks as markup."""
    spans = code_spans(md)
    texts = []
    for start, end, text in reversed(spans):
        texts.append(text)
        md = md[:start] + CODE_TOKEN % (len(spans) - len(texts)) + md[end:]
    texts.reverse()
    return md, texts


def restore_code_spans(adoc, texts):
    """Each token -> monospace text passed through as it is (only <, > and & escaped). In a
    table row a | is escaped, or it would end the cell."""
    out = []
    for line in adoc.split("\n"):
        cell = line.startswith("|")

        def put(mm):
            t = texts[int(mm.group(1))]
            if cell:
                t = t.replace("|", "\\|")
            core = t.rstrip("\\")          # pass:c[...\] would read as an escaped ]: trailing
            tail = "{backslash}" * (len(t) - len(core))   # backslashes go outside the macro
            return "``%s%s``" % ("pass:c[%s]" % core.replace("]", "\\]") if core else "", tail)
        out.append(re.sub(r"BBCODESPAN(\d+)X", put, line))
    return "\n".join(out)


def convert(stem, known):
    src = os.path.join(MAN, stem + ".md")
    md = open(src, encoding="utf-8").read()
    md = re.sub(r"<!--.*?-->\n?", "", md, flags=re.S)          # the manual-check directives
    md, spans = protect_code_spans(md)
    adoc = subprocess.run([PANDOC, "-f", "gfm", "-t", "asciidoc", "--wrap=none"], input=md,
                          capture_output=True, text=True, check=True).stdout
    here = os.path.dirname(src)
    sid = stem_id(stem)
    lines = adoc.split("\n")
    out, seen, first = [], {}, True
    for line in lines:
        if re.match(r"^\[\[[^\]]+\]\]$", line):
            continue        # pandoc's own heading ids (which may start with a digit): ours follow
        m = re.match(r"^(=+) (.*)$", line)
        if m:
            title = m.group(2)
            plain = re.sub(r"BBCODESPAN(\d+)X", lambda mm: spans[int(mm.group(1))], title)
            slug = gh_slug(re.sub(r"\[.*?\]|link:\S+", "", plain))
            n = seen.get(slug, 0)
            seen[slug] = n + 1
            anchor = sid if first else "%s-%s" % (sid, slug + ("-%d" % n if n else ""))
            if first:
                # chapter titles: "Appendix A: Keywords" -> "Keywords" (asciidoctor adds the letter)
                title = re.sub(r"^Appendix [A-Z]: ", "", title)
            out.append("[[%s]]" % anchor)
            out.append("%s %s" % (m.group(1), title))
            first = False
            continue
        out.append(line)
    adoc = "\n".join(out)

    # links between the manual's files -> internal cross references
    def link(mm):
        target, text = mm.group(1), mm.group(2)
        path, _, anchor = target.partition("#")
        if not path:
            dest = os.path.normpath(src)
        else:
            dest = os.path.normpath(os.path.join(here, path))
        rel = os.path.relpath(dest, MAN)
        if rel.endswith(".md"):
            st = rel[:-3]
            if st in known:
                xid = stem_id(st) + ("-" + anchor if anchor else "")
                return "xref:%s[%s]" % (xid, text)
            if st == "README":                                   # the contents page
                return text
        # a file of the repository: link to it on GitHub
        repo_rel = os.path.relpath(dest, ROOT)
        return "%s%s[%s]" % (REPO_URL, repo_rel, text)
    adoc = re.sub(r"link:([^\[\s]+)\[([^\]]*)\]", link, adoc)

    # "Listing N.M (path)" paragraphs -> the title of the block that follows
    adoc = re.sub(r"_Listing (\d+\.\d+)_ \((BBCODESPAN\d+X)\)\n\n\[source,(\w+)\]",
                  lambda mm: ".Listing %s (%s)\n[source,%s]" % (mm.group(1), mm.group(2), mm.group(3)), adoc)
    adoc = re.sub(r"\[source,(%s)\]" % "|".join(CFG.PLAIN), "[listing]", adoc)
    adoc = re.sub(r"\[source,(\w+)\]", lambda mm: "[source,%s]" % CFG.LEXERS.get(mm.group(1), mm.group(1)), adoc)

    # Mermaid diagrams -> pictures rendered at 3x by mmdc (tools/manual-diagrams.py), captioned
    # by the "Figure: ..." line after them
    md_name = DIAG.name_of(src)
    counter = [0]

    def diagram(mm):
        counter[0] += 1
        name = "%s-%d" % (md_name, counter[0])
        os.makedirs(os.path.join(OUT, "diagrams"), exist_ok=True)
        png = os.path.join(OUT, "diagrams", name + ".png")
        try:
            DIAG.render(mm.group(1) + "\n", png, scale=3)
        except RuntimeError as e:
            raise SystemExit("manual-pdf: diagram %s doesn't render: %s" % (name, e))
        small = os.path.join(DIAG.DIR, name + ".png")       # its width at 1x sets its width on the page
        width = 100
        try:
            from PIL import Image
            width = max(30, min(100, round(Image.open(small).size[0] / 10)))   # 1000 px = the page width
        except Exception:
            pass
        title = mm.group(2) or name
        return ".%s\nimage::%s[%s,pdfwidth=%d%%]\n" % (title, png, name, width)
    adoc = re.sub(r"\[source,mermaid\]\n----\n(.*?)\n----\n(?:\n_Figure: (.*?)_\n)?", diagram, adoc, flags=re.S)
    if "[source,mermaid]" in adoc:
        raise SystemExit("manual-pdf: %s: a mermaid block wasn't turned into a picture" % stem)

    # pictures: a paragraph that is only an image -> a figure, captioned by the italic line after it
    def figure(mm):
        path, alt, cap = mm.group(1), mm.group(2), mm.group(4)
        full = os.path.normpath(os.path.join(here, path))
        title = cap if cap else alt
        return ".%s\nimage::%s[%s]\n" % (title, full, alt.replace(",", ""))
    adoc = re.sub(r"^image:(?!:)([^\[\s]+)\[([^\]]*)\]\n(\n_([^\n]+)_\n)?", figure, adoc, flags=re.M)
    if re.search(r"^_Listing \d+\.\d+_", adoc, re.M):
        raise SystemExit("manual-pdf: %s: a Listing caption wasn't turned into a block title" % stem)
    adoc = restore_code_spans(adoc, spans)
    open(os.path.join(OUT, stem.replace("/", "_") + ".adoc"), "w", encoding="utf-8").write(adoc)


def version():
    if os.environ.get("MANUAL_VERSION"):
        return os.environ["MANUAL_VERSION"]
    try:
        return subprocess.run(["git", "-C", ROOT, "describe", "--tags", "--always", "--dirty"],
                              capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return "unknown"


def master(letter):
    import datetime
    L = ["= " + CFG.TITLE,
         ":doctype: book",
         ":revnumber: " + version(),
         ":revdate: " + datetime.date.today().isoformat(),
         ":toc:", ":toclevels: 2", ":sectnums:", ":sectnumlevels: 1", ":partnums!:",
         ":chapter-signifier: Chapter",
         ":source-highlighter: rouge", ":rouge-style: github",
         ":pdf-theme: manual-pdf-theme.yml", ":pdf-themesdir: " + os.path.join(ROOT, "tools"),
         ":pdf-page-size: " + ("Letter" if letter else "A4"),
         ":manual-name: " + CFG.SHORT_NAME,                      # the running footer
         ] + ([":title-logo-image: image:" + os.path.join(MAN, CFG.LOGO) + "[pdfwidth=45%,align=center]"]
              if CFG.LOGO else []) + [   # (no blank line: it would end the header)
         ":icons: font", ":experimental:", ":hide-uri-scheme:",
         ":listing-caption!:", ":figure-caption!:", ":table-caption!:",
         ""]
    for title, chapters in PARTS:
        L += ["= " + title, ""]
        for c in chapters:
            L += ["include::%s.adoc[leveloffset=0]" % c.replace("/", "_"), ""]
    L += ["= Exercises", "", ":sectnums!:", ""]
    for c in EXERCISES:
        L += ["include::%s.adoc[]" % c.replace("/", "_"), ""]
    L += [":sectnums:", ""]
    for c in APPENDICES:
        L += ["[appendix]", "include::%s.adoc[]" % c, ""]
    open(os.path.join(OUT, "book.adoc"), "w", encoding="utf-8").write("\n".join(L))


def main():
    letter = "--letter" in sys.argv
    out = os.path.join(ROOT, "build", "%s%s.pdf" % (CFG.PDF_NAME, "-Letter" if letter else ""))
    if "--out" in sys.argv:
        out = os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    known = set(all_stems())
    for st in known:
        convert(st, known)
    master(letter)
    r = subprocess.run([ASCIIDOCTOR_PDF, "-v", "-r", os.path.join(ROOT, "tools", "manual-pdf-lexer.rb"), "--failure-level=WARN", "-o", out, os.path.join(OUT, "book.adoc")],
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if "possible invalid reference" in r.stderr:     # asciidoctor reports these only as INFO
        print("manual-pdf: unresolved cross references (see above)", file=sys.stderr)
        return 1
    if r.returncode:
        print("manual-pdf: asciidoctor-pdf failed (warnings count as errors)", file=sys.stderr)
        return 1
    qpdf = shutil.which("qpdf")
    if qpdf:                              # asciidoctor-pdf leaves most streams uncompressed
        tmp = out + ".tmp"
        subprocess.run([qpdf, "--compress-streams=y", "--recompress-flate", "--object-streams=generate",
                        out, tmp], check=True)
        os.replace(tmp, out)
    print("manual-pdf: wrote %s (%d KB)" % (os.path.relpath(out, ROOT), os.path.getsize(out) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
