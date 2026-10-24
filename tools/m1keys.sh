#!/bin/zsh
# m1keys.sh KEY... — send keys to the OS4 guest via QEMU's monitor `sendkey` (e.g. a b ret shift-a ctrl-c f5 up).
M1=${M1:-chriscollins@m1.local}
{ for k in "$@"; do echo "sendkey $k"; sleep 0.08; done; sleep 0.3; } | ssh $M1 'nc -w 3 127.0.0.1 2348' >/dev/null
