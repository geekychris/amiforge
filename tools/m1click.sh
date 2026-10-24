#!/bin/zsh
# m1click.sh X Y [button] — click at screen (X,Y) on the OS4 guest via QEMU's monitor (button 1=left, 2=right, 4=middle).
M1=${M1:-chriscollins@m1.local}
x=$1 y=$2 b=${3:-1}
{ for i in {1..12}; do echo "mouse_move -200 -200"; sleep 0.05; done
  while (( x > 0 || y > 0 )); do dx=$(( x > 50 ? 50 : x )); dy=$(( y > 50 ? 50 : y )); echo "mouse_move $dx $dy"; sleep 0.05; x=$((x-dx)); y=$((y-dy)); done
  echo "mouse_move 1 0"; sleep 0.3; echo "mouse_button $b"; sleep 0.3; echo "mouse_button 0"; sleep 0.5; } | ssh $M1 'nc -w 3 127.0.0.1 2348' >/dev/null
