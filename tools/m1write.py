#!/usr/bin/env python3
"""m1write.py LOCAL_FILE AMIGA_PATH — write a small file into the OS4 guest (via the bridge's write_file)."""
import json, subprocess, sys, os
data = open(sys.argv[1], 'rb').read()
here = os.path.dirname(os.path.abspath(__file__))
for off in range(0, max(len(data), 1), 2048):
    chunk = data[off:off + 2048]
    r = subprocess.run([os.path.join(here, 'm1mcp.py'), 'amiga_write_file',
                        json.dumps({'path': sys.argv[2], 'hex_data': chunk.hex(), 'offset': off})],
                       capture_output=True, text=True)
    if r.returncode: sys.exit(r.stdout + r.stderr)
print(f"wrote {len(data)} bytes to {sys.argv[2]}")
