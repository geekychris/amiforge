#!/bin/zsh
# m1shot.sh [out.png] — screenshot the OS4 guest on $M1 (user@host, or "local") via QEMU's monitor.
M1=${M1:-m1.local}
out=${1:-${TMPDIR:-/tmp}/os4.png}
if [ "$M1" = local ]; then
  printf "screendump /tmp/os4.ppm\n" | nc -w 2 127.0.0.1 2348 >/dev/null; sleep 0.5
  sips -s format png /tmp/os4.ppm --out $out >/dev/null && echo $out
else
  ssh $M1 'printf "screendump /tmp/os4.ppm\n" | nc -w 2 127.0.0.1 2348 >/dev/null; sleep 0.5; sips -s format png /tmp/os4.ppm --out /tmp/os4.png >/dev/null' && scp -q $M1:/tmp/os4.png $out && echo $out
fi
