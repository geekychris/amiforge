#!/bin/sh
# check-cpu.sh FILE... — fail if any AmigaOS 4 PPC binary contains 440/460-only instructions.
#
# GCC with -mcpu=440 or -mcpu=440fp inlines strlen, strcat, strcmp etc. with dlmzb, an
# instruction only the PowerPC 440/460 cores have. QEMU's sam460ex runs it, but real X5000
# (e5500), A1222 (e300) and G3/G4 machines stop with an illegal-instruction alert (0x80000004).
# AmiForge is built with -mcpu=powerpc; this check keeps it that way. Files that aren't ELF
# are skipped. Runs where ppc-amigaos-objdump is on PATH (the build image; see tools/build.sh).
OBJDUMP=${OBJDUMP:-ppc-amigaos-objdump}
bad=0
for f in "$@"; do
    [ -f "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = "177ELF" ] || continue
    n=$($OBJDUMP -d "$f" | grep -c dlmzb)
    if [ "$n" -ne 0 ]; then
        echo "check-cpu: $f has $n dlmzb instructions (440/460-only): build with -mcpu=powerpc" >&2
        bad=1
    fi
done
[ $bad -eq 0 ] && echo "check-cpu: no 440/460-only instructions in $# files"
exit $bad
