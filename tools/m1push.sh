#!/bin/zsh
# m1push.sh LOCAL_FILE AMIGA_PATH — copy a file into the running OS4 guest on $M1 (scp to m1, then the bridge).
M1=${M1:-chriscollins@m1.local}
f=$1 dest=$2
tmp=/tmp/af-push-$$-${f:t}
scp -q $f $M1:$tmp || exit 1
${0:A:h}/m1mcp.py amiga_push_file "{\"local_path\": \"$tmp\", \"amiga_path\": \"$dest\"}"; rc=$?
ssh $M1 "rm -f $tmp"
exit $rc
