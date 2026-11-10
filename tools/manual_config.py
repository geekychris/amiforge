"""manual_config.py - what makes AmiForge's user manual this manual. The manual tools
(manual-pdf.py, manual-pdf-test.py, manual-diagrams.py, manual-guide.py, manual-check.py) are
copies of Boing BASIC's (boing-basic tools/, by bb-docs) kept structurally the same so fixes can
be ported both ways; everything specific to this manual is here."""

TITLE = "AmiForge: The AmigaOS 4 IDE"
SUBTITLE = "User manual"
SHORT_NAME = "AmiForge"          # the running footer of the PDF
PDF_NAME = "AmiForge-Manual"            # build/AmiForge-Manual.pdf
GUIDE_NAME = "AmiForge-Manual.guide"     # build/manual/AmiForge-Manual.guide
TEXT_NAME = "AmiForge-Manual.txt"
AUTHOR = "AmiForge"
REPO_URL = "https://github.com/geekychris/amiforge/blob/main/"
LOGO = ""                                # docs/manual/<LOGO> on the title page; "" for none
MIN_PAGES = 115                 # the manual is about 125 pages; fewer means something went missing

PARTS = [
    ("Getting started", ["01-introduction", "02-tour"]),
    ("The editor", ["03-editing", "04-navigation"]),
    ("Projects and building", ["05-projects", "06-building", "07-designer"]),
    ("Debugging", ["08-debugging", "09-edit-and-continue", "10-debugger-tools"]),
    ("Tools and integration", ["11-git", "12-markdown", "13-arexx-mcp", "14-qemu", "15-settings",
                               "16-troubleshooting"]),
]
EXERCISES = ["exercises/README", "exercises/01-first-program", "exercises/02-form-app",
             "exercises/03-debugging", "exercises/04-edit-and-continue", "exercises/05-basic-extension",
             "exercises/06-git", "exercises/07-arexx"]
APPENDICES = ["appendix-a-keys", "appendix-b-commands", "appendix-c-menus", "appendix-d-files"]

# ```language of the Markdown -> the Rouge lexer for the PDF (tools/manual-pdf-lexer.rb adds
# boingbasic); a language not here is highlighted under its own name if Rouge knows it
LEXERS = {"basic": "boingbasic", "c": "c", "cpp": "cpp", "rexx": "rexx", "arexx": "rexx",
          "python": "python", "asm": "nasm", "sh": "shell", "amigados": "text", "ini": "ini"}
# blocks shown as plain listings (no highlighting)
PLAIN = ("output", "text", "amigados", "log")
