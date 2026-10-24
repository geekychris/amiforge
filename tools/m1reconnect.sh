#!/bin/zsh
# m1reconnect.sh — make sure m1's OS4 devbench is talking to the guest's bridge (it needs a
# reconnect after the guest reboots). Leaves a working connection alone; retries for ~2 minutes.
t=${0:A:h}
for i in {1..12}; do
  $t/m1dos.sh 'echo up' 2>/dev/null | grep -q '^up' && { echo "bridge up"; exit 0 }
  $t/m1mcp.py amiga_disconnect '{}' >/dev/null 2>&1
  $t/m1mcp.py amiga_connect '{"mode":"tcp","host":"127.0.0.1","port":2347}' >/dev/null 2>&1
  sleep 10
done
echo "bridge not answering"; exit 1
