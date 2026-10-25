#!/usr/bin/env python3
"""m1mcp.py TOOL '{json args}' [port] — call a devbench MCP tool on m1 (via an ssh tunnel on localhost:13000/13001)."""
import os, json, subprocess, sys, urllib.request, socket, time
tool = sys.argv[1]; args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
port = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
# M1=local talks to this machine's devbench directly
local = os.environ.get("M1") == "local"
lport = port if local else 10000 + port
def up():
    s = socket.socket(); s.settimeout(0.5)
    try: s.connect(("127.0.0.1", lport)); return True
    except OSError: return False
    finally: s.close()
if not local and not up():
    subprocess.Popen(["ssh", "-fN", "-o", "ExitOnForwardFailure=yes", "-L", f"{lport}:127.0.0.1:{port}", os.environ.get("M1", "m1.local")]).wait()
    for _ in range(20):
        if up(): break
        time.sleep(0.25)
url = f"http://127.0.0.1:{lport}/mcp"
H = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
def post(body, sid=None):
    h = dict(H)
    if sid: h["mcp-session-id"] = sid
    r = urllib.request.urlopen(urllib.request.Request(url, json.dumps(body).encode(), h), timeout=600)
    data = r.read().decode(); sid2 = r.headers.get("mcp-session-id")
    for line in data.splitlines():
        if line.startswith("data:"): data = line[5:]
    return (json.loads(data) if data.strip() else None), sid2
res, sid = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "m1mcp", "version": "1"}}})
post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
if tool == "list":
    res, _ = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list"}, sid)
    for t in res["result"]["tools"]: print(t["name"], "-", (t.get("description") or "").split("\n")[0][:100])
    sys.exit()
res, _ = post({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": tool, "arguments": args}}, sid)
r = res.get("result", res)
for c in r.get("content", []):
    print(c.get("text", c))
if r.get("isError"): sys.exit(1)
