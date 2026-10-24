#!/bin/zsh
# push-sdk.sh SDK_INSTALL_DIR — copy the SDK archives and install-sdk into DH0:SDK-Install on the guest.
# Big archives go in 8 MB parts (resumable: parts already there with the right size are skipped),
# then AmigaDOS Join puts them back together.
setopt null_glob
src=$1; here=${0:A:h}; tools=${here:h}
parts=${TMPDIR:-/tmp}/af-sdk-parts; mkdir -p $parts
$tools/m1dos.sh 'makedir DH0:SDK-Install' >/dev/null 2>&1
$tools/m1push.sh $here/install-sdk DH0:SDK-Install/install-sdk
size_on_amiga() { $tools/m1dos.sh "list DH0:SDK-Install/$1 LFORMAT %L" 2>/dev/null | tr -dc '0-9' }
push() {  # push LOCAL NAME (skip if already there with the right size)
  local want=$(stat -f %z $1)
  [ "$(size_on_amiga $2)" = "$want" ] && { echo "  have $2"; return 0 }
  for try in 1 2 3 4 5; do
    $tools/m1push.sh $1 DH0:SDK-Install/$2 && [ "$(size_on_amiga $2)" = "$want" ] && return 0
    echo "  retry $2 ($try)"; sleep 20
  done
  return 1
}
for f in $(cat $here/files.txt); do
  echo "== $f $(stat -f %z $src/$f)"
  if [ $(stat -f %z $src/$f) -le 9000000 ]; then push $src/$f $f || exit 1; continue; fi
  [ "$(size_on_amiga $f)" = "$(stat -f %z $src/$f)" ] && { echo "  have $f"; continue }
  rm -f $parts/$f.part*; split -b 8m $src/$f $parts/$f.part
  names=()
  for p in $parts/$f.part*; do push $p ${p:t} || exit 1; names+=(DH0:SDK-Install/${p:t}); done
  $tools/m1dos.sh "Join ${names[*]} AS DH0:SDK-Install/$f"
  [ "$(size_on_amiga $f)" = "$(stat -f %z $src/$f)" ] || { echo "join failed for $f"; exit 1 }
  $tools/m1dos.sh "Delete DH0:SDK-Install/$f.part#? QUIET"
done
echo PUSH-DONE
