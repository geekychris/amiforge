#!/bin/sh
# manual-pdf.sh [--letter] - build the user manual as a PDF (build/AmiForge-Manual.pdf) and check
# it. Needs pandoc and asciidoctor-pdf (qpdf, pdfinfo and pdftotext are used when there).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
python3 "$root/tools/manual-diagrams.py" --check     # every Mermaid diagram renders, pictures up to date
python3 "$root/tools/manual-pdf.py" "$@"
case " $* " in
*" --letter "*) python3 "$root/tools/manual-pdf-test.py" "$root/build/AmiForge-Manual-Letter.pdf" ;;
*) python3 "$root/tools/manual-pdf-test.py" ;;
esac
