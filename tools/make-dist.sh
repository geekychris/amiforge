#!/bin/zsh
# make-dist.sh — package AmiForge as dist/AmiForge.lha:
#
#   AmiForge.info                 drawer icon
#   AmiForge/AmiForge(.info)      the IDE (stripped build/AmiForge) and its tool icon
#   AmiForge/Examples/<name>/     hello, shapes, asmdemo, each .afp with a project icon
#   AmiForge/Docs/                README.txt, user-guide.txt (Latin-1 text from the Markdown)
#   AmiForge/Install-AmiForge     AmigaDOS script: copies the drawer to a destination
#   AmiForge/Templates/<id>/      project templates (template.def + files)
#   AmiForge/Rexx/                example ARexx macros (the Macros menu)
#
# Build the IDE first (tools/build.sh), or set AMIFORGE_BIN. Icons come from tools/make-icons.py (needs Pillow).
# The archive is made with LHa for UNIX from the build image (the host's lha may be Lhasa,
# which cannot create archives).
set -e
root=${0:A:h:h}
dist=$root/dist
stage=$dist/stage
image=${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11}

# AMIFORGE_BIN packages another binary in its place (to test the archive before the IDE links).
bin=${AMIFORGE_BIN:-$root/build/AmiForge}
if [[ ! -f $bin ]]; then
    print -u2 "make-dist: ${bin#$root/} does not exist. Build the IDE first: tools/build.sh build/AmiForge"
    exit 1
fi

rm -rf $stage $dist/AmiForge.lha
mkdir -p $stage/AmiForge/Examples $stage/AmiForge/Docs
python3 $root/tools/make-icons.py $dist/icons >/dev/null
icons=$dist/icons
d=$stage/AmiForge

# the drawer, the IDE
cp $icons/AmiForge-drawer.info $stage/AmiForge.info
cp $bin $d/AmiForge
cp $icons/AmiForge.info $d/AmiForge.info
# for QEMU's sam460ex machine: see Docs/user-guide.txt
if [ -f $root/build/QEMUCopyFix ]; then
    mkdir -p $d/Tools
    cp $root/build/QEMUCopyFix $d/Tools/QEMUCopyFix
    cp $icons/drawer.info $d/Tools.info
fi

# --- integrations --- ARexx macros for the Macros menu
mkdir -p $d/Rexx
cp $root/Rexx/*.rexx $d/Rexx/
cp $icons/drawer.info $d/Rexx.info

# examples: sources plus a project icon per .afp
cp $icons/drawer.info $d/Examples.info
for ex in hello shapes asmdemo bouncer; do
    [[ -d $root/examples/$ex ]] || { print -u2 "make-dist: examples/$ex is missing"; exit 1 }
    mkdir -p $d/Examples/$ex
    cp $root/examples/$ex/*(.) $d/Examples/$ex/
    cp $icons/drawer.info $d/Examples/$ex.info
    for afp in $d/Examples/$ex/*.afp(N); do cp $icons/project.info $afp.info; done
done

# --- designer ---
# project templates (Project > New Project from Template) in PROGDIR:Templates
mkdir -p $d/Templates
cp -R $root/templates/. $d/Templates/
cp $icons/drawer.info $d/Templates.info
# --- end designer ---

# docs: Markdown to plain Latin-1 text
cp $icons/drawer.info $d/Docs.info
md2txt() {
    python3 - "$1" "$2" <<'EOF'
import re, sys
src, dst = sys.argv[1], sys.argv[2]
subst = {"—": "--", "–": "-", "‘": "'", "’": "'", "“": '"', "”": '"',
         "…": "...", "→": "->", "←": "<-", "↔": "<->", "•": "*", " ": " "}
out, fence = [], False
for line in open(src, encoding="utf-8").read().splitlines():
    if line.lstrip().startswith("```"):
        fence = not fence
        continue
    if fence:
        out.append("    " + line)
        continue
    m = re.match(r"^(#{1,6})\s+(.*)", line)
    if m:
        title = re.sub(r"[`*]", "", m.group(2))
        level = len(m.group(1))
        if out and out[-1] != "":
            out.append("")
        out.append(title.upper() if level == 1 else title)
        if level <= 2:
            out.append(("=" if level == 1 else "-") * len(title))
        continue
    line = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: m.group(1) if m.group(1) == m.group(2) else f"{m.group(1)} ({m.group(2)})", line)
    line = re.sub(r"\*\*(.+?)\*\*", r"\1", line)
    line = re.sub(r"__(.+?)__", r"\1", line)
    line = line.replace("`", "")
    out.append(line)
text = "\n".join(out).rstrip() + "\n"
for k, v in subst.items():
    text = text.replace(k, v)
open(dst, "wb").write(text.encode("latin-1", errors="replace"))
EOF
}
md2txt $root/README.md $d/Docs/README.txt
md2txt $root/docs/user-guide.md $d/Docs/user-guide.txt
cp $icons/doc.info $d/Docs/README.txt.info
cp $icons/doc.info $d/Docs/user-guide.txt.info

# the installer script
cat > $d/Install-AmiForge <<'EOF'
.key DEST
.bra {
.ket }
; Install-AmiForge [DEST]
; Copies the AmiForge drawer (the one this script is in) into the drawer DEST,
; for example Work: or SYS:Utilities. Without DEST it asks with a file requester.
; Run it from its icon, or from a Shell: Execute Install-AmiForge Work:
FailAt 21
Set AFsrc `CD`
If NOT EXISTS "$AFsrc/AmiForge"
    Echo "Run Install-AmiForge from the AmiForge drawer (CD there first)."
    UnSet AFsrc
    Quit 10
EndIf
Set AFdest "{DEST}"
If "$AFdest" EQ ""
    RequestFile >ENV:AFdest DRAWERSONLY NOICONS DRAWER SYS: TITLE "Install AmiForge into which drawer?" POSITIVE Install
    If WARN
        Echo "Installation cancelled."
        UnSet AFsrc
        UnSet AFdest
        Quit 5
    EndIf
    Set AFdest `Type ENV:AFdest`
    Delete ENV:AFdest QUIET
EndIf
If NOT EXISTS $AFdest
    Echo "There is no drawer $AFdest."
    Quit 10
EndIf
CD $AFdest
If NOT EXISTS AmiForge
    MakeDir AmiForge
EndIf
Copy "$AFsrc" TO AmiForge ALL CLONE QUIET
Copy "$AFsrc.info" TO "" CLONE QUIET
Echo "AmiForge is installed in $AFdest."
CD "$AFsrc"
UnSet AFsrc
UnSet AFdest
EOF
cp $icons/install.info $d/Install-AmiForge.info
chmod 755 $d/AmiForge $d/Install-AmiForge

# archive (inside the build image: LHa for UNIX)
# Files only: xadmaster reports explicit directory entries as errors; the paths imply them.
docker run --rm -v $dist:/dist -w /dist/stage $image sh -c 'lha aq ../AmiForge.lha AmiForge.info $(find AmiForge -type f | sort)'
rm -rf $stage
print "dist/AmiForge.lha: $(wc -c < $dist/AmiForge.lha | tr -d ' ') bytes"
