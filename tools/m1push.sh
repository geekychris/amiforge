#!/bin/zsh
# m1push.sh LOCAL_FILE AMIGA_PATH — copy a file into the running OS4 guest on $M1 (scp to m1, then the bridge).
# ELF files are padded with zeros to a 1 KB multiple: the bridge's push can wedge when the last
# chunk is tiny, and trailing zeros don't matter to an ELF loader.
M1=${M1:-m1.local}
f=$1 dest=$2
tmp=/tmp/af-push-$$-${f:t}
src=$f
if [ "$(head -c 4 $f | od -An -tx1 | tr -d ' ')" = "7f454c46" ]; then
  src=${TMPDIR:-/tmp}/af-pad-$$; cp $f $src
  size=$(stat -f %z $src); pad=$(( (1024 - size % 1024) % 1024 ))
  (( pad )) && head -c $pad /dev/zero >> $src
fi
if [ "$M1" = local ]; then cp $src $tmp || exit 1; else scp -q $src $M1:$tmp || exit 1; fi
[ $src != $f ] && rm -f $src
${0:A:h}/m1mcp.py amiga_push_file "{\"local_path\": \"$tmp\", \"amiga_path\": \"$dest\"}"; rc=$?
if [ "$M1" = local ]; then rm -f $tmp; else ssh $M1 "rm -f $tmp"; fi
exit $rc
