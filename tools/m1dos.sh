#!/bin/zsh
# m1dos.sh 'AMIGADOS COMMAND' — run a shell command in the OS4 guest on $M1 and print its output.
${0:A:h}/m1mcp.py amiga_dos_command "$(python3 -c 'import json,sys; print(json.dumps({"command": sys.argv[1]}))' "$1")"
