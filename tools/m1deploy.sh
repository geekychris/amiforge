#!/bin/zsh
# m1deploy.sh FILE[=DEST]... — write files into the dev HDF (DH1:) on m1 with QEMU stopped,
# then start it again. Reliable for big files (the bridge push isn't). DEST is relative to
# DH1: (default AmiForge/<name>). Takes about 90 s because of the reboot.
M1=${M1:-m1.local}
specs=()
for a in "$@"; do
  src=${a%%=*}; dest=${a#*=}; [ "$dest" = "$a" ] && dest=AmiForge/${src:t}
  scp -q $src $M1:/tmp/af-deploy-${src:t} || exit 1
  specs+=("/tmp/af-deploy-${src:t}=$dest")
done
ssh $M1 "zsh -lc 'retro down os4' >/dev/null; sleep 2; H=~/AmigaOS4/amigaos4-dev.hdf
for s in ${specs[*]}; do src=\${s%%=*}; dest=\${s#*=}
  xdftool -f \$H open part=0 + delete \$dest >/dev/null 2>&1
  xdftool -f \$H open part=0 + write \$src \$dest 2>&1 | grep -i error
  xdftool -r \$H open part=0 + list \$dest 2>&1 | tail -1; rm -f \$src
done
zsh -lc 'retro up os4' 2>&1 | tail -1"
${0:A:h}/m1reconnect.sh
