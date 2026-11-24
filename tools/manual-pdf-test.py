#!/usr/bin/env python3
"""manual-pdf-test.py [PDF] - check the PDF of the user manual (make manual-pdf-test): it exists,
has at least MIN_PAGES pages, and (with pdftotext) contains every chapter's, exercise's and
appendix's title and every inline code span of the Markdown (characters such as # ^ ~ * + or
{name} in code must survive the conversion to AsciiDoc)."""
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import manual_config as CFG   # (a copy of Boing BASIC's manual-pdf-test.py; this manual's names there)
PDF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", CFG.PDF_NAME + ".pdf")
MIN_PAGES = CFG.MIN_PAGES


def main():
    fails = []
    if not os.path.exists(PDF):
        print("FAIL manual-pdf: %s is missing (make manual-pdf)" % PDF)
        return 1
    pages, info = 0, ""
    if shutil.which("pdfinfo"):
        info = subprocess.run(["pdfinfo", PDF], capture_output=True, text=True).stdout
        m = re.search(r"^Pages:\s+(\d+)", info, re.M)
        pages = int(m.group(1)) if m else 0
    else:
        pages = open(PDF, "rb").read().count(b"/Type /Page\n") or open(PDF, "rb").read().count(b"/Type/Page")
    if pages < MIN_PAGES:
        fails.append("only %d pages (expected at least %d)" % (pages, MIN_PAGES))
    if shutil.which("pdftotext"):
        text = subprocess.run(["pdftotext", "-layout", PDF, "-"], capture_output=True, text=True).stdout
        flat = re.sub(r"\s+", " ", text)
        for md in sorted(glob.glob(os.path.join(ROOT, "docs", "manual", "*.md")) +
                         glob.glob(os.path.join(ROOT, "docs", "manual", "exercises", "*.md"))):
            if os.path.basename(md) == "README.md" and "exercises" not in md:
                continue
            title = open(md, encoding="utf-8").readline().lstrip("# ").strip()
            title = re.sub(r"^Appendix [A-Z]: ", "", title).replace("`", "")
            if re.sub(r"\s+", " ", title) not in flat:
                fails.append("title not found in the PDF: %s (%s)" % (title, os.path.relpath(md, ROOT)))
        # AsciiDoc markup that leaked into the text: an anchor it didn't accept (an id starting
        # with a digit), or a cross reference it couldn't resolve
        for leak in sorted(set(re.findall(r"\[\[[^\]\s]+\]\]|<<[^>\s]+>>", text)))[:10]:
            fails.append("AsciiDoc markup in the PDF's text: %s" % leak)
        # every inline code span, compared without white space (lines wrap in the PDF)
        import importlib.util
        spec = importlib.util.spec_from_file_location("manual_pdf", os.path.join(ROOT, "tools", "manual-pdf.py"))
        mp = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mp)
        # the body text in reading order, without the running headers and footers (a span split
        # by a page break would otherwise have them in the middle)
        size = re.search(r"Page size:\s+([\d.]+) x ([\d.]+)", info or "")
        w, h = (float(size.group(1)), float(size.group(2))) if size else (612.0, 842.0)
        body = subprocess.run(["pdftotext", "-raw", "-x", "0", "-y", "58", "-W", str(int(w) + 10),
                               "-H", str(int(h) - 120), PDF, "-"], capture_output=True, text=True).stdout
        squashed = re.sub(r"\s+", "", body)
        missing = 0
        for stem in mp.all_stems():
            md = open(os.path.join(ROOT, "docs", "manual", stem + ".md"), encoding="utf-8").read()
            md = re.sub(r"<!--.*?-->", "", md, flags=re.S)
            for _, _, code in mp.code_spans(md):
                c = re.sub(r"\s+", "", code)
                if c and c not in squashed:
                    missing += 1
                    if missing <= 20:
                        fails.append("inline code missing from the PDF: %s: `%s`" % (stem, code))
        if missing > 20:
            fails.append("... %d inline code spans missing in all" % missing)
    # one picture per figure: the Markdown's images, one per Mermaid diagram, and the title logo
    if shutil.which("pdfimages") and shutil.which("pdftotext"):
        import importlib.util
        spec = importlib.util.spec_from_file_location("manual_pdf2", os.path.join(ROOT, "tools", "manual-pdf.py"))
        mp2 = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mp2)
        images = diagrams = 0
        captions = []
        for stem in mp2.all_stems():
            md = open(os.path.join(ROOT, "docs", "manual", stem + ".md"), encoding="utf-8").read()
            images += len(re.findall(r"^\s*!\[", md, re.M))
            for m in re.finditer(r"^```mermaid\n.*?^```\n\n\*Figure: (.*?)\*", md, re.M | re.S):
                captions.append((stem, m.group(1)))
            diagrams += len(re.findall(r"^```mermaid$", md, re.M))
        lst = subprocess.run(["pdfimages", "-list", PDF], capture_output=True, text=True).stdout.splitlines()[2:]
        got = sum(1 for l in lst if l.split()[2:3] == ["image"])
        want = images + diagrams + (1 if CFG.LOGO else 0)
        if got != want:
            fails.append("%d pictures in the PDF, expected %d (%d images, %d diagrams, the title logo)"
                         % (got, want, images, diagrams))
        if len(captions) != diagrams:
            fails.append("%d diagrams but %d 'Figure:' captions after them" % (diagrams, len(captions)))
        flat_body = re.sub(r"\s+", " ", subprocess.run(["pdftotext", "-raw", PDF, "-"], capture_output=True,
                                                         text=True).stdout)
        flat_body = flat_body.replace("\u2019", "'").replace("\u2018", "'").replace("\u201c", '"').replace("\u201d", '"')
        for stem, cap in captions:
            if re.sub(r"\s+", " ", cap)[:60] not in flat_body:
                fails.append("diagram caption not in the PDF: %s: %s" % (stem, cap[:60]))
        print("manual-pdf: %d pictures (%d diagrams)" % (got, diagrams))
    for f in fails:
        print("FAIL manual-pdf:", f)
    print("manual-pdf: %s, %d pages, %d KB%s" % (os.path.relpath(PDF, ROOT), pages, os.path.getsize(PDF) // 1024,
                                               "" if not fails else ", %d failures" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
