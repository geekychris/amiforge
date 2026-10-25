#!/usr/bin/env python3
"""m1type.py 'text' [ret] — type text into the OS4 guest via QEMU's monitor sendkey."""
import subprocess, sys, os, time
M1 = os.environ.get("M1", "chriscollins@m1.local")
special = {' ': 'spc', '.': 'dot', '/': 'slash', ':': 'shift-semicolon', '-': 'minus', '_': 'shift-minus',
           '@': 'shift-2', '=': 'equal', ',': 'comma', ';': 'semicolon', '"': 'shift-apostrophe',
           "'": 'apostrophe', '(': 'shift-9', ')': 'shift-0', '!': 'shift-1', '?': 'shift-slash',
           '\n': 'ret'}
keys = []
for c in sys.argv[1]:
    if c in special: keys.append(special[c])
    elif c.isupper(): keys.append('shift-' + c.lower())
    else: keys.append(c)
if len(sys.argv) > 2 and sys.argv[2] == 'ret': keys.append('ret')
cmds = ''.join(f"sendkey {k}\n" for k in keys)
p = subprocess.Popen(['ssh', M1, 'nc -w 3 127.0.0.1 2348'], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
for line in cmds.splitlines(True):
    p.stdin.write(line.encode()); p.stdin.flush(); time.sleep(0.06)
time.sleep(0.4); p.stdin.close(); p.wait()
